#include <SPI.h>

//AP
#include <Arduino.h>
#include <WebServer.h>
#include <DNSServer.h>

#include <LoRa.h>
#include <EEPROM.h>

//Pantalla TFT
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

//Wiffi
#include <WiFi.h>
#include "esp_wifi.h"  // Necesario para usar esp_wifi_set_mac()

//MQTT
#include <PubSubClient.h>


//EEPROM  Tamaño EEPROM (ESP32 tiene 4KB)
// Definir direcciones para nombre y email (después de tus otras configuraciones) 
#define USER_NAME_ADDR 3000    // 
#define USER_EMAIL_ADDR 3500 //
#define EEPROM_SIZE 4096              //
#define ALIAS_DISPOSITIVOS 2000       // 

#define MQTT_CONFIRMED_FLAG_ADDR 350  //
#define USER_ID_ADDR 400              // 
#define LORA_DEVICES_ADDR 500         //
#define CONFIG_DISPOSITIVOS_ADDR 700  // 

// Configuración WiFi
#define AP_SSID "NUUP_2025"// que permita el acceso directo finalmente no puede hacer nada hasta no ingresar un ID de usuario correcto "nuup"
#define AP_PASS "12345678"
#define WIFI_TIMEOUT 5000 // 30 segundos
#define USER_ID_MAX_LEN 32    // Máximo 32 caracteres para el ID lo puedo cambiar si solo necesito el users.users_id concatenado a la clave NUUP2025

// --- Nueva Configuración para Dispositivos LoRa ---
#define MAX_DISPOSITIVOS 50         // Máximo de dispositivos registrables
#define MAC_LEN 17                  // Longitud de MAC (ej: "A0:B1:C2:D3:E4:F5")
#define VALORES_POR_DISPOSITIVO 5    // Máximo de valores por dispositivo


//Redes guardadas
#define MAX_NETWORKS 3
#define SSID_LEN 32
#define PASS_LEN 64
#define NETWORK_SIZE (SSID_LEN + PASS_LEN + 2) // +2 para los bytes de longitud

// Pines LoRa
#define LORA_SS   5
#define LORA_SCK  18
#define LORA_MISO 19
#define LORA_MOSI 23


//*****************************
//***   LORA ***
//*****************************
// Variable global para almacenar el último mensaje LoRa recibido
SPIClass loraSPI(HSPI);  // Segundo SPI para LoRa

volatile bool nuevoMensajeLoRa = false;
String mensajeLoRa = "";

typedef struct {
  char mac[MAC_LEN + 1];  // MAC del dispositivo (18 caracteres)
  float valores[VALORES_POR_DISPOSITIVO]; // Valores genéricos
  byte tipoDispositivo;    // Tipo de dispositivo (1-255)
  bool activo;   //voy a usar este para controlar MQTT
} ConfigDispositivo;
ConfigDispositivo configDispositivos[MAX_DISPOSITIVOS];
//para asignar un numero de serial al dispositivo lector este viene del LORA pero ahorita lo fijmos aqui
const String serial_number = "TOPICMYSQL";  //aqui voy a maper la MAC


//*****************************
//***   CONFIGURACION MQTT  ***
//*****************************
const char *mqtt_server ="168.231.66.42";//
const int  mqtt_port =2783; //Es docker
const char *mqtt_user="nuup_web";
const char *mqtt_pass ="Kfl-0878";
WiFiClient espClient;
PubSubClient client(espClient);

long lastMsg = 0;

//*****************************
//***   ALTA MQTT DE MONITOR ***
//*****************************
bool mqttConfirmed = false;          // Bandera de confirmación MQTT
unsigned long lastConfirmationAttempt = 0;
const unsigned long confirmationTimeout = 30000; // 30 segundos para esperar confirmación
const unsigned long confirmationRetryInterval = 10000; // segundos entre reintentos de conexion MQTT
String userID = "";


// Estructura para almacenar credenciales WIFFI
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

// Declaración de funciones
void inicializa_eeprom();
void iniciarLoRaConReintentos();
void clearEEPROM_WIFFI();
void clearMQTTConfirmationFlag();
void startAPMode();
void handleRoot();
void handleSaveCredentials();
void handleDeleteNetwork();
void handleSelectNetwork();
void handleDeleteDevice();
void saveNetworksToEEPROM();
bool loadNetworksFromEEPROM();
void attemptReconnectToAllNetworks();
void handleSetID();
void saveUserIDToEEPROM(const String& id);
bool loadUserIDFromEEPROM();
void callback(char* topic, byte* playload, unsigned int lengt);
void reconnect();
void  checkMemory();
bool guardarDispositivos();
void cargarDispositivos();
bool eliminarDispositivo(const String &mac);


bool registrarDispositivo(const String &mac);
void MQTT_ALTA();
void notificarBajaMQTT(const String &mac);
bool loadMQTTConfirmationState();
void procesarMensajeLoRa();
void imprimirConfigDispositivo(const String &mac);
void imprimirDispositivosRegistrados();
void Reintentar_Wiffi();


void recepcion_lora();

//temporal para forzar apmode una vez si no hay wiffi
bool una_APmode=true;


//Definiciones pantalla TFT
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Estructura para los dispositivos
struct Dispositivo {
  String nombre;
  int porcentaje;
  float bateria; // en volts
  int litros;
};

// Variables de estado
bool wifiConectado = false;

// Datos de ejemplo
Dispositivo dispositivos[] = {
  {"Solares 1", 80, 3.3, 1500},
  {"Villas 1", 60, 3.1, 6500},
  {"Principal", 45, 2.9, 3200}
};

int dispositivoActual = 0;
unsigned long ultimoCambio = 0;
const unsigned long INTERVALO_CAMBIO = 3000; // 3 segundos

// Variables para animación de emparejamiento
bool emparejando = false;
int frameAnimacion = 0;
unsigned long ultimoCambioAnimacion = 0;
const unsigned long INTERVALO_ANIMACION = 200; // ms entre frames

// Bitmaps para iconos (16x16 pixels)
const unsigned char PROGMEM wifiIcon[] = {
  0b00000011, 0b11000000,
  0b00001100, 0b00110000,
  0b00110000, 0b00001100,
  0b01000000, 0b00000010,
  0b00000000, 0b00000000,
  0b00000011, 0b11000000,
  0b00001100, 0b00110000,
  0b00000000, 0b00000000,
  0b00000000, 0b00000000,
  0b00000000, 0b11000000,
  0b00000000, 0b00000000,
  0b00000000, 0b00000000,
  0b00000000, 0b00000000,
  0b00000000, 0b00000000,
  0b00000000, 0b00000000,
  0b00000000, 0b00000000
};

const unsigned char PROGMEM wifiIconOff[] = {
  0b10000011, 0b11000001,  // Tache + WiFi
  0b11001100, 0b00110011,  // Tache + WiFi
  0b01110000, 0b00001110,  // Tache + WiFi
  0b00110000, 0b00001100,  // Tache + WiFi
  0b00011000, 0b00011000,  // Tache + WiFi
  0b00001111, 0b11110000,  // Tache + WiFi
  0b00000111, 0b11100000,  // Tache + WiFi
  0b00000011, 0b11000000,  // Tache + WiFi
  0b00000011, 0b11000000,  // Tache + WiFi
  0b00000111, 0b11100000,  // Tache + WiFi
  0b00001111, 0b11110000,  // Tache + WiFi
  0b00011000, 0b00011000,  // Tache + WiFi
  0b00110000, 0b00001100,  // Tache + WiFi
  0b01110000, 0b00001110,  // Tache + WiFi
  0b11001100, 0b00110011,  // Tache + WiFi
  0b10000011, 0b11000001   // Tache + WiFi
};

const unsigned char PROGMEM batteryFull[] = {
  0b00111111, 0b11111100,
  0b01100000, 0b00000110,
  0b11000000, 0b00000011,
  0b10011111, 0b11111001,
  0b10011111, 0b11111001,
  0b10011111, 0b11111001,
  0b10011111, 0b11111001,
  0b10011111, 0b11111001,
  0b10011111, 0b11111001,
  0b10011111, 0b11111001,
  0b10011111, 0b11111001,
  0b10011111, 0b11111001,
  0b10011111, 0b11111001,
  0b11000000, 0b00000011,
  0b01100000, 0b00000110,
  0b00111111, 0b11111100
};

const unsigned char PROGMEM batteryEmpty[] = {
  0b00111111, 0b11111100,
  0b01100000, 0b00000110,
  0b11000000, 0b00000011,
  0b10000000, 0b00000001,
  0b10000000, 0b00000001,
  0b10000000, 0b00000001,
  0b10000000, 0b00000001,
  0b10000000, 0b00000001,
  0b10000000, 0b00000001,
  0b10000000, 0b00000001,
  0b10000000, 0b00000001,
  0b10000000, 0b00000001,
  0b10000000, 0b00000001,
  0b11000000, 0b00000011,
  0b01100000, 0b00000110,
  0b00111111, 0b11111100
};


// Variables para animación de WiFi en TFT
bool animandoWifi = false;
int frameWifi = 0;
unsigned long ultimoCambioWifi = 0;
const unsigned long INTERVALO_WIFI = 300; // ms entre frames

// DECLARACIONES DE FUNCIONES TFT
void dibujarHeader();
void dibujarTituloDispositivo(Dispositivo disp);
void dibujarContenidoPrincipal(Dispositivo disp);
void dibujarCirculoGiratorio(int centroX, int centroY, int radio, int angulo);
void mostrarEmparejamiento();
void iniciarEmparejamiento();
void detenerEmparejamiento();
void emparejarNuevoDispositivo();
void actualizarDatos(int index, int porcentaje, float bateria, int litros);
void setWifiStatus(bool conectado);

// También agregar estas declaraciones al principio TFT
void dibujarWifiAnimado(int centroX, int centroY, int frame);
void mostrarConexionWifi();
void iniciarAnimacionWifi();
void detenerAnimacionWifi();
void conectarWifi();



// --- Pines para los botones---
#define BOTON_S 33  
#define BOTON_W 4  
#define TIEMPO_BOTON 3000

bool boton_s=false;
bool boton_w=false;
unsigned long tiempoInicioPresion = 0;

void manejarBoton_S();
void manejarBoton_W();



void setup() {
  
pinMode(BOTON_S, INPUT_PULLUP);
pinMode(BOTON_W, INPUT_PULLUP);

  // 0. Inicialización básica SERIAL PANTLALLA  EEPROM
  Serial.begin(115200);
    delay(1000);

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

 //7. configura DISPOSITIVOS
// Cargar dispositivos registrados
cargarDispositivos();  // 
delay(1000);

//Wiffi 
 //8. attemptReconnectToAllNetworks();  
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

  //Configuracion TFT   
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("Fallo inicializacion OLED");
    while(true);
  }
  
  Serial.println("OLED inicializado correctamente");
  display.setTextColor(SSD1306_WHITE);


}



// Modificar el loop principal para manejar ambas animaciones
void loop() {

 manejarBoton_S();
 manejarBoton_W();
// 0. Recepcion de LORA 
if (!boton_s){ recepcion_lora();} //Solo se procesa si el oton de emparejamiento no esta activo 



// 1. Comportamiento en recepcion continua 
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


// 1.2 En caso de que tenga WIFFI conectado a MQTT y este dado de alta envia el dato
if (WiFi.status() == WL_CONNECTED && client.connected() && mqttConfirmed && !forceAPMode) {  
  // Publicar inmediatamente si hay datos y estamos conectados
  if (nuevoMensajeLoRa) {
    String miMac = WiFi.macAddress();
    miMac.replace(":", "_");
    String topico = "NUUP/" + miMac;
    if (client.publish(topico.c_str(), mensajeLoRa.c_str())) {
      Serial.print("Publicado: ");
      Serial.println(mensajeLoRa);
    } else {
      Serial.println("Error al publicar");
    }
    nuevoMensajeLoRa=false; //solo publicar una vez el mensaje y esperar a otro nuevo
  }
} 

  // 3. Manejo básico de conexiones
if (!client.connected() && WiFi.status() == WL_CONNECTED && !forceAPMode) {
    reconnect();  //Solo para reconectar y configuracion de subscripciones
  }
 client.loop();

 if ( client.connected() && WiFi.status() == WL_CONNECTED && !forceAPMode)  {
 MQTT_ALTA();  //Para solicitar el alta en broker
  }

  
  //4. Variables para el control de memoria (declaradas una sola vez)
  static unsigned long lastMemoryCheck = 0;
  const unsigned long memoryCheckInterval = 30000; // 30 segundos en milisegundos

  //5. Verificación periódica de memoria (solo para debug)
   if (millis() - lastMemoryCheck > memoryCheckInterval) {
    lastMemoryCheck = millis();
    checkMemory();
  }

   

//8. Si no hay Wiffi 
 if (WiFi.status() != WL_CONNECTED && !forceAPMode) {
wifiConectado=false;
  Reintentar_Wiffi();
  }else{
    wifiConectado=true;
  }

  if(!forceAPMode){
    // Código normal de la pantalla presentando dispositivos
    display.clearDisplay();
    
    dibujarHeader();
    dibujarTituloDispositivo(dispositivos[dispositivoActual]);
    dibujarContenidoPrincipal(dispositivos[dispositivoActual]);
    
    if (millis() - ultimoCambio >= INTERVALO_CAMBIO) {
      dispositivoActual = (dispositivoActual + 1) % (sizeof(dispositivos) / sizeof(dispositivos[0]));
      ultimoCambio = millis();
    }
    
    display.display();
    delay(100);
  
  }


}


//validar si esta presionando boton S o W

void manejarBoton_S() {
  tiempoInicioPresion = millis();
  while(!digitalRead(BOTON_S)){
  if (millis() - tiempoInicioPresion >= TIEMPO_BOTON) {
    Serial.println("Presionando boton Sincronizacion de dispositivo..");

    // Animación de emparejamiento de dispositivo
    if (millis() - ultimoCambioAnimacion >= INTERVALO_ANIMACION) {
      frameAnimacion++;
      ultimoCambioAnimacion = millis();
    }
    mostrarEmparejamiento();
  
    
    
      boton_s=true;
      ///dispositivos
//6. Manejo del botón S para registro de dispositivo o emparejamiento
if (boton_s){ 
    Serial.println("Modo recepción activado. Esperando solicitudes REG o BAJA...");
        // Solo procesar mensajes (el OK_REG se envía desde procesarMensajeLoRa)
        procesarMensajeLoRa();
      }
    }
 }

if(boton_s){
 boton_s=false;
   detenerEmparejamiento();
   //imprimir una pantalla aqui con los datos del dispositivo emparejado
   delay(3000);
   }



}


void manejarBoton_W() {
  tiempoInicioPresion = millis();
  while(!digitalRead(BOTON_W)){
  if (millis() - tiempoInicioPresion >= TIEMPO_BOTON) {
  Serial.println("Presionando boton Wiffi  dispositivo..");  // Para detener después de 10 segundos (en producción lo controlarías con el estado real del WiFi)

    // Animación de conexión WiFi
    if (millis() - ultimoCambioWifi >= INTERVALO_WIFI) {
      frameWifi++;
      ultimoCambioWifi = millis();
      
      // Reiniciar animación cuando llega al final
      if (frameWifi > 3) {
        frameWifi = 0;
      }
    }
    mostrarConexionWifi();
 boton_w=true;
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

    
 }

if(boton_w){
 boton_w=false;
  detenerAnimacionWifi();
  delay(3000);}


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
  server.on("/delete-device", HTTP_POST, handleDeleteDevice);
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

  String devicesList = "";
  int deviceCount = 0;

  for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
    String mac = String(configDispositivos[i].mac);
    if (mac.length() > 0) {
      deviceCount++;
      devicesList += "<div class='device-item'>";
      devicesList += "<div><strong>MAC:</strong> " + mac + "</div>";
      devicesList += "<button type='button' onclick=\"deleteDevice('" + mac + "')\">Eliminar</button>";
      devicesList += "</div>";
    }
  }

  if (deviceCount == 0) {
    devicesList = "<p class='empty-state'>No hay dispositivos registrados en este monitor.</p>";
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
    .device-list {
      margin-top: 20px;
    }
    .device-list h3 {
      margin-bottom: 10px;
    }
    .device-item {
      display: flex;
      align-items: center;
      justify-content: space-between;
      margin: 10px 0;
      padding: 10px;
      background-color: #333;
      border-radius: 5px;
    }
    .empty-state {
      color: #aaa;
      text-align: center;
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

    function deleteDevice(mac) {
      if (confirm('¿Borrar el dispositivo ' + mac + '?')) {
        fetch('/delete-device', {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
          body: 'mac=' + encodeURIComponent(mac)
        }).then(response => {
          if (response.ok) {
            location.reload();
          }
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

    <div class="device-list">
      <h3>Dispositivos registrados:</h3>
      )=====" + devicesList + R"=====(
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
      clearMQTTConfirmationFlag();
      server.send(200, "text/plain", "OK");
    } else {
      server.send(400, "text/plain", "Índice inválido");
    }
  } else {
    server.send(400, "text/plain", "Falta parámetro index");
  }
}

void handleDeleteDevice() {
  if (!server.hasArg("mac")) {
    server.send(400, "text/plain", "Falta parámetro mac");
    return;
  }

  String mac = server.arg("mac");
  mac.trim();

  if (mac.length() != MAC_LEN) {
    server.send(400, "text/plain", "MAC inválida");
    return;
  }

  bool eliminado = eliminarDispositivo(mac);

  if (eliminado) {
    notificarBajaMQTT(mac);
    server.send(200, "text/plain", "Dispositivo eliminado");
  } else {
    server.send(404, "text/plain", "Dispositivo no encontrado");
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

            EEPROM.write(MQTT_CONFIRMED_FLAG_ADDR, 1);

            EEPROM.commit();
            EEPROM.end();

            Serial.println("CONFIRMACION RECIBIDA - Alta validada correctamente");
            Serial.println("Nombre guardado: " + nombreUsuario);
            Serial.println("Email guardado: " + emailUsuario);

            mqttConfirmed = true;
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
    } //FIN DE MENSAJE REG 
//CODIGO PARA DAR DE BAJA
// Procesar mensajes de baja (BAJA02)
   else if (mensaje.startsWith("BAJA")) {
    String macBaja = mensaje.substring(7);
    Serial.println("\nSolicitud de baja recibida para MAC: " + macBaja);

    // Verificación adicional de MAC
    if (macBaja.length() != MAC_LEN || macBaja.indexOf(':') == -1) {
        Serial.println("Error: Formato de MAC inválido");
        LoRa.beginPacket();
        LoRa.print("ERROR:MAC_INVALIDA");
        LoRa.endPacket();
        return;
    }

    Serial.println("Buscando dispositivo...");
    imprimirDispositivosRegistrados();

    if (eliminarDispositivo(macBaja)) {
        LoRa.beginPacket();
        LoRa.print("OK_BAJA");
        LoRa.endPacket();
        Serial.println("Confirmación OK_BAJA enviada");
    } else {
        LoRa.beginPacket();
        LoRa.print("ERROR:NO_ENCONTRADO");
        LoRa.endPacket();
        Serial.println("Error: No se pudo completar la baja");
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

bool guardarDispositivos() {
    EEPROM.begin(EEPROM_SIZE); // Reiniciar EEPROM
    int addr = CONFIG_DISPOSITIVOS_ADDR;
    
    // Contar dispositivos válidos
    int count = 0;
    for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
        if (strlen(configDispositivos[i].mac) > 0) {
            count++;
        }
    }

    // Guardar contador (2 bytes)
    EEPROM.write(addr++, (count >> 8) & 0xFF);
    EEPROM.write(addr++, count & 0xFF);

    // Guardar cada dispositivo
    for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
        if (strlen(configDispositivos[i].mac) > 0) {
            // Guardar MAC (18 bytes incluyendo \0)
            for (int j = 0; j < MAC_LEN + 1; j++) {
                EEPROM.write(addr++, configDispositivos[i].mac[j]);
            }
            
            // Guardar valores (float * VALORES_POR_DISPOSITIVO)
            for (int j = 0; j < VALORES_POR_DISPOSITIVO; j++) {
                EEPROM.put(addr, configDispositivos[i].valores[j]);
                addr += sizeof(float);
            }
            
            // Guardar tipo y estado (2 bytes)
            EEPROM.write(addr++, configDispositivos[i].tipoDispositivo);
            EEPROM.write(addr++, configDispositivos[i].activo ? 1 : 0);
        }
    }

    bool success = EEPROM.commit();
    EEPROM.end();
    
    if (!success) {
        Serial.println("Error crítico: Falló EEPROM.commit()");
        // Aquí podrías agregar reintentos o manejo de error más robusto
    }
    
    return success;
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

  clearMQTTConfirmationFlag();
  mqttConfirmed = false;
  
  Serial.println("Configuración inicial - Modo fábrica");
    for (int i = 0; i < MAX_NETWORKS; i++) {
      savedNetworks[i] = {"", "", false};
    }

    saveNetworksToEEPROM();
    Serial.println("Reestablece correctamente EEPROM y Redes Wiffi....");
    Serial.println("Modo AP activado (configuración inicial)");
}

void clearMQTTConfirmationFlag() {
  EEPROM.write(MQTT_CONFIRMED_FLAG_ADDR, 0);
  EEPROM.commit();
  mqttConfirmed = false;
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

void notificarBajaMQTT(const String &mac) {
  if (!mqttConfirmed) {
    Serial.println("MQTT baja omitida: monitor sin alta confirmada");
    return;
  }

  if (WiFi.status() != WL_CONNECTED || !client.connected()) {
    Serial.println("MQTT baja diferida: sin conexión MQTT activa");
    return;
  }

  String monitorMac = WiFi.macAddress();
  monitorMac.replace("-", ":");

  String mensaje = monitorMac + "," + mac;
  if (userID.length() > 0) {
    mensaje += "," + userID;
  }

  if (client.publish("baja/1/solicitud/", mensaje.c_str())) {
    Serial.println("Solicitud de baja enviada a MQTT: " + mensaje);
  } else {
    Serial.println("Error al publicar solicitud de baja en MQTT");
  }
}


bool loadMQTTConfirmationState() {
  return EEPROM.read(MQTT_CONFIRMED_FLAG_ADDR) == 1;
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

    estadoLED = !estadoLED;
    intentos++;
    Serial.println("Intento #" + String(intentos));
    delay(3000);
  }

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

void recepcion_lora(){

if (LoRa.parsePacket()) {
    String received = LoRa.readString();
    received.trim();
 if ((received.startsWith("REG")) or received.startsWith("BAJA")) {
  return; //si es baja o registro no se procesa por que solo actua con el boron de amparejamiento activo
 }
         mensajeLoRa = received;
        nuevoMensajeLoRa = true;

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
        } else {
            Serial.println("El mensaje contiene errores de formato");
        }
           }
      } else {
        Serial.println("No existe el dispositivo registrado ..."); //quitarlo en produccion solo para depuracion aqui es donde puede escuchar otroa dispositivos NUUP no registrados en este monitor
    }
 }

}

// Función para eliminar un dispositivo
bool eliminarDispositivo(const String &mac) {
    int posicion = -1;
    
    // Buscar el dispositivo
    for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
        if (String(configDispositivos[i].mac) == mac) {
            posicion = i;
            break;
        }
    }
    
    if (posicion == -1) {
        Serial.println("Dispositivo no encontrado para eliminar");
        return false;
    }

    // Debug: Mostrar antes de eliminar
    Serial.println("Dispositivo encontrado en posición: " + String(posicion));
    Serial.println("Contenido antes de eliminar:");
    imprimirConfigDispositivo(mac);

    // Reorganizar el array
    for (int i = posicion; i < MAX_DISPOSITIVOS - 1; i++) {
        // Copiar manualmente cada campo
        strncpy(configDispositivos[i].mac, configDispositivos[i+1].mac, MAC_LEN + 1);
        memcpy(configDispositivos[i].valores, configDispositivos[i+1].valores, sizeof(float) * VALORES_POR_DISPOSITIVO);
        configDispositivos[i].tipoDispositivo = configDispositivos[i+1].tipoDispositivo;
        configDispositivos[i].activo = configDispositivos[i+1].activo;
    }
    
    // Limpiar la última posición
    configDispositivos[MAX_DISPOSITIVOS-1].mac[0] = '\0';
    memset(configDispositivos[MAX_DISPOSITIVOS-1].valores, 0, sizeof(float) * VALORES_POR_DISPOSITIVO);
    configDispositivos[MAX_DISPOSITIVOS-1].tipoDispositivo = 0;
    configDispositivos[MAX_DISPOSITIVOS-1].activo = false;

    // Debug: Mostrar después de eliminar
    Serial.println("Contenido después de eliminar:");
    imprimirDispositivosRegistrados();

    // Guardar cambios
    if (guardarDispositivos()) {
        Serial.println("Dispositivo eliminado y EEPROM actualizada correctamente");
        return true;
    } else {
        Serial.println("Error al guardar cambios en EEPROM");
        return false;
    }
}

// ============================================================================
// DESARROLLO DE FUNCIONES TFT (después del loop)
// ============================================================================

void dibujarHeader() {
  // Línea superior (1/3 del display ≈ 21 pixels)
  
  // WiFi - Esquina superior izquierda
  if (wifiConectado) {
    display.drawBitmap(2, 2, wifiIcon, 16, 16, SSD1306_WHITE);
  } else {
    display.drawBitmap(2, 2, wifiIconOff, 16, 16, SSD1306_WHITE);
  }
  
  // NUUP pequeño (tamaño 1)
  display.setTextSize(1);
  display.setCursor(22, 6); // Posición centrada para texto pequeño
  display.print("NUUP");
  
  // Indicador grande (tamaño 2) - pegado a NUUP
  display.setTextSize(2);
  display.setCursor(22 + 4 * 6, 2); // 4 caracteres "NUUP" × 6px cada uno + espacio
  display.print(" ");
  display.print(dispositivoActual + 1);
  display.print("/");
  display.print(sizeof(dispositivos) / sizeof(dispositivos[0]));
  
  // Batería - Esquina superior derecha
  Dispositivo dispActual = dispositivos[dispositivoActual];
  if (dispActual.bateria >= 3.0) {
    display.drawBitmap(SCREEN_WIDTH - 20, 2, batteryFull, 16, 16, SSD1306_WHITE);
  } else {
    display.drawBitmap(SCREEN_WIDTH - 20, 2, batteryEmpty, 16, 16, SSD1306_WHITE);
  }
}


void dibujarTituloDispositivo(Dispositivo disp) {
  // Segunda línea (1/3 del display) - Título del dispositivo
  display.setTextSize(2);
  display.setCursor(0, 22); // 21 pixels desde arriba
  
  // Ajustar texto si es muy largo
  String titulo = disp.nombre;
  if (titulo.length() > 12) {
    titulo = titulo.substring(0, 12);
  }
  
  display.println(titulo);
}

void dibujarContenidoPrincipal(Dispositivo disp) {
  // Limpiar solo las áreas necesarias
  display.fillRect(0, 44, 128, 20, SSD1306_BLACK);
  
  // Porcentaje grande (izquierda)
  display.setTextSize(3);
  display.setCursor(0, 44);
  display.print(disp.porcentaje);
  display.print("%");
  
  // Litros (derecha)
  display.setTextSize(2);
  display.setCursor(70, 48);
  display.print(disp.litros);
  display.setTextSize(1);
  display.setCursor(70 + String(disp.litros).length() * 12, 52);
  display.print("L");
}

void dibujarCirculoGiratorio(int centroX, int centroY, int radio, int angulo) {
  // Dibujar círculo base
  display.drawCircle(centroX, centroY, radio, SSD1306_WHITE);
  
  // Calcular punto en el círculo según el ángulo
  float radianes = angulo * PI / 180.0;
  int puntoX = centroX + radio * cos(radianes);
  int puntoY = centroY + radio * sin(radianes);
  
  // Dibujar punto giratorio
  display.fillCircle(puntoX, puntoY, 3, SSD1306_WHITE);
}

void mostrarEmparejamiento() {
  display.clearDisplay();
  
  // Centro de la pantalla
  int centroX = SCREEN_WIDTH / 2;
  int centroY = 20;
  
  // Dibujar círculo giratorio
  int angulo = (frameAnimacion * 45) % 360; // Gira 45° por frame
  dibujarCirculoGiratorio(centroX, centroY, 15, angulo);
  
  // Texto "Emparejando"
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  

  display.setCursor(0, 40);
  display.print("ALTA");
  
  // Puntos animados debajo
  display.setTextSize(1);
  int puntosAncho = 3 * 6; // 3 puntos × 6px
  display.setCursor((SCREEN_WIDTH - puntosAncho) / 2, 55);
  
  // Animación de puntos (0, 1, 2, 3 puntos)
  int numPuntos = (frameAnimacion / 2) % 4;
  for(int i = 0; i < numPuntos; i++) {
    display.print(".");
  }
  
  display.display();
}

void iniciarEmparejamiento() {
  emparejando = true;
  frameAnimacion = 0;
  ultimoCambioAnimacion = millis();
  Serial.println("Iniciando emparejamiento...");
}

void detenerEmparejamiento() {
  emparejando = false;
  Serial.println("Emparejamiento completado");
}

void emparejarNuevoDispositivo() {
  iniciarEmparejamiento();
}

void actualizarDatos(int index, int porcentaje, float bateria, int litros) {
  if (index >= 0 && index < (sizeof(dispositivos) / sizeof(dispositivos[0]))) {
    dispositivos[index].porcentaje = porcentaje;
    dispositivos[index].bateria = bateria;
    dispositivos[index].litros = litros;
  }
}

void setWifiStatus(bool conectado) {
  wifiConectado = conectado;
}


// Función para dibujar WiFi animado (creciente)
void dibujarWifiAnimado(int centroX, int centroY, int frame) {
  int maxNiveles = 4; // Niveles del icono WiFi
  
  for(int nivel = 0; nivel < maxNiveles; nivel++) {
    int radio = 3 + (nivel * 4); // Radios: 3, 7, 11, 15
    
    // Solo dibujar niveles hasta el frame actual
    if (nivel <= frame) {
      if (nivel < maxNiveles - 1) {
        // Arcos del WiFi (niveles 0, 1, 2)
        display.drawCircle(centroX, centroY, radio, SSD1306_WHITE);
      } else {
        // Punto central (nivel 3)
        display.fillCircle(centroX, centroY, 2, SSD1306_WHITE);
      }
    }
  }
}

// Función para mostrar conexión WiFi
void mostrarConexionWifi() {
  display.clearDisplay();
  
  // Centro de la pantalla
  int centroX = SCREEN_WIDTH / 2;
  int centroY = 20;
  
  // Dibujar WiFi animado
  dibujarWifiAnimado(centroX, centroY, frameWifi);
  
  // Texto "Conectando WiFi" - ALINEADO A LA IZQUIERDA
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  
  display.setCursor(0, 45);
  display.print("WiFi");
   
  // Puntos animados
  display.setTextSize(2);
  int posicionPuntos = 4 * 12;
  display.setCursor(posicionPuntos, 45);
  
  // Animación de puntos (0, 1, 2, 3 puntos)
  int numPuntos = (frameWifi / 2) % 4;
  for(int i = 0; i < numPuntos; i++) {
    display.print(".");
  }
  
  display.display();
}

// Función para iniciar animación WiFi
void iniciarAnimacionWifi() {
  animandoWifi = true;
  frameWifi = 0;
  ultimoCambioWifi = millis();
  Serial.println("Iniciando animación WiFi...");
}

// Función para detener animación WiFi
void detenerAnimacionWifi() {
  animandoWifi = false;
  wifiConectado = true; // Marcar como conectado
  Serial.println("Animación WiFi completada");
}

// Función para conectar WiFi (desde otras partes del código)
void conectarWifi() {
  iniciarAnimacionWifi();
}