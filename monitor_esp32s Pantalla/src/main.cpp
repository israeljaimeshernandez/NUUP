//Pantalla
#include <LovyanGFX.hpp>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>

#include <WiFi.h>
#include "esp_wifi.h"  // Necesario para usar esp_wifi_set_mac()

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

#include <LoRa.h>
#include <EEPROM.h>
#include <PubSubClient.h>
#include <esp_task_wdt.h>

//EEPROM  Tamaño EEPROM (ESP32 tiene 4KB)
// Definir direcciones para nombre y email (después de tus otras configuraciones) 
#define USER_NAME_ADDR 3000    // 
#define USER_EMAIL_ADDR 3500 //
#define EEPROM_SIZE 4096              //
#define MQTT_CONFIRMED_FLAG_ADDR 350  //
#define USER_ID_ADDR 400              // 
#define LORA_DEVICES_ADDR 500         //
#define CONFIG_DISPOSITIVOS_ADDR 700  // 
#define ALIAS_DISPOSITIVOS 2000       // 
// Configuración WiFi
#define AP_SSID "NUUP_2025"// que permita el acceso directo finalmente no puede hacer nada hasta no ingresar un ID de usuario correcto "nuup"
#define AP_PASS "12345678"
#define WIFI_TIMEOUT 5000 // 30 segundos
#define USER_ID_MAX_LEN 32    // Máximo 32 caracteres para el ID lo puedo cambiar si solo necesito el users.users_id concatenado a la clave NUUP2025
// --- Nueva Configuración para Dispositivos LoRa ---
#define MAX_DISPOSITIVOS 50         // Máximo de dispositivos registrables
#define MAC_LEN 17                  // Longitud de MAC (ej: "A0:B1:C2:D3:E4:F5")
#define VALORES_POR_DISPOSITIVO 5    // Máximo de valores por dispositivo
// Pines LoRa
#define LORA_SS 27
//#define LORA_RST 35
//#define LORA_DIO0 35  //lo quiero cambiar sospecho que algo esta haciendo interferencia se queda pasmado el LORA despues de un tiempo
                    //voy a probar en el PCB de desarollo y vemos si se cambia
// Botón configuración
//#define CONFIG_BUTTON 26
#define BUTTON_HOLD_TIME 3000 // 20 segundos
#define BOTON_PIN 35          // botón de emparejamiento D15 le puse el D33 por que se complica el PCB probar en proto si funciona
//#define LED_WIFFI 27          // LED estatus WIFFI
#define INIT_FLAG_VALUE 0x42
//#define EEPROM_SIZE 512 la tengo definida en seccion de dispositivos
#define MAX_NETWORKS 3
#define SSID_LEN 32
#define PASS_LEN 64
#define NETWORK_SIZE (SSID_LEN + PASS_LEN + 2) // +2 para los bytes de longitud

// ==================== CONSTANTES ====================
#define EN 4

// Pines LCD
#define LCD_CS   15
#define LCD_DC    2
#define LCD_SCK  14
#define LCD_MOSI 13
#define LCD_MISO 12
#define LCD_RST  -1
#define LCD_BL   21

// Pines táctiles
#define TOUCH_CS   33
#define TOUCH_IRQ  36
#define TOUCH_SCK  25
#define TOUCH_MOSI 32
#define TOUCH_MISO 39

// Pines LoRa
#define LORA_SS   27
#define LORA_SCK  18
#define LORA_MISO 19
#define LORA_MOSI 23

//Definiciones PANTALLA 

// ==================== CLASE LGFX ====================
class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ILI9488 _panel_instance;
    lgfx::Bus_SPI _bus_instance;

public:
    LGFX(void) {
        { // Configuración del bus SPI
            auto cfg = _bus_instance.config();
            cfg.spi_host = HSPI_HOST;
            cfg.spi_mode = 0;
            cfg.freq_write = 40000000;
            cfg.freq_read = 16000000;
            cfg.spi_3wire = true;
            cfg.use_lock = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk = LCD_SCK;
            cfg.pin_mosi = LCD_MOSI;
            cfg.pin_miso = LCD_MISO;
            cfg.pin_dc = LCD_DC;
            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }

        { // Configuración del panel
            auto cfg = _panel_instance.config();
            cfg.pin_cs = LCD_CS;
            cfg.pin_rst = LCD_RST;
            cfg.pin_busy = -1;
            cfg.memory_width = 320;
            cfg.memory_height = 480;
            cfg.panel_width = 320;
            cfg.panel_height = 480;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.offset_rotation = 0;
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits = 1;
            cfg.readable = true;
            cfg.invert = false;
            cfg.rgb_order = false;
            cfg.dlen_16bit = false;
            cfg.bus_shared = true;
            _panel_instance.config(cfg);
        }
        setPanel(&_panel_instance);
    }
};

// ==================== OBJETOS GLOBALES Pantalla====================
LGFX lcd;
SPIClass touchSPI(VSPI);
SPIClass loraSPI(HSPI);  // Segundo SPI para LoRa
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);


// Usa los valores de borde COMPLETOS para el mapeo general
#define X_MIN 410     // Esquina Superior Izquierda X
#define X_MAX 3613    // Esquina Superior Derecha X
#define Y_MIN 387     // Esquina Superior Derecha Y
#define Y_MAX 3401    // Esquina Inferior Izquierda Y

// ==================== CONSTANTES Y VARIABLES ====================
const int btnW_x = 50;
const int btnS_x = 170;
const int btn_y = 50;
const int btn_width = 80;
const int btn_height = 80;

bool boton_w = false;
bool boton_s = false;
bool tocado_w=false;
bool tocado_s=false;

//Fin pantalla



// Variables globales para comunicación entre núcleos
volatile bool nuevoMensajeLoRa = false;
String mensajeLoRa = "";
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;



// Variable global para almacenar el último mensaje LoRa recibido
String lastLoRaMessage = "";


bool Usando_lora_nucleo1=false;
extern char macAddress[18]; // Debe contener la MAC del dispositivo actual
bool registrado = false;

typedef struct {
  char mac[MAC_LEN + 1];  // MAC del dispositivo (18 caracteres)
  float valores[VALORES_POR_DISPOSITIVO]; // Valores genéricos
  byte tipoDispositivo;    // Tipo de dispositivo (1-255)
  bool activo;   //voy a usar este para controlar MQTT
} ConfigDispositivo;
ConfigDispositivo configDispositivos[MAX_DISPOSITIVOS];
//para asignar un numero de serial al dispositivo lector este viene del LORA pero ahorita lo fijmos aqui
const String serial_number = "TOPICMYSQL";  //aqui voy a maper la MAC
//para el uso del segundo procesador
TaskHandle_t Task1;

//*****************************
//***   CONFIGURACION MQTT  ***
//*****************************
const char *mqtt_server ="168.231.67.152";
const int  mqtt_port =1883;
const char *mqtt_user="nuup_web";
const char *mqtt_pass ="Kfl-0878";
WiFiClient espClient;
PubSubClient client(espClient);

long lastMsg = 0;
bool send_access_query = false;
char rfid[50] = {0};
char user_name[50] = {0};
char msg[50] = {0};

//*****************************
//***   ALTA MQTT DE MONITOR ***
//*****************************
bool mqttConfirmed = false;          // Bandera de confirmación MQTT
unsigned long lastConfirmationAttempt = 0;
const unsigned long confirmationTimeout = 30000; // 30 segundos para esperar confirmación
const unsigned long confirmationRetryInterval = 10000; // segundos entre reintentos de conexion MQTT
String userID = "";


// Estructura para almacenar credenciales
typedef struct {
  String ssid;
  String password;
  bool active;
} WiFiCredential;
// Objetos globales
WebServer server(80);
DNSServer dnsServer;
bool apMode = false;
bool forceAPMode = false;
//intento de reconectar
unsigned long lastReconnectAttempt = 0;
const unsigned long reconnectInterval = 2 * 60 * 1000; ; // 5 minutos


WiFiCredential savedNetworks[MAX_NETWORKS];
int currentNetwork = -1;
unsigned long buttonPressedTime = 0;
bool buttonActive = false;


// Declaración de funciones
//Incializa en setup
void inicializa_eeprom();
void iniciarLoRaConReintentos();
void clearEEPROM_WIFFI();
void startAPMode();
void handleRoot();
void handleSaveCredentials();
void handleDeleteNetwork();
void handleSelectNetwork();
void saveNetworksToEEPROM();
bool loadNetworksFromEEPROM();
void blinkLED(int times, int delayTime);
void attemptReconnectToAllNetworks();
void handleSetID();
void saveUserIDToEEPROM(const String& id);
bool loadUserIDFromEEPROM();
void callback(char* topic, byte* playload, unsigned int lengt);
void reconnect();
void  checkMemory();
void guardarDispositivos();
void cargarDispositivos();
bool registrarDispositivo(const String &mac);
void imprimirDatos(const String &mac, int contador, float voltaje, float temperatura);
bool isNetworkReady();
void MQTT_ALTA();
void saveMQTTConfirmationState(bool confirmed);
bool loadMQTTConfirmationState();
void procesarMensajeLoRa();
void cargarConfigDispositivos();
void imprimirConfigDispositivo(const String &mac);
void resetConfigDispositivo(const String &mac);
void guardarConfigDispositivos(); 
void imprimirDispositivosRegistrados();
void Reintentar_Wiffi();

void codeForTask1(void *parameter);


// ==================== DECLARACIÓN DE FUNCIONES PANTALLA====================
void hardwareReset();
void dibujarIconoWiFi(int x, int y);
void dibujarIconoSensor(int x, int y);
void dibujarBotones();
void verificarToques();
void calibrarSilencioso();
void calibrarBordes();



//*****************************
//***   TAREA OTRO NUCLEO   ***
//*****************************
void codeForTask1(void *parameter) {
for (;;) {
if(!Usando_lora_nucleo1){

int packetSize = LoRa.parsePacket();
if (packetSize > 0) {

  //para ver como recibo todo
Serial.println("LoRa recibido desde dispositivo ..");

    String received = "";
    while (LoRa.available()) {
        received += (char)LoRa.read();
    }
    received.trim();
 // Guardar mensaje (protegido atómicamente)
        portENTER_CRITICAL(&mux);
        mensajeLoRa = received;
        nuevoMensajeLoRa = true;
        portEXIT_CRITICAL(&mux);

        // Guardar el mensaje recibido en la variable global
        lastLoRaMessage = received;


//para ver como recibo todo
Serial.println("LoRa Mensaje ...: " + lastLoRaMessage);


//
// Contar comas para validar estructura (ahora deben ser 7 comas)
    int commaCount = 0;
    for (int i = 0; i < received.length(); i++) {
        if (received.charAt(i) == ',') commaCount++;
    }

    // Validar que tenga exactamente 7 comas (7 campos + coma final)
    if (commaCount == 8) {
        // Variables para los campos
        String tipo = "";
        String mac = "";
        String val1 = "";
        String val2 = "";
        String val3 = "";
        String val4 = "";
        String val5 = "";
        String nombre = "";
        
        // Posiciones de las comas
        int commaPositions[8];
        int currentComma = 0;
        
        // Encontrar posiciones de todas las comas
        for (int i = 0; i < received.length() && currentComma < 8; i++) {
            if (received.charAt(i) == ',') {
                commaPositions[currentComma] = i;
                currentComma++;
            }
        }

        // Extraer cada campo
        tipo = received.substring(0, commaPositions[0]);
        mac = received.substring(commaPositions[0]+1, commaPositions[1]);
        val1 = received.substring(commaPositions[1]+1, commaPositions[2]);
        val2 = received.substring(commaPositions[2]+1, commaPositions[3]);
        val3 = received.substring(commaPositions[3]+1, commaPositions[4]);
        val4 = received.substring(commaPositions[4]+1, commaPositions[5]);
        val5= received.substring(commaPositions[5]+1, commaPositions[6]);  
        nombre = received.substring(commaPositions[6]+1, commaPositions[7]); // Ahora va hasta la última coma

  // Verificar si ya existe buscar en el arreglo de dispositivos
bool busca_registro=false;
  for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
    if (String(configDispositivos[i].mac) == mac) {
      Serial.println("Dispositivo si esta  registrado: " + mac);
      busca_registro=true;
    }
    }

if(busca_registro){  //solo va a ejecutar el codigo de recepcion LORA normal si existe el dispositivo dado da alta
        // Validar formato de cada campo
        bool formatoCorrecto = true;

        // [Resto de validaciones igual que antes...]
        // Validar tipo (debe ser "002")
        if (tipo != "002") {
            Serial.println("Error: El tipo debe ser '002'");
            formatoCorrecto = false;
        }

        // Validar MAC (formato XX:XX:XX:XX:XX:XX)
        if (mac.length() != 17) {
            Serial.println("Error: Longitud de MAC inválida");
            formatoCorrecto = false;
        } else {
            for (int i = 0; i < mac.length(); i++) {
                if (i == 2 || i == 5 || i == 8 || i == 11 || i == 14) {
                    if (mac.charAt(i) != ':') {
                        Serial.println("Error: Formato de MAC inválido, se esperaban ':' en posiciones específicas");
                        formatoCorrecto = false;
                        break;
                    }
                } else {
                    char c = mac.charAt(i);
                    if (!(c >= '0' && c <= '9') && !(c >= 'A' && c <= 'F') && !(c >= 'a' && c <= 'f')) {
                        Serial.println("Error: Caracteres MAC inválidos");
                        formatoCorrecto = false;
                        break;
                    }
                }
            }
        }

        // Validar valores numéricos
        // Validar val1 (debe ser entero)
        for (char c : val1) {
            if (!isdigit(c)) {
                Serial.println("Error: Valor 1 debe ser entero");
                formatoCorrecto = false;
                break;
            }
        }

        // Validar val2 (debe ser decimal)
        bool puntoEncontrado = false;
        for (char c : val2) {
            if (c == '.') {
                if (puntoEncontrado) {
                    Serial.println("Error: Valor 2 tiene múltiples puntos decimales");
                    formatoCorrecto = false;
                    break;
                }
                puntoEncontrado = true;
            } else if (!isdigit(c)) {
                Serial.println("Error: Valor 2 debe ser decimal");
                formatoCorrecto = false;
                break;
            }
        }

        // Validar val3 (debe ser decimal)
        puntoEncontrado = false;
        for (char c : val3) {
            if (c == '.') {
                if (puntoEncontrado) {
                    Serial.println("Error: Valor 3 tiene múltiples puntos decimales");
                    formatoCorrecto = false;
                    break;
                }
                puntoEncontrado = true;
            } else if (!isdigit(c)) {
                Serial.println("Error: Valor 3 debe ser decimal");
                formatoCorrecto = false;
                break;
            }
        }

        // Validar val4 (debe ser entero)
        for (char c : val4) {
            if (!isdigit(c)) {
                Serial.println("Error: Valor 4 debe ser entero");
                formatoCorrecto = false;
                break;
            }
        }

        // Validar val4 (debe ser entero)
        for (char c : val5) {
            if (!isdigit(c)) {
                Serial.println("Error: Valor 5 debe ser entero");
                formatoCorrecto = false;
                break;
            }
        }

        // Si todo está correcto
        if (formatoCorrecto) {
            Serial.println("Formato correcto!");
            Serial.println("Tipo: " + tipo);
            Serial.println("MAC: " + mac);
            Serial.println("Valor 1: " + val1);
            Serial.println("Valor 2: " + val2);
            Serial.println("Valor 3: " + val3);
            Serial.println("Valor 4: " + val4);
            Serial.println("Valor 5: " + val5);
            Serial.println("Nombre: " + nombre);

            // Convertir a valores numéricos
            int valor1 = val1.toInt();
            float valor2 = val2.toFloat();
            float valor3 = val3.toFloat();
            int valor4 = val4.toInt();
            int valor5 = val5.toInt();

            Serial.print("Valores convertidos: ");
            Serial.print(valor1); Serial.print(", ");
            Serial.print(valor2, 2); Serial.print(", ");
            Serial.print(valor3, 5); Serial.print(", ");
            Serial.println(valor4);
            Serial.println(valor5);

        } else {
            Serial.println("El mensaje contiene errores de formato");
        }
    } else {
        Serial.println("No existe el dispositivo registrado ..."); //quitarlo en produccion solo para depuracion aqui es donde puede escuchar otroa dispositivos NUUP no registrados en este monitor
    }
 }


    } //LORA detecto recepcion


    }
    // ⏱ Delay mínimo para no saturar la CPU, pero evitar perder paquetes
    vTaskDelay(5 / portTICK_PERIOD_MS);
  }
}


//temporal para forzar apmode una vez si no hay wiffi
bool una_APmode=true;

void setup() {

disableCore0WDT(); // Desactiva watchdog para el núcleo 0
disableCore1WDT(); // Desactiva watchdog para el núcleo 1
  // 0. Inicialización básica SERIAL PANTLALLA  EEPROM
  Serial.begin(115200);
      hardwareReset();
    delay(1000);

   // Inicialización SPI para touch
    touchSPI.begin(TOUCH_SCK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
    touch.begin(touchSPI);
    touch.setRotation(1);

  lcd.init();
    pinMode(LCD_BL, OUTPUT);
    digitalWrite(LCD_BL, HIGH);
    
    lcd.setFont(&fonts::Font0);
    lcd.setTextSize(1);
 dibujarBotones();
    
    Serial.println("Pantalla inicializada correctamente");
//calibrarSilencioso();
//calibrarBordes();
  while (!Serial && millis() < 5000); // Esperar hasta 5 segundos para Serial en desarrollo
 inicializa_eeprom();
//clearEEPROM_WIFFI();  //solo para configuracion inicial
delay(1000);


// 2. Cargar configuración Wiffi existente y USER_ID capturado por usuario
    if (!loadNetworksFromEEPROM()) {
      Serial.println("Error al cargar redes de EEPROM");
    }
delay(1000);
Serial.println("Se cargaron redes de EEPROM");

    
// 3. Cargar configuración UserID capturado por usuario

   if (!loadUserIDFromEEPROM()) {
      Serial.println("Error al cargar ID de EEPROM");
     }
      Serial.println("Cargando  ID de EEPROM");

    // Si no hay ID guardado, forzar el modo AP para que el usuario lo ingrese
    if (userID.isEmpty()) {
   Serial.println("No hay Cargando  ID de EEPROM ");
  } else {
    // Intentar conectar a WiFi normalmente
   Serial.println("ID cargado en  EEPROM ");
  }
delay(1000);

 // 4. Manejo de arreglo de redes WiFi si existe alguna actualmente dada de alta

 bool hasNetworks = false;
  for (int i = 0; i < MAX_NETWORKS; i++) {
    if (savedNetworks[i].ssid.length() > 0) {
      hasNetworks = true;
      break;
    }
  }

// 6. Inicialización LoRa con manejo de errores mejorado
iniciarLoRaConReintentos();

  // 7. Configuración del segundo núcleo
  Serial.println("Iniciando tarea en segundo núcleo...");
  if (xTaskCreatePinnedToCore(
    codeForTask1,    // Función de la tarea
    "Task_1",       // Nombre
    4096,           // Stack size aumentado
    NULL,           // Parámetros
    1,              // Prioridad
    &Task1,         // Handle
    0               // Núcleo
  ) != pdPASS) {
    Serial.println("Error al crear tarea en segundo núcleo");
  }

 //8. configura DISPOSITIVOS
// Cargar dispositivos registrados
cargarDispositivos();  // 
delay(1000);

//Wiffi 
 //attemptReconnectToAllNetworks();  
 if (WiFi.status() != WL_CONNECTED) {
  attemptReconnectToAllNetworks();     
}


// 9. Configuración MQTT con parámetros mejorados

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  client.setBufferSize(512);  // Buffer aumentado para mensajes grandes
  client.setKeepAlive(60);    // Keepalive de 60 segundos
  client.setSocketTimeout(30); // Timeout de 30 segundos
delay(1000);

// 10. Alta de monitor
mqttConfirmed = loadMQTTConfirmationState(); // Cargar estado persistente
// Si ya estaba confirmado, imprimir mensaje
if(mqttConfirmed) {
  Serial.println("Estado MQTT: Confirmación alta encontrada en EEPROM");
} else {
  Serial.println("Estado MQTT: Esperando configuracion de alta  inicial");
}

Serial.println("Setup completado");
}


void loop() {

// 1. Manejo básico de conexiones
if (!client.connected() && WiFi.status() == WL_CONNECTED && !forceAPMode) {
    reconnect();  //Solo para reconectar y configuracion de subscripciones
  }
 client.loop();


 if ( client.connected() && WiFi.status() == WL_CONNECTED && !forceAPMode)  {
 MQTT_ALTA();  //Para solicitar el alta en broker
  }




 // 2. Si estamos en modo AP, manejar eso y salir
if (boton_w) { //mientras no exista comando en pantalla
    if(una_APmode){
  startAPMode();
una_APmode = false;
forceAPMode = true;
}
  }

if (forceAPMode) {
    dnsServer.processNextRequest();
    server.handleClient();
    static unsigned long lastBlink = 0;
    if (millis() - lastBlink > 300) {
        lastBlink = millis();
    }
    return;
  }

  
  
  //3. Variables para el control de memoria (declaradas una sola vez)
  static unsigned long lastMemoryCheck = 0;
  const unsigned long memoryCheckInterval = 30000; // 30 segundos en milisegundos

  // 4. Verificación periódica de memoria (solo para debug)
   if (millis() - lastMemoryCheck > memoryCheckInterval) {
    lastMemoryCheck = millis();
    checkMemory();
  }




// 6. Comportamiento en modo normal 
  // 6.1 En caso de que tenga WIFFI conectado a MQTT y este dado de alta 
  if (WiFi.status() == WL_CONNECTED && client.connected() && mqttConfirmed && !forceAPMode) {  

  // Verificar si hay mensaje LoRa nuevo (protegido atómicamente)
  bool hayDatos = false;
  String mensajeActual = "";
  
  portENTER_CRITICAL(&mux);
  if (nuevoMensajeLoRa) {
    hayDatos = true;
    mensajeActual = mensajeLoRa;
    nuevoMensajeLoRa = false;
  }
  portEXIT_CRITICAL(&mux);

  // Publicar inmediatamente si hay datos y estamos conectados
  if (hayDatos && WiFi.status() == WL_CONNECTED && client.connected() && mqttConfirmed) {
    String miMac = WiFi.macAddress();
    miMac.replace(":", "_");
    String topico = "NUUP/" + miMac;
    
    if (client.publish(topico.c_str(), mensajeActual.c_str())) {
      Serial.print("Publicado: ");
      Serial.println(mensajeActual);
    } else {
      Serial.println("Error al publicar");
    }
  }
    
  } 

 
///dispositivos
// 7. Manejo del botón S para registro de dispositivo o emparejamiento
if (boton_s){
      Usando_lora_nucleo1=true;
      delay(100);
    Serial.println("Modo recepción activado. Esperando solicitudes REG o BAJA...");
        // Solo procesar mensajes (el OK_REG se envía desde procesarMensajeLoRa)
        procesarMensajeLoRa();
        delay(100); // Pequeña pausa para evitar saturación
      }
      Usando_lora_nucleo1=false;



      //8.  Verificaciones de botones en TOUCH
      verificarToques(); //solo programado dos botones 
    static uint32_t lastPrint = 0;
    if(millis() - lastPrint > 1000) {
        Serial.printf("Estado: W=%d S=%d\n", boton_w, boton_s);
        lastPrint = millis();
    }
    
    delay(10);

  //0. Si no hay Wiffi 
 if (WiFi.status() != WL_CONNECTED && !forceAPMode) {
Reintentar_Wiffi();
  }


  }


// Implementación de funciones

void startAPMode() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  dnsServer.start(53, "*", WiFi.softAPIP());
  
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSaveCredentials);
  server.on("/delete", HTTP_POST, handleDeleteNetwork);
  server.on("/select", HTTP_POST, handleSelectNetwork);
  server.on("/setid", HTTP_POST, handleSetID);  // 👉 Aquí se agrega la ruta nueva
  server.onNotFound(handleRoot);
  server.begin();
  
  Serial.println("\nModo AP activado");
  Serial.print("SSID: "); Serial.println(AP_SSID);
  Serial.print("IP: "); Serial.println(WiFi.softAPIP());
  

}

String getCheckedStatus(bool active) {
  return active ? " checked" : "";
}

void handleRoot() {

  // Si no hay un ID guardado, mostrar SOLO el formulario para capturar el ID
  if (userID.isEmpty()) {
    String html = R"=====(
<!DOCTYPE html>
<html>
<head>
  <title>Configuración ID - NUUP</title>
  <meta name='viewport' content='width=device-width, initial-scale=1'>
  <style>
    body {
      background-color: #121212;
      color: #FFD700;
      font-family: Arial, sans-serif;
      display: flex;
      justify-content: center;
      align-items: center;
      height: 100vh;
      margin: 0;
      padding: 20px;
    }
    .container {
      background-color: #1E1E1E;
      border: 2px solid #FFD700;
      border-radius: 10px;
      padding: 30px;
      width: 90%;
      max-width: 500px;
      box-shadow: 0 0 20px rgba(255, 215, 0, 0.3);
    }
    h1 {
      color: #FFD700;
      text-align: center;
      margin-bottom: 25px;
      font-size: 24px;
    }
    form {
      display: flex;
      flex-direction: column;
    }
    input {
      background-color: #333;
      color: #FFD700;
      border: 2px solid #FFD700;
      border-radius: 5px;
      padding: 12px;
      margin-bottom: 15px;
      font-size: 16px;
    }
    button {
      background-color: #FFD700;
      color: #121212;
      border: none;
      border-radius: 5px;
      padding: 12px;
      font-size: 16px;
      font-weight: bold;
      cursor: pointer;
      transition: background-color 0.3s;
    }
    button:hover {
      background-color: #FFA500;
    }
  </style>
</head>
<body>
  <div class="container">
    <h1>Configurar ID de Usuario</h1>
    <form action='/setid' method='POST'>
      <input type='text' name='newid' placeholder='Ingresa tu ID' maxlength=')" + String(USER_ID_MAX_LEN) + R"=====(' required>
      <button type='submit'>Guardar ID</button>
    </form>
  </div>
</body>
</html>
)=====";
    server.send(200, "text/html", html);
    return; // Salir de la función después de enviar esta página
  }

  
  String networksList = "";
  for(int i = 0; i < MAX_NETWORKS; i++) {
    if(savedNetworks[i].ssid.length() > 0) {
      networksList += "<div class='network-item'>";
      networksList += "<input type='radio' name='selectedNetwork' id='net" + String(i) + "' value='" + String(i) + "'";
      networksList += getCheckedStatus(savedNetworks[i].active) + ">";
      networksList += "<label for='net" + String(i) + "'>" + savedNetworks[i].ssid + "</label>";
      networksList += "<button type='button' onclick='deleteNetwork(" + String(i) + ")'>Borrar</button>";
      networksList += "</div>";
    }
  }


String idSection = "<h3>ID de usuario actual:</h3>";
idSection += "<p><strong>" + userID + "</strong></p>";
idSection += "<form action='/setid' method='POST'>";
idSection += "<input type='text' name='newid' placeholder='Nuevo ID' maxlength='" + String(USER_ID_MAX_LEN) + "' required>";
idSection += "<button type='submit'>Actualizar ID</button>";
idSection += "</form><hr>";

  String html = R"=====(
<!DOCTYPE html>
<html>
<head>
  <title>Configuración WiFi - NUUP</title>
  <meta name='viewport' content='width=device-width, initial-scale=1'>
  <style>
    body {
      background-color: #121212;
      color: #FFD700;
      font-family: Arial, sans-serif;
      display: flex;
      justify-content: center;
      align-items: center;
      height: 100vh;
      margin: 0;
      padding: 20px;
    }
    .container {
      background-color: #1E1E1E;
      border: 2px solid #FFD700;
      border-radius: 10px;
      padding: 30px;
      width: 90%;
      max-width: 500px;
      box-shadow: 0 0 20px rgba(255, 215, 0, 0.3);
    }
    h1 {
      color: #FFD700;
      text-align: center;
      margin-bottom: 25px;
      font-size: 24px;
    }
    .device-title {
      color: #FFD700;
      text-align: center;
      font-size: 18px;
      margin-bottom: 5px;
      font-weight: bold;
    }
    form {
      display: flex;
      flex-direction: column;
    }
    input {
      background-color: #333;
      color: #FFD700;
      border: 2px solid #FFD700;
      border-radius: 5px;
      padding: 12px;
      margin-bottom: 15px;
      font-size: 16px;
    }
    input:focus {
      outline: none;
      border-color: #FFA500;
      box-shadow: 0 0 5px rgba(255, 215, 0, 0.5);
    }
    button {
      background-color: #FFD700;
      color: #121212;
      border: none;
      border-radius: 5px;
      padding: 12px;
      font-size: 16px;
      font-weight: bold;
      cursor: pointer;
      transition: background-color 0.3s;
      margin: 5px 0;
    }
    button:hover {
      background-color: #FFA500;
    }
    .network-list {
      margin: 20px 0;
    }
    .network-item {
      display: flex;
      align-items: center;
      margin: 10px 0;
      padding: 10px;
      background-color: #333;
      border-radius: 5px;
    }
    .network-item label {
      flex-grow: 1;
      margin-left: 10px;
    }
    .network-item button {
      padding: 8px 12px;
      background-color: #ff3333;
    }
    .network-item button:hover {
      background-color: #cc0000;
    }
    ::placeholder {
      color: #888;
      opacity: 1;
    }
  </style>
  <script>
    function deleteNetwork(index) {
      if(confirm('¿Borrar esta red WiFi?')) {
        fetch('/delete', {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
          body: 'index=' + index
        }).then(response => {
          if(response.ok) location.reload();
        });
      }
    }
  </script>
</head>
<body>
  <div class="container">
    <div class="device-title">Dispositivo NUUP</div>
    <h1>Configurar WiFi</h1>
    )=====" + idSection + R"=====(   <!-- ← AÑADIDO AQUÍ -->
    <div class="network-list">
      <h3>Redes guardadas:</h3>
      <form id="networksForm">
        )=====" + networksList + R"=====(
        <button type="button" onclick="submitSelection()">Conectar a red seleccionada</button>
      </form>
    </div>
    
    <h3>Agregar nueva red:</h3>
    <form action='/save' method='POST'>
      <input type='text' name='ssid' placeholder='Nombre de la red (SSID)' required>
      <input type='password' name='pass' placeholder='Contraseña' required>
      <button type='submit'>Guardar Configuración</button>
    </form>
  </div>
  
  <script>
    function submitSelection() {
      const form = document.getElementById('networksForm');
      const selected = form.querySelector('input[name="selectedNetwork"]:checked');
      if(selected) {
        fetch('/select', {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
          body: 'index=' + selected.value
        }).then(response => {
          if(response.ok) {
            alert('Red seleccionada. Reconectando...');
            setTimeout(() => { location.reload(); }, 1000);
          }
        });
      } else {
        alert('Selecciona una red primero');
      }
    }
  </script>
</body>
</html>
)=====";
  
  server.send(200, "text/html", html);
}

void handleSaveCredentials() {
  if(server.hasArg("ssid") && server.hasArg("pass")) {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    
    // Buscar espacio vacío o red más antigua
    int indexToSave = -1;
    for(int i = 0; i < MAX_NETWORKS; i++) {
      if(savedNetworks[i].ssid.length() == 0) {
        indexToSave = i;
        break;
      }
    }
    
    // Si no hay espacio, reemplazar la primera (podría mejorarse con timestamp)
    if(indexToSave == -1) {
      indexToSave = 0;
    }
    
    savedNetworks[indexToSave].ssid = ssid;
    savedNetworks[indexToSave].password = pass;
    savedNetworks[indexToSave].active = true;
    
    // Desactivar las demás
    for(int i = 0; i < MAX_NETWORKS; i++) {
      if(i != indexToSave) {
        savedNetworks[i].active = false;
      }
    }
    
    saveNetworksToEEPROM();
    server.send(200, "text/html", "<html><body><h2>Credenciales guardadas! Reconectando...</h2></body></html>");
    delay(1000);
    ESP.restart();
  } else {
    server.send(400, "text/plain", "Faltan parámetros");
  }
}

void handleDeleteNetwork() {
  if(server.hasArg("index")) {
    int index = server.arg("index").toInt();
    if(index >= 0 && index < MAX_NETWORKS) {
      savedNetworks[index].ssid = "";
      savedNetworks[index].password = "";
      savedNetworks[index].active = false;
      saveNetworksToEEPROM();
      server.send(200, "text/plain", "OK");
    } else {
      server.send(400, "text/plain", "Índice inválido");
    }
  } else {
    server.send(400, "text/plain", "Falta parámetro index");
  }
}

void handleSelectNetwork() {
  if(server.hasArg("index")) {
    int index = server.arg("index").toInt();
    if(index >= 0 && index < MAX_NETWORKS && savedNetworks[index].ssid.length() > 0) {
      // Desactivar todas
      for(int i = 0; i < MAX_NETWORKS; i++) {
        savedNetworks[i].active = false;
      }
      // Activar la seleccionada
      savedNetworks[index].active = true;
      saveNetworksToEEPROM();
      server.send(200, "text/plain", "OK");
      delay(1000);
      ESP.restart();
    } else {
      server.send(400, "text/plain", "Índice inválido o red vacía");
    }
  } else {
    server.send(400, "text/plain", "Falta parámetro index");
  }
}

void handleSetID() {
  if (server.hasArg("newid")) {
    String newID = server.arg("newid");
    newID.trim();

    if (newID.length() > 0 && newID.length() <= USER_ID_MAX_LEN) {
      saveUserIDToEEPROM(newID);
      userID = newID;
      
      // Redirigir a la página principal (que ahora mostrará la interfaz completa)
      server.send(200, "text/html", 
        "<html><body><script>window.location.href='/';</script></body></html>");
    } else {
      server.send(400, "text/plain", "ID inválido");
    }
  } else {
    server.send(400, "text/plain", "Falta parámetro newid");
  }
}


void saveNetworksToEEPROM() {
  int address = 1; // Empezamos en 1 porque 0 es el flag de inicialización

  for(int i = 0; i < MAX_NETWORKS; i++) {
    // Guardar SSID
    int ssidLen = savedNetworks[i].ssid.length();
    if(ssidLen > SSID_LEN) ssidLen = SSID_LEN;
    EEPROM.write(address++, ssidLen);

    for(int j = 0; j < ssidLen; j++) {
      EEPROM.write(address++, savedNetworks[i].ssid[j]);
    }
    // Rellenar con ceros si ssidLen es menor que SSID_LEN
    for(int j = ssidLen; j < SSID_LEN; j++) {
      EEPROM.write(address++, 0);
    }

    // Guardar contraseña
    int passLen = savedNetworks[i].password.length();
    if(passLen > PASS_LEN) passLen = PASS_LEN;
    EEPROM.write(address++, passLen);

    for(int j = 0; j < passLen; j++) {
      EEPROM.write(address++, savedNetworks[i].password[j]);
    }
    // Rellenar con ceros si passLen es menor que PASS_LEN
    for(int j = passLen; j < PASS_LEN; j++) {
      EEPROM.write(address++, 0);
    }

    // Guardar estado activo
    EEPROM.write(address++, savedNetworks[i].active ? 1 : 0);
  }

  EEPROM.commit();
}


bool loadNetworksFromEEPROM() {
  int address = 1; // Empezamos en 1 porque 0 es el flag de inicialización
  bool success = true;

  for (int i = 0; i < MAX_NETWORKS; i++) {
    // Leer SSID
    int ssidLen = EEPROM.read(address++);
    if (ssidLen < 0 || ssidLen > SSID_LEN) {
      Serial.printf("Error: Longitud SSID inválida en red %d: %d\n", i, ssidLen);
      success = false;
      ssidLen = 0; // Usar longitud cero para evitar problemas
    }

    char ssidData[SSID_LEN + 1] = {0};
    for (int j = 0; j < ssidLen; j++) {
      ssidData[j] = EEPROM.read(address++);
      // Verificar caracteres no imprimibles
      if (ssidData[j] < 32 || ssidData[j] > 126) {
        ssidData[j] = '?'; // Reemplazar caracteres inválidos
      }
    }
    // Saltar relleno si es necesario
    address += (SSID_LEN - ssidLen);
    
    savedNetworks[i].ssid = String(ssidData);

    // Leer contraseña
    int passLen = EEPROM.read(address++);
    if (passLen < 0 || passLen > PASS_LEN) {
      Serial.printf("Error: Longitud Pass inválida en red %d: %d\n", i, passLen);
      success = false;
      passLen = 0;
    }

    char passData[PASS_LEN + 1] = {0};
    for (int j = 0; j < passLen; j++) {
      passData[j] = EEPROM.read(address++);
      // No verificamos caracteres de contraseña por seguridad
    }
    // Saltar relleno si es necesario
    address += (PASS_LEN - passLen);
    
    savedNetworks[i].password = String(passData);

    // Leer estado activo
    byte active = EEPROM.read(address++);
    savedNetworks[i].active = (active == 1);
    
    // Verificar si el estado active es válido (0 o 1)
    if (active != 0 && active != 1) {
      Serial.printf("Error: Valor active inválido en red %d: %d\n", i, active);
      success = false;
      savedNetworks[i].active = false;
    }

    // Solo imprimir redes con SSID no vacío
    if (savedNetworks[i].ssid.length() > 0) {
      Serial.printf("Red %d cargada: SSID='%s', Pass=%s, Activa=%d\n", 
                   i, 
                   savedNetworks[i].ssid.c_str(), 
                   savedNetworks[i].password.length() > 0 ? "[oculta]" : "vacía",
                   savedNetworks[i].active ? 1 : 0);
    }
  }

  // Verificación de dirección final
  if (address > EEPROM_SIZE) {
    Serial.printf("Error: Dirección EEPROM excede tamaño máximo (%d > %d)\n", address, EEPROM_SIZE);
    success = false;
  }

  return success;
}

void blinkLED(int times, int delayTime) {
  for(int i = 0; i < times; i++) {
    //digitalWrite(LED_WIFFI, HIGH);
    delay(delayTime);
    //digitalWrite(LED_WIFFI, LOW);
    delay(delayTime); 
  }
}

void attemptReconnectToAllNetworks() {
   WiFi.mode(WIFI_STA);
  for (int i = 0; i < MAX_NETWORKS; i++) {
    if (savedNetworks[i].ssid.length() > 0) {
      Serial.print("Intentando reconectar a: ");
      Serial.println(savedNetworks[i].ssid);
 //cofigo para rotar la MAC y no tenga bloqueos por conexiones fantasmas 
 // Inicializa el WiFi en modo STA
  WiFi.mode(WIFI_STA);
  delay(100);  // Pequeña pausa para estabilizar
  // Configura una nueva MAC
  uint8_t newMAC[6];
  WiFi.macAddress(newMAC);        // Obtiene la MAC actual
  newMAC[5] = random(0, 255);    // Cambia el último byte (aleatorio)

  // Aplica la nueva MAC (¡Solo después de WiFi.mode()!)
  esp_err_t result = esp_wifi_set_mac(WIFI_IF_STA, newMAC);
  if (result != ESP_OK) {
    Serial.printf("Error al cambiar MAC: %d\n", result);
    return;
  }
  //    
      WiFi.begin(savedNetworks[i].ssid.c_str(), savedNetworks[i].password.c_str());

      unsigned long startAttempt = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < WIFI_TIMEOUT) {
        delay(500);
        Serial.print(".");
      }

      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nConectado exitosamente a:");
        Serial.println(savedNetworks[i].ssid);
        currentNetwork = i;

        // Marcar solo esta red como activa
        for (int j = 0; j < MAX_NETWORKS; j++) {
          savedNetworks[j].active = (j == i);
        }

        saveNetworksToEEPROM();
        return; // Salir porque ya se conectó
      } else {
        Serial.println("\nNo se pudo conectar.");
      }
    }
else{     Serial.println("No Existen redes guardadas.");}
  }

  Serial.println("No se pudo conectar a ninguna red.");
}


void saveUserIDToEEPROM(const String& id) {
  int len = id.length();
  if (len > USER_ID_MAX_LEN) len = USER_ID_MAX_LEN;

  EEPROM.write(USER_ID_ADDR, len);  // Guardar longitud

  for (int i = 0; i < len; i++) {
    EEPROM.write(USER_ID_ADDR + 1 + i, id[i]);
  }

  // Rellenar con ceros si sobran caracteres
  for (int i = len; i < USER_ID_MAX_LEN; i++) {
    EEPROM.write(USER_ID_ADDR + 1 + i, 0);
  }

  EEPROM.commit();
  Serial.println("📝 ID guardado en EEPROM: " + id);
}

bool loadUserIDFromEEPROM() {
  bool success = true;
  int len = EEPROM.read(USER_ID_ADDR);
  if (len > USER_ID_MAX_LEN) len = USER_ID_MAX_LEN;

  char buffer[USER_ID_MAX_LEN + 1] = {0};
  for (int i = 0; i < len; i++) {
    buffer[i] = EEPROM.read(USER_ID_ADDR + 1 + i);
  }

  buffer[len] = '\0';
  userID = String(buffer);
  Serial.println("🔄 ID cargado desde EEPROM: " + userID);
  return success;
}


void callback(char* topic, byte* payload, unsigned int length) {
  Serial.println("callback MQTT ejecutado recepcion-->");  // al principio de la función
  // Crear buffer seguro para el mensaje
  char message[length + 1];
  strncpy(message, (char*)payload, length);
  message[length] = '\0';

  String strTopic = String(topic);
  String strMessage = String(message);

  Serial.printf("Mensaje recibido MQTT TOPIC[%s]: message--> %s\n", topic, message);


//seccion de validacion de alta de cualquier tipo de dispositivo:
 // Procesar confirmación de alta
 /*
  if (strTopic.endsWith("/confirmacion/") && strMessage == "registrado") {
    // Extraer MAC del topic
    int lastSlash = strTopic.lastIndexOf('/');
    int prevSlash = strTopic.lastIndexOf('/', lastSlash - 1);
    String mac = strTopic.substring(prevSlash + 1, lastSlash);
    
    // Extraer tipo de dispositivo
    int tipoSlash = strTopic.indexOf('/', 5); // después de "alta/"
    int tipo = strTopic.substring(5, tipoSlash).toInt();

    // Buscar o crear entrada para este dispositivo
    int index = -1;
    for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
      if (String(configDispositivos[i].mac) == mac) {
        index = i;
        break;
      }
      if (index == -1 && configDispositivos[i].mac[0] == '\0') {
        index = i;
      }
    }
    
    if (index != -1) {
      mac.toCharArray(configDispositivos[index].mac, MAC_LEN + 1);
      configDispositivos[index].tipoDispositivo = tipo;
      
      // Inicializar valores
      for (int i = 0; i < VALORES_POR_DISPOSITIVO; i++) {
        configDispositivos[index].valores[i] = 0.0;
      }
      
      guardarConfigDispositivos();
      Serial.printf("Dispositivo registrado: MAC=%s, Tipo=%d\n", mac.c_str(), tipo);
    }
    return;
  }
*/
/*
  // Procesar configuración de valores (alta/X/MAC/config/valorY)
  if (strTopic.indexOf("/config/valor") != -1) {
    // Extraer MAC
    int macStart = strTopic.indexOf('/', 5) + 1; // después de "alta/X/"
    int macEnd = strTopic.indexOf('/', macStart);
    String mac = strTopic.substring(macStart, macEnd);
    
    // Extraer índice del valor (valor1, valor2, etc.)
    int valIndex = strTopic.substring(strTopic.lastIndexOf('r') + 1).toInt() - 1;
    
    if (valIndex >= 0 && valIndex < VALORES_POR_DISPOSITIVO) {
      float valor = strMessage.toFloat();
      
      // Buscar dispositivo
      for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
        if (String(configDispositivos[i].mac) == mac) {
          configDispositivos[i].valores[valIndex] = valor;
          guardarConfigDispositivos();
          Serial.printf("Config actualizada - MAC: %s, Valor%d: %.2f\n", 
                       mac.c_str(), valIndex + 1, valor);
          break;
        }
      }
    }
    return;
  }
*/


// ALTA MONITOR Validación estricta para el topic de confirmación solo considerar que 1 es para monitor modelo NUUP01

// ALTA MONITOR Validación estricta para el topic de confirmación
if (strcmp(topic, "alta/1/confirmacion/") == 0) {
    // Obtener la MAC del dispositivo
    String miMac = WiFi.macAddress();
    miMac.replace("-", ":");
    
    // Convertir el mensaje a String para procesarlo
    String mensajeRecibido = String(message);
    Serial.println("Recibiendo mensaje de confirmacion: " + mensajeRecibido);
    
    // Verificar que el mensaje comience con nuestra MAC y "registrado"
    if (mensajeRecibido.startsWith(miMac + ",registrado")) {
        // Separar los componentes del mensaje (formato: MAC,registrado,nombre,email)
        int primeraComa = mensajeRecibido.indexOf(',');
        int segundaComa = mensajeRecibido.indexOf(',', primeraComa + 1);
        int terceraComa = mensajeRecibido.indexOf(',', segundaComa + 1);

        if (segundaComa != -1 && terceraComa != -1) {
            String nombreUsuario = mensajeRecibido.substring(segundaComa + 1, terceraComa);
            String emailUsuario = mensajeRecibido.substring(terceraComa + 1);

            // Guardar en EEPROM usando tus direcciones definidas
            EEPROM.begin(EEPROM_SIZE);
            
           
            // Guardar nombre (primero la longitud)
            int nombreLen = nombreUsuario.length();
            EEPROM.write(USER_NAME_ADDR, nombreLen);
            for (int i = 0; i < nombreLen; i++) {
                EEPROM.write(USER_NAME_ADDR + 1 + i, nombreUsuario[i]);
            }
            
            // Guardar email
            int emailLen = emailUsuario.length();
            EEPROM.write(USER_EMAIL_ADDR, emailLen);
            for (int i = 0; i < emailLen; i++) {
                EEPROM.write(USER_EMAIL_ADDR + 1 + i, emailUsuario[i]);
            }
            
            EEPROM.commit();
            EEPROM.end();

            Serial.println("CONFIRMACION RECIBIDA - Alta validada correctamente");
            Serial.println("Nombre guardado: " + nombreUsuario);
            Serial.println("Email guardado: " + emailUsuario);
            
            mqttConfirmed = true;
            // saveMQTTConfirmationState(true); // Descomenta si necesitas guardar el estado
            delay(3000); // Espera para evitar conflictos
        } else {
            Serial.println("Formato de mensaje incorrecto. Faltan datos de usuario");
        }
    } else {
        Serial.printf("ADVERTENCIA - Mensaje no reconocido en alta/1/confirmacion/: '%s'\n", message);
        
        // Depuración adicional
        Serial.println("Contenido hexadecimal del mensaje:");
        for (unsigned int i = 0; i < length; i++) {
            Serial.printf("%02X ", payload[i]);
        }
        Serial.println();
    }
    return;
}
//termina confirmacion de alta


  // 4. Construir el topic esperado de forma segura
  /*
  char expectedCommandTopic[60];
  snprintf(expectedCommandTopic, sizeof(expectedCommandTopic), "%s/comando", serial_number.c_str());

  // 5. Comparar topics (evitando Strings)
  if (strcmp(topic, expectedCommandTopic) == 0) {
    // 6. Procesar comandos
    if (strcmp(message, "on") == 0) {
      Serial.println("Mensaje de encendido recibido");
      // Aquí tu lógica para el comando 'on'
    } 
    else if (strcmp(message, "off") == 0) {
      Serial.println("Mensaje de apagado recibido");
      // Aquí tu lógica para el comando 'off'
    }
  }*/


}

void reconnect() {
  static unsigned long lastAttempt = 0;
  const unsigned long retryInterval = 5000;

  if (millis() - lastAttempt < retryInterval) return;
  lastAttempt = millis();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("No se pudo conectar a MQTT --> Error: WiFi no conectado");
    return;
  }

  char clientId[40];
  snprintf(clientId, sizeof(clientId), "Monitor-%08X", (uint32_t)ESP.getEfuseMac());

  if (client.connect(clientId, mqtt_user, mqtt_pass)) {
    Serial.println("Conexión MQTT establecida preparado para el alta de dispositivo.............");
    Serial.println("Cliente:"+String(clientId)+" Usuario: "+ mqtt_user+" PAssword:"+ mqtt_pass);

  
    // 2. Suscripciones con QoS 1 (confirmación de recepción)
    client.subscribe("alta/1/confirmacion/", 1);
Serial.println("Subscripcion: alta/1/confirmacion/");
    delay(50);
    client.subscribe((String(serial_number) + "/command").c_str(), 1);
Serial.println("Subscripcion: /command");
   delay(50);
    client.subscribe((String(serial_number) + "/estatus").c_str(), 1);
Serial.println("Subscripcion: /estatus");
    delay(50);    

  } else {
    Serial.printf("Error en conexión MQTT pero wiffi conectado --> (estado: %d)\n", client.state());
  }
} 

void checkMemory() {
  Serial.println("\n--- Memory Report ---");
  Serial.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());
  Serial.printf("Min Free Heap: %d bytes\n", ESP.getMinFreeHeap());
  Serial.printf("Max Alloc Heap: %d bytes\n", ESP.getMaxAllocHeap());
  Serial.printf("Main Task Stack: %d bytes free\n", uxTaskGetStackHighWaterMark(NULL));
  Serial.printf("PSRAM: %d bytes free\n", ESP.getFreePsram());
  Serial.println("---------------------");
}



void procesarMensajeLoRa() {
  if (LoRa.parsePacket()) {
    String mensaje = LoRa.readString();
    mensaje.trim();
    Serial.println("PROCESAR MENSAJE LoRa recibido desde dispositivo para configuracion de alta o baja: " + mensaje);

    // Procesar mensajes de alta (REG)
    if (mensaje.startsWith("REG")) {
      int tipoDispositivo = mensaje.substring(3, 5).toInt(); // Extraer el tipo (dos posiciones)
      String mac = mensaje.substring(6); // Extraer MAC (ej: "A8:42:E3:4A:85:E8")
      
      Serial.println("Solicitud de alta recibida - Tipo: " + String(tipoDispositivo) + " MAC: " + mac);

      // Verificar MAC válida
      if (mac.length() != MAC_LEN) {
        Serial.println("Error: MAC inválida");
        return;
      }

      bool confirmado = false;
      
      // Verificar si ya está registrado
      for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
        if (String(configDispositivos[i].mac) == mac) {
          confirmado = true;
          break;
        }
      }

      // Obtener la MAC del dispositivo que está respondiendo (este módulo)
      String miMac = WiFi.macAddress();
      miMac.replace("-", ":");

      if (confirmado) {
        // Dispositivo ya registrado - Enviar confirmación con datos
        LoRa.beginPacket();
        LoRa.print("OK_REG," + miMac + ",tinaco solares,180,1100");
        LoRa.endPacket();
        Serial.println("Dispositivo ya registrado - OK_REG enviado con datos se manda nuevamente:");
        Serial.println("OK_REG," + miMac + ",tinaco solares,1200,1100");
      } else {
        // Intentar registrar nuevo dispositivo
        bool registroExitoso = false;
        for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
          if (String(configDispositivos[i].mac) == "") { // Buscar espacio vacío
            mac.toCharArray(configDispositivos[i].mac, MAC_LEN + 1);
            guardarDispositivos(); // Guardar en EEPROM
            registroExitoso = true;
            break;
          }
        }

        if (registroExitoso) {
          // Nuevo registro exitoso - Enviar confirmación con datos
          LoRa.beginPacket();
          LoRa.print("OK_REG," + miMac + ",tinaco solares,180,1100"); //ahortia son valores fijos
          LoRa.endPacket();
          Serial.println("Nuevo dispositivo registrado - OK_REG enviado con datos");
          Serial.println("OK_REG," + miMac + ",tinaco solares,1200,1100"); //ahortia son valores fijos

        } else {
          Serial.println("Error: No se pudo registrar (no hay espacio)");
        }
      }
    } //FIN DE MENSAJE REG 02
//CODIGO PARA DAR DE BAJA
// Procesar mensajes de baja (BAJA02)
    else if (mensaje.startsWith("BAJA02:")) {
      String macBaja = mensaje.substring(7); // Extraer MAC después de "BAJA02:"
      Serial.println("Solicitud de baja recibida para MAC: " + macBaja);

      // Verificar MAC válida
      if (macBaja.length() != MAC_LEN) {
        Serial.println("Error: MAC inválida");
        return;
      }

      bool dispositivoEncontrado = false;
      int posicionBorrar = -1;


//voy a ver si los cargo algo esta pasando      cargarDispositivos()
      // Buscar el dispositivo
      for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
        if (String(configDispositivos[i].mac) == macBaja) {
          dispositivoEncontrado = true;
          posicionBorrar = i;
          break;
        }
      }


      Serial.println("********************** ");
      Serial.println("Buscando mac para baja: "+macBaja);
      //imprimir dispositivos
      imprimirDispositivosRegistrados(); 
      Serial.println("********************** ");


      if (dispositivoEncontrado) {
        // 1. Eliminar el dispositivo
        memset(&configDispositivos[posicionBorrar], 0, sizeof(ConfigDispositivo));
        
        // 2. Reorganizar el array para eliminar huecos
        for (int i = posicionBorrar; i < MAX_DISPOSITIVOS - 1; i++) {
          memcpy(&configDispositivos[i], &configDispositivos[i+1], sizeof(ConfigDispositivo));
        }
        
        // 3. Limpiar la última posición
        memset(&configDispositivos[MAX_DISPOSITIVOS-1], 0, sizeof(ConfigDispositivo));
        
        // 4. Guardar cambios en EEPROM
        guardarDispositivos();
        
        // 5. Enviar confirmación
        LoRa.beginPacket();
        LoRa.print("OK_BAJA");
        LoRa.endPacket();
        
        Serial.println("Dispositivo eliminado y EEPROM reorganizada. OK_BAJA enviado");
        Serial.println("Nuevo listado de dispositivos:");
        imprimirDispositivosRegistrados();
      } else {
        Serial.println("Error: Dispositivo no encontrado");
      }
    }



//FIN DE CODIGO PARA DAR DE BAJA

  }
}



// Función auxiliar para imprimir dispositivos (para debug)
void imprimirDispositivosRegistrados() {
  Serial.println("--- Dispositivos Registrados ---");
  for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
    if (String(configDispositivos[i].mac) != "") {
      Serial.print(i);
      Serial.print(": ");
      Serial.println(configDispositivos[i].mac);
    }
  }
  Serial.println("-----------------------------");
}
    /*
      // Solo procesar si tenemos un userID válido
      if (userID.length() > 0) {
        // Construir el mensaje MQTT
        String topic = "alta/" + String(tipoDispositivo) + "/" + mac  + "/solicitud/";
                // Publicar en MQTT
        if (client.publish(topic.c_str(), userID.c_str())) {
          Serial.println("Solicitud de alta enviada por MQTT");
          
          // Esperar confirmación con timeout
          unsigned long startTime = millis();
          bool confirmado = false;
          
          while (millis() - startTime < 10000 && !confirmado) { // 10 segundos
            client.loop();
            
            // Verificar si ya está registrado
            for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
              if (String(configDispositivos[i].mac) == mac) {
                confirmado = true;
                break;
              }
            }
            delay(100);
          }
          
          if (confirmado) {
            // Enviar confirmación LoRa
            LoRa.beginPacket();
            LoRa.print("OK_REG");
            LoRa.endPacket();
            Serial.println("Confirmación OK_REG enviada");
          } else {
            Serial.println("Timeout: No se recibió confirmación");
          }
        } else {
          Serial.println("Error al publicar solicitud MQTT");
        }
      } else {
        Serial.println("Error: userID no configurado");
      }
*/



// --- Funciones Modificadas ---
bool registrarDispositivo(const String &mac) {
  // Verificar si ya existe
  for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
    if (String(configDispositivos[i].mac) == mac) {
      Serial.println("Dispositivo ya registrado: " + mac);
      return true;
    }
  }
  
  // Buscar espacio libre
  for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
      mac.toCharArray(configDispositivos[i].mac, MAC_LEN + 1); // +1 para '\0'
      configDispositivos[i].activo = false;
      guardarDispositivos();
      return true;
  
  }
  
  Serial.println("¡No hay espacio para más dispositivos!");
  return false;
}


void guardarDispositivos() {
    int addr = LORA_DEVICES_ADDR;
    
    // 1. Contar dispositivos con MAC no vacía
    int count = 0;
    for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
        if (strlen(configDispositivos[i].mac) > 0) {
            count++;
        }
    }

    // 2. Guardar contador (2 bytes)
    EEPROM.write(addr++, (count >> 8) & 0xFF);
    EEPROM.write(addr++, count & 0xFF);

    // 3. Guardar solo las MACs (6 bytes cada una)
    for (int i = 0; i < count; i++) {
        // Convertir MAC "XX:XX:XX:XX:XX:XX" a 6 bytes
        byte macBytes[6];
        sscanf(configDispositivos[i].mac, "%2hhx:%2hhx:%2hhx:%2hhx:%2hhx:%2hhx",
               &macBytes[0], &macBytes[1], &macBytes[2],
               &macBytes[3], &macBytes[4], &macBytes[5]);
        
        // Guardar los 6 bytes
        for (int j = 0; j < 6; j++) {
            EEPROM.write(addr++, macBytes[j]);
        }
    }

    if (!EEPROM.commit()) {
        Serial.println("Error al guardar en EEPROM");
    } else {
        Serial.println("Dispositivos guardados en EEPROM");
    }
}
void cargarDispositivos() {
    int addr = LORA_DEVICES_ADDR;
    
    // 1. Leer contador (2 bytes)
    int count = (EEPROM.read(addr++) << 8) | EEPROM.read(addr++);
    count = min(count, MAX_DISPOSITIVOS);  // Prevenir corrupción

    Serial.print("Obteniendo Dispositivos registrados en EEPROM: ");
    Serial.println(count);

    // 3. Bandera para saber si encontramos el dispositivo actual
    bool dispositivoEncontrado = false;

    // 4. Cargar MACs registradas
    for (int i = 0; i < count; i++) {
        char mac[18] = {0}; // Formato MAC: "XX:XX:XX:XX:XX:XX" + null terminator
        
        // Leer MAC de EEPROM
        for (int j = 0; j < 6; j++) {
            byte b = EEPROM.read(addr++);
            sprintf(mac + j*3, "%02X", b);
            if (j < 5) mac[j*3 + 2] = ':';
        }

        Serial.print("[DEBUG] Dispositivo ");
        Serial.print(i);
        Serial.print(": ");
        Serial.println(mac);

        // Verificar si es el dispositivo actual
        #ifdef macAddress
        if (strcmp(mac, macAddress) == 0) {
            Serial.println("[DEBUG] ¡Este dispositivo YA está registrado!");
            registrado = true;
            dispositivoEncontrado = true;
        }
        #endif

        // Copiar MAC al arreglo de dispositivos (siempre se hace para mantener estructura)
        strncpy(configDispositivos[i].mac, mac, sizeof(configDispositivos[i].mac));
        configDispositivos[i].activo = false;
    }

    // 5. Mensaje final diferenciado
    if (dispositivoEncontrado) {
        Serial.println("[DEBUG] Fin de carga (dispositivo ya registrado)");
    } else {
        Serial.println("[DEBUG] Fin de carga (dispositivo nuevo)");
    }
}

// --- Función para imprimir datos ---
void imprimirDatos(const String &mac, int contador, float voltaje, float temperatura) {
  Serial.print("Datos de ");
  Serial.print(mac);
  Serial.print(" | Contador: ");
  Serial.print(contador);
  Serial.print(" | Voltaje: ");
  Serial.print(voltaje, 2);
  Serial.print("V | Temp: ");
  Serial.print(temperatura, 2);
  Serial.println("°C");
}





//Funciones SETUP de innicializacion
   // 2. Inicialización segura de EEPROM
void inicializa_eeprom(){
  if (!EEPROM.begin(EEPROM_SIZE)) {
    Serial.println("Error al inicializar EEPROM");
    while (1) {
      // Patrón de error en LED (3 parpadeos rápidos, pausa)
      for (int i = 0; i < 3; i++) {
       // digitalWrite(LED_WIFFI, HIGH);
        delay(100);
       // digitalWrite(LED_WIFFI, LOW);
        delay(100);
      }
      delay(1000);
    }
  }
 
}

//Limpieza de fabrica EEPROM y WIFFI
void clearEEPROM_WIFFI() {
  for (int i = 0; i < EEPROM_SIZE; i++) {
    EEPROM.write(i, 0);
  }
  EEPROM.commit();
  
  Serial.println("Configuración inicial - Modo fábrica");
    for (int i = 0; i < MAX_NETWORKS; i++) {
      savedNetworks[i] = {"", "", false};
    }

    saveNetworksToEEPROM();
    Serial.println("Reestablece correctamente EEPROM y Redes Wiffi....");
    Serial.println("Modo AP activado (configuración inicial)");
}

//Funcion para alta de monitor
bool isNetworkReady() {
  // Verificar conexión WiFi
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi no conectado");
    return false;
  }
  
  // Verificar conexión MQTT
  if (!client.connected()) {
    Serial.println("MQTT no conectado");
    return false;
  }
  
  return true;
}

void MQTT_ALTA() {
  if (mqttConfirmed) return;
  
  if (WiFi.status() == WL_CONNECTED && client.connected()) {
    if (millis() - lastConfirmationAttempt > confirmationRetryInterval) {
      lastConfirmationAttempt = millis();
      
      if (userID.length() > 0) {
        Serial.println("MQTT ALTA Existe USUARIO ID Enviando solicitud de alta INICIAL...");  
        // Versión simplificada usando el método publish() que acepta const char*
              // Obtener la MAC del dispositivo que está respondiendo (este módulo)
      String miMac = WiFi.macAddress();
      miMac.replace("-", ":");
      // Crear el mensaje completo como String primero
String mensajeCompleto = miMac + "," + userID;
        if (client.publish("alta/1/solicitud/",  mensajeCompleto.c_str())) {
          Serial.println("MQTT ALTA Solicitud de alta enviada correctamente");
          Serial.println("MQTT ALTA topico: alta/1/solicitud/");
          Serial.println("MQTT ALTA mensaje: "+mensajeCompleto);




        } else {
          Serial.println("MQTT ALTA Error al publicar solicitud de alta");
        }
      }
    }
  }
}

void saveMQTTConfirmationState(bool confirmed) {
  EEPROM.write(MQTT_CONFIRMED_FLAG_ADDR, confirmed ? 1 : 0);
  EEPROM.commit();
}

bool loadMQTTConfirmationState() {
  return EEPROM.read(MQTT_CONFIRMED_FLAG_ADDR) == 1;
}


//Funciones para dar de alta a Broker cualquier tipo de dispositivo LORA mediante MQTT 
void guardarConfigDispositivos() {
  int addr = CONFIG_DISPOSITIVOS_ADDR;
  
  // Contar dispositivos configurados
  int count = 0;
  while (count < MAX_DISPOSITIVOS && configDispositivos[count].mac[0] != '\0') {
    count++;
  }
  
  // Guardar contador (2 bytes)
  EEPROM.write(addr++, (count >> 8) & 0xFF);
  EEPROM.write(addr++, count & 0xFF);
  
  // Guardar configuraciones
  for (int i = 0; i < count; i++) {
    EEPROM.put(addr, configDispositivos[i]);
    addr += sizeof(ConfigDispositivo);
  }
  
  EEPROM.commit();
}

void cargarConfigDispositivos() {
  int addr = CONFIG_DISPOSITIVOS_ADDR;
  
  // Leer contador
  int count = (EEPROM.read(addr++) << 8) | EEPROM.read(addr++);
  count = min(count, MAX_DISPOSITIVOS);
  
  // Inicializar todos como vacíos
  for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
    configDispositivos[i].mac[0] = '\0';
    for (int j = 0; j < VALORES_POR_DISPOSITIVO; j++) {
      configDispositivos[i].valores[j] = 0.0;
    }
    configDispositivos[i].tipoDispositivo = 0;
  }
  
  // Cargar configuraciones
  for (int i = 0; i < count; i++) {
    EEPROM.get(addr, configDispositivos[i]);
    addr += sizeof(ConfigDispositivo);
  }
}


// Obtener configuración de un dispositivo por MAC
ConfigDispositivo* getConfigDispositivo(const String &mac) {
  for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
    if (String(configDispositivos[i].mac) == mac) {
      return &configDispositivos[i];
    }
  }
  return nullptr;
}

// Imprimir configuración de un dispositivo
void imprimirConfigDispositivo(const String &mac) {
  ConfigDispositivo* config = getConfigDispositivo(mac);
  if (config) {
    Serial.printf("Configuración para MAC %s:\n", mac.c_str());
    Serial.printf("Tipo: %d\n", config->tipoDispositivo);
    for (int i = 0; i < VALORES_POR_DISPOSITIVO; i++) {
      if (config->valores[i] != 0.0) {
        Serial.printf("Valor%d: %.2f\n", i + 1, config->valores[i]);
      }
    }
  } else {
    Serial.println("Dispositivo no encontrado");
  }
}

// Reiniciar configuración de un dispositivo
void resetConfigDispositivo(const String &mac) {
  for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
    if (String(configDispositivos[i].mac) == mac) {
      for (int j = 0; j < VALORES_POR_DISPOSITIVO; j++) {
        configDispositivos[i].valores[j] = 0.0;
      }
      guardarConfigDispositivos();
      Serial.println("Configuración reiniciada para MAC: " + mac);
      return;
    }
  }
  Serial.println("Dispositivo no encontrado");
}



void iniciarLoRaConReintentos() {
  int intentos = 0;
  bool estadoLED = false;

Serial.println("Iniciando LoRa...");
    loraSPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
    LoRa.setSPI(loraSPI);
    LoRa.setPins(LORA_SS, -1, -1);

  while (!LoRa.begin(433E6)) {
    Serial.println("Error al iniciar LoRa. Reintentando...");

    // Verifica manualmente SPI
    Serial.println("SPI test: " + String(SPI.transfer(0x42), HEX));

    // Reset LoRa
//    pinMode(LORA_RST, OUTPUT);
 //   digitalWrite(LORA_RST, LOW);
 //   delay(100);
 //   digitalWrite(LORA_RST, HIGH);
 //   delay(100);

    estadoLED = !estadoLED;
    intentos++;
    Serial.println("Intento #" + String(intentos));
    delay(3000);
  }

  //digitalWrite(LED_WIFFI, LOW);
  Serial.println("LoRa listo después de " + String(intentos) + " intento(s)!");
}

void Reintentar_Wiffi(){
    // Reintentar conexión periódicamente
    if (millis() - lastReconnectAttempt > reconnectInterval) {
      Serial.println("Reintentando conexión a redes guardadas...");
      lastReconnectAttempt = millis();
      attemptReconnectToAllNetworks();
    }
}


/******FUNCIONES PANTALLA   */


// ======== CALIBRACIÓN SILENCIOSA (SOLO SERIAL) ========
void calibrarSilencioso() {

  
  /*
  
>>> Toque 5 veces el botón W (centro) <<<
Lectura 1: Raw[2749,1489] -> Mapeado[234,158] | Objetivo[90,90]
Lectura 2: Raw[2648,1489] -> Mapeado[224,158] | Objetivo[90,90]
Lectura 3: Raw[2660,1494] -> Mapeado[225,159] | Objetivo[90,90]
Lectura 4: Raw[2609,1473] -> Mapeado[220,156] | Objetivo[90,90]
Lectura 5: Raw[2742,1438] -> Mapeado[233,150] | Objetivo[90,90]

=== RESULTADOS ===
Botón W:
- Raw promedio: X=2681, Y=1476
- Mapeado promedio: X=227, Y=156
- Error: X=137px, Y=66px
--------------------------------------

>>> Toque 5 veces el botón S (centro) <<<
Lectura 1: Raw[1456,1722] -> Mapeado[103,194] | Objetivo[210,90]
Lectura 2: Raw[1438,1707] -> Mapeado[101,192] | Objetivo[210,90]
Lectura 3: Raw[1282,1529] -> Mapeado[85,164] | Objetivo[210,90]
Lectura 4: Raw[1251,1557] -> Mapeado[82,169] | Objetivo[210,90]
Lectura 5: Raw[1380,1664] -> Mapeado[95,185] | Objetivo[210,90]

=== RESULTADOS ===
Botón S:
- Raw promedio: X=1361, Y=1635
- Mapeado promedio: X=93, Y=181
- Error: X=117px, Y=91px
--------------------------------------
  */
  Serial.println("\n\n=== MODO CALIBRACIÓN PRECISO ===");
  Serial.println("Toque 5 veces el CENTRO de cada botón");
  Serial.println("Siga el orden indicado en Serial Monitor");
  Serial.println("--------------------------------------");

  // Configuración inicial con tus valores
  const int calibValues[2][2] = {{436, 3580}, {470, 3549}}; // {{X_MIN, X_MAX}, {Y_MIN, Y_MAX}}

  // Coordenadas esperadas de los botones (píxeles)
  const int btnCoords[2][2] = {
    {btnW_x + btn_width/2, btn_y + btn_height/2},  // Centro botón W
    {btnS_x + btn_width/2, btn_y + btn_height/2}   // Centro botón S
  };

  // Diagnóstico para ambos botones
  for(int boton = 0; boton < 2; boton++) {
    Serial.printf("\n>>> Toque 5 veces el botón %c (centro) <<<\n", (boton == 0) ? 'W' : 'S');
    
    int totalX = 0, totalY = 0;
    for(int i = 1; i <= 5; i++) {
      while(!touch.touched()); // Espera toque
      
      TS_Point p = touch.getPoint();
      totalX += p.x;
      totalY += p.y;
      
      // Mapeo en tiempo real con tus constantes
      int x_mapped = map(p.x, calibValues[0][0], calibValues[0][1], 0, lcd.width()-1);
      int y_mapped = map(p.y, calibValues[1][0], calibValues[1][1], 0, lcd.height()-1);
      
      Serial.printf("Lectura %d: Raw[%d,%d] -> Mapeado[%d,%d] | Objetivo[%d,%d]\n", 
                   i, p.x, p.y, x_mapped, y_mapped, 
                   btnCoords[boton][0], btnCoords[boton][1]);

      // Parpadeo del botón actual
      int btnX = (boton == 0) ? btnW_x : btnS_x;
      lcd.drawRect(btnX, btn_y, btn_width, btn_height, TFT_RED);
      delay(150);
      dibujarBotones();
      
      while(touch.touched()); // Espera dejar de tocar
      delay(250);
    }

    // Resultados por botón
    int avgX = totalX / 5;
    int avgY = totalY / 5;
    int avgX_mapped = map(avgX, calibValues[0][0], calibValues[0][1], 0, lcd.width()-1);
    int avgY_mapped = map(avgY, calibValues[1][0], calibValues[1][1], 0, lcd.height()-1);
    
    Serial.println("\n=== RESULTADOS ===");
    Serial.printf("Botón %c:\n", (boton == 0) ? 'W' : 'S');
    Serial.printf("- Raw promedio: X=%d, Y=%d\n", avgX, avgY);
    Serial.printf("- Mapeado promedio: X=%d, Y=%d\n", avgX_mapped, avgY_mapped);
    Serial.printf("- Error: X=%dpx, Y=%dpx\n", 
                 abs(avgX_mapped - btnCoords[boton][0]), 
                 abs(avgY_mapped - btnCoords[boton][1]));
    Serial.println("--------------------------------------");
  }

  // Restaura pantalla
  dibujarBotones();
  Serial.println("\nCalibración completada. Revise los errores de precisión.");
}

// ==================== IMPLEMENTACIÓN DE FUNCIONES ====================
void hardwareReset() {
    pinMode(EN, OUTPUT);
    digitalWrite(EN, LOW);
    delay(50);
    digitalWrite(EN, HIGH);
    delay(150);
}


void dibujarIconoWiFi(int x, int y) {
    lcd.fillCircle(x, y, 3, boton_w ? TFT_WHITE : TFT_YELLOW);
    lcd.drawArc(x, y, 10, 12, 0, 180, boton_w ? TFT_WHITE : TFT_YELLOW);
    lcd.drawArc(x, y, 15, 17, 0, 180, boton_w ? TFT_WHITE : TFT_YELLOW);
}


void dibujarIconoSensor(int x, int y) {
    lcd.fillCircle(x, y, 8, boton_s ? TFT_WHITE : TFT_YELLOW);
    for(int i=0; i<8; i++) {
        float angle = i * PI/4;
        lcd.drawLine(
            x + cos(angle)*10,
            y + sin(angle)*10,
            x + cos(angle)*15,
            y + sin(angle)*15,
            boton_s ? TFT_WHITE : TFT_YELLOW
        );
    }
}


void dibujarBotones() {
    lcd.fillScreen(TFT_BLACK);
    
    // Botón W (WiFi)
    lcd.drawRect(btnW_x, btn_y, btn_width, btn_height, boton_w ? TFT_WHITE : TFT_YELLOW);
    lcd.setTextColor(boton_w ? TFT_WHITE : TFT_YELLOW, TFT_BLACK);
    lcd.setTextSize(2);
    lcd.drawCenterString("W", btnW_x + btn_width/2, btn_y + btn_height/2 - 10);
    dibujarIconoWiFi(btnW_x + btn_width/2, btn_y + 20);

    // Botón S (Sensor)
    lcd.drawRect(btnS_x, btn_y, btn_width, btn_height, boton_s ? TFT_WHITE : TFT_YELLOW);
    lcd.setTextColor(boton_s ? TFT_WHITE : TFT_YELLOW, TFT_BLACK);
    lcd.drawCenterString("S", btnS_x + btn_width/2, btn_y + btn_height/2 - 10);
    dibujarIconoSensor(btnS_x + btn_width/2, btn_y + 20);

    // Texto de estado
    lcd.setTextSize(1);
    lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
    lcd.drawCenterString("Estado:", 160, 150);
    lcd.drawCenterString(boton_w ? "W: ON " : "W: OFF", 100, 170);
    lcd.drawCenterString(boton_s ? "S: ON " : "S: OFF", 220, 170);
}

void verificarToques() {
    if(touch.touched()) {
        TS_Point p = touch.getPoint();
        
        // 1. Verificación directa con valores RAW (sin mapeo completo)
        tocado_w = (p.x >= 2500 && p.x <= 3000 &&  // Rango X para botón W (ajustar)
                        p.y >= 1400 && p.y <= 1600);    // Rango Y para botón W
        
        tocado_s = (p.x >= 1200 && p.x <= 1700 &&  // Rango X para botón S
                        p.y >= 1500 && p.y <= 1800);    // Rango Y para botón S

        // 2. Detección con feedback inmediato
        if(tocado_w && !boton_w) {
            boton_w = true;
            Serial.println("[EVENTO] Botón W presionado (RAW)");
            Serial.printf("Coordenadas: X=%d, Y=%d\n", p.x, p.y);
            dibujarBotones();
        }
        
        if(tocado_s && !boton_s) {
            boton_s = true;
            Serial.println("[EVENTO] Botón S presionado (RAW)");
            Serial.printf("Coordenadas: X=%d, Y=%d\n", p.x, p.y);
            dibujarBotones();
        }
    } 
    else {
        // Solo envía mensaje si había un botón presionado
        if(boton_w || boton_s) {
            Serial.println("[EVENTO] Todos los botones liberados");
            boton_w = false;
            boton_s = false;
            dibujarBotones();
        }
    }
}

void calibrarBordes() {
  /*
  
=== VALORES PARA TU CODIGO ===
// Reemplaza en tus constantes:
const int X_MIN = 436;  // Esq. Sup. Izq. X
const int X_MAX = 3580;  // Esq. Sup. Der. X
const int Y_MIN = 470;  // Esq. Sup. Der. Y
const int Y_MAX = 3549;  // Esq. Inf. Izq. Y
=============================
  */
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextColor(TFT_WHITE);
  lcd.setTextSize(2);
  lcd.drawString("Calibracion Bordes", 20, 20);
  lcd.drawString("Toque las 4 esquinas", 20, 50);
  lcd.drawString("en este orden:", 20, 80);
  lcd.drawString("1. Superior Izq", 20, 110);
  lcd.drawString("2. Superior Der", 20, 140);
  lcd.drawString("3. Inferior Der", 20, 170);
  lcd.drawString("4. Inferior Izq", 20, 200);

  int puntos[4][2]; // Almacenar [X,Y] de cada esquina
  const char* nombres[4] = {"Superior Izq", "Superior Der", "Inferior Der", "Inferior Izq"};

  for(int i = 0; i < 4; i++) {
    lcd.fillRect(0, 230, 320, 30, TFT_BLACK);
    lcd.drawString("Toque: " + String(nombres[i]), 20, 230);
    
    while(!touch.touched()); // Espera toque
    TS_Point p = touch.getPoint();
    puntos[i][0] = p.x;
    puntos[i][1] = p.y;
    
    Serial.printf("Esquina %d (%s): X=%d, Y=%d\n", i+1, nombres[i], p.x, p.y);
    lcd.fillRect(0, 260, 320, 30, TFT_BLACK);
    lcd.drawString("X:" + String(p.x) + " Y:" + String(p.y), 20, 260);
    
    delay(500);
    while(touch.touched()); // Espera dejar de tocar
  }

  // Resultados finales
  Serial.println("\n=== VALORES PARA TU CODIGO ===");
  Serial.println("// Reemplaza en tus constantes:");
  Serial.printf("const int X_MIN = %d;  // Esq. Sup. Izq. X\n", puntos[0][0]);
  Serial.printf("const int X_MAX = %d;  // Esq. Sup. Der. X\n", puntos[1][0]);
  Serial.printf("const int Y_MIN = %d;  // Esq. Sup. Der. Y\n", puntos[1][1]);
  Serial.printf("const int Y_MAX = %d;  // Esq. Inf. Izq. Y\n", puntos[3][1]);
  Serial.println("=============================");

  while(1); // Detiene ejecución después de calibrar
}
/*******************FIN FUNCIONES PANTALLA */