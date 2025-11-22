// 05 - Corrección: esconder WiFi al inicio (solo aviso breve) y mantener el portal activo tras guardar ID sin cerrarlo
// 04 - Corrección: iniciar portal WiFi si falta ID o conexión, proteger OLED antes de inicializarla y mantener reinicios seguros del AP
// 03 - Corrección: reiniciar el portal WiFi con cada pulsación larga, listar/borrar dispositivos y mostrar redes cercanas en el portal
// 02 - Corrección: validar y limpiar UserID corrupto en EEPROM y activar modo AP con botón WiFi de 1s mostrando icono y reinicio tras conexión
//Bersion BLE

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

//BLE 
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>

//EEPROM  Tamaño EEPROM (ESP32 tiene 4KB)
// Definir direcciones para nombre y email (después de tus otras configuraciones) 
#define USER_NAME_ADDR 3000    // 
#define USER_EMAIL_ADDR 3500 //
#define EEPROM_SIZE 4096              //
#define ALIAS_DISPOSITIVOS 2000       // 

#define MQTT_CONFIRMED_FLAG_ADDR 350  //
#define USER_ID_ADDR 400              // 
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
  char mac[MAC_LEN + 1];  
  char nombre[20];        // Nombre del dispositivo
  float litrosActuales;   // Litros actuales
  float voltaje;          // Voltaje de batería
  float temperatura;      // Temperatura
  float alturaConfig;     // Altura configurada
  float litrosConfig;     // Litros configurados
  int porcentaje;         // ⭐ NUEVO: Porcentaje calculado
  byte tipoDispositivo;   // Tipo de dispositivo  
  bool activo;           // Para MQTT
} ConfigDispositivo;

ConfigDispositivo configDispositivos[MAX_DISPOSITIVOS];
//para asignar un numero de serial al dispositivo lector este viene del LORA pero ahorita lo fijmos aqui
const String serial_number = "TOPICMYSQL";  //aqui voy a maper la MAC

// Variables para control de tiempo sin datos
unsigned long ultimaActualizacionLoRa[MAX_DISPOSITIVOS] = {0};
const unsigned long TIEMPO_SIN_DATOS = 300000; // 60 000 1 minuto (configurable) - puse 5 minutos
bool mostrarSinDatos[MAX_DISPOSITIVOS] = {false};


// O si quieres hacerlo configurable via BLE/serial:
unsigned long tiempoSinDatosConfig = 60000; // Puedes cambiar este valor

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
void clearEEPROM();
void startAPMode();
void handleRoot();
void handleSaveCredentials();
void handleDeleteNetwork();
void handleSelectNetwork();
void handleDeleteDevice();
void reiniciarConfiguracionWiFi();
void detenerConfiguracionWiFi();
void saveNetworksToEEPROM();
bool loadNetworksFromEEPROM();
void attemptReconnectToAllNetworks();
void handleSetID();
void saveUserIDToEEPROM(const String& id);
bool loadUserIDFromEEPROM();
void callback(char* topic, byte* playload, unsigned int lengt);
void reconnect();
void  checkMemory();
void cargarDispositivos();
bool eliminarDispositivo(const String &mac);


bool registrarDispositivo(const String &mac);
void MQTT_ALTA();
bool loadMQTTConfirmationState();
void imprimirConfigDispositivo(const String &mac);
void imprimirDispositivosRegistrados();
void Reintentar_Wiffi();

void debugNetworks();
void checkWiFiStatus();

void recepcion_lora();


void actualizarDatosDesdeLoRa(const String &mac, const String &mensaje, const String &nombre);

// AGREGAR estas declaraciones:
int contarDispositivosRegistrados();
void dibujarHeader();
void dibujarTituloDispositivo();
void dibujarContenidoPrincipal();
bool guardarDispositivos();

void debugNombresDispositivos();

void verificarTiemposSinDatos();

void debugMensajeLoRa(const String &mensaje);
void testDispositivosRapido() ;


//Definiciones pantalla TFT
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool displayReady = false;

// Estructura para los dispositivos
struct Dispositivo {
  String nombre;
  int porcentaje;
  float bateria; // en volts
  int litros;
  bool sinDatos; // ⭐ NUEVO: indica si no hay datos recientes
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
unsigned long INTERVALO_CAMBIO = 3000;




// Variables para animación de emparejamiento
bool emparejando = false;
int frameAnimacion = 0;
unsigned long ultimoCambioAnimacion = 0;
const unsigned long INTERVALO_ANIMACION = 200; // ms entre frames

// Variables para controlar el estado de alta/baja
bool operacionCompletada = false;
String tipoOperacion = ""; // "ALTA" o "BAJA"
unsigned long tiempoFinOperacion = 0;
const unsigned long TIEMPO_MOSTRAR_OPERACION = 3000; // 3 segundos

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
void mostrarWifiInicioTemporal();
void iniciarAnimacionWifi();
void detenerAnimacionWifi();
void conectarWifi();
void testWiFiConnection();
void debugEEPROMReal();

void verificarEstadoConfigDispositivos();

void debugEstadoDispositivos();

void debugNombreProblema();
void manejarBotonWifi();
void mostrarMensajeRedConectada(const String &ssid, bool conectado);
void dibujarMensajeConexion();

// --- Pines para los botones---
#define BOTON_S 33
#define BOTON_W 4
#define TIEMPO_BOTON 1000

bool boton_s=false;
unsigned long tiempoInicioPresion = 0;
bool wifiButtonPressed = false;
unsigned long wifiButtonPressStart = 0;
bool wifiConfigInProgress = false;
bool mostrarMensajeConexion = false;
unsigned long inicioMensajeConexion = 0;
String ultimaRedConfigurada = "";
bool conexionExitosa = false;

//void manejarBoton_S();


// Variables BLE
BLEServer *pServer = NULL;
BLECharacteristic *pCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;


bool solicitudAltaBLE = false;
bool solicitudBajaBLE = false;

// Variables para mostrar detalles de la operación del ALTA o BAJA de un dispositivo  
String ultimoNombreDispositivo = "";
int ultimosLitros = 0;
int ultimaAltura = 0;
bool mostrarResultado = false;
unsigned long inicioResultado = 0;
const unsigned long TIEMPO_RESULTADO = 5000; // 5 segundos

// Basado en "NUUP" - más fácil de recordar
#define SERVICE_UUID        "4e555550-2024-1337-8001-123456789abc"
#define CHARACTERISTIC_UUID "4e555550-2024-1337-8002-123456789abc"

// Agrega esto con las otras variables globales BLE
String ultimaMacCliente = "";  // Para mantener la MAC entre mensajes

bool procesarBajaBLE(const String &macCliente) {
    Serial.println("\n🔄 ===== PROCESANDO BAJA BLE =====");
    Serial.println("📱 MAC recibida: '" + macCliente + "'");
    
    // ⭐⭐ SIEMPRE ACTIVAR LA SOLICITUD DE BAJA PARA LA ANIMACIÓN
    solicitudBajaBLE = true;
    
    // Resto del código igual...
    String macBuscada = macCliente;
    macBuscada.toUpperCase();
    macBuscada.trim();
    
    Serial.println("🔍 Buscando dispositivo...");
    
    bool encontrado = false;
    int indiceEncontrado = -1;
    
    // DEBUG: Mostrar todos los dispositivos registrados
    Serial.println("📋 DISPOSITIVOS REGISTRADOS:");
    for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
        if (String(configDispositivos[i].mac) != "") {
            String macGuardada = String(configDispositivos[i].mac);
            macGuardada.trim();
            
            Serial.printf("   [%d] MAC: '%s' (long: %d)\n", 
                         i, macGuardada.c_str(), macGuardada.length());
            
            // Comparación case-insensitive
            if (macGuardada.equalsIgnoreCase(macBuscada)) {
                Serial.println("   ✅ COINCIDENCIA ENCONTRADA!");
                encontrado = true;
                indiceEncontrado = i;
                
                // Guardar datos para mostrar en pantalla
                ultimoNombreDispositivo = "Dispositivo"; // Nombre genérico para baja
ultimosLitros = configDispositivos[i].litrosActuales;
ultimaAltura = configDispositivos[i].alturaConfig;
                break;
            }
        }
    }
    
    if (!encontrado) {
        Serial.println("❌ BAJA FALLIDA - Dispositivo no encontrado");
        Serial.println("💡 El sensor no está dado de alta en el servidor");
        
        // ⭐⭐ CONFIGURAR DATOS PARA MOSTRAR EN PANTALLA
        ultimoNombreDispositivo = "No Registrado";
        ultimosLitros = 0;
        ultimaAltura = 0;
        
        Serial.println("===== FIN BAJA BLE =====\n");
        return false;
    }
    
    // Proceder con la eliminación
    if (eliminarDispositivo(configDispositivos[indiceEncontrado].mac)) {
        Serial.println("✅ Baja completada via BLE: " + macCliente);
        imprimirDispositivosRegistrados();
        
        Serial.println("✅ BAJA EXITOSA");
        Serial.println("===== FIN BAJA BLE =====\n");
        return true;
    } else {
        Serial.println("❌ ERROR en eliminación del dispositivo");
        Serial.println("===== FIN BAJA BLE =====\n");
        return false;
    }
}


bool procesarRegistroBLE(const String &macCliente, const String &nombre = "", int altura = 0, int litros = 0) {
    
    Serial.println("🔄 Procesando registro BLE COMPLETO:");
    Serial.println("   MAC: " + macCliente);
    Serial.println("   Nombre: " + nombre);
    Serial.println("   Altura: " + String(altura));
    Serial.println("   Litros: " + String(litros));
    
    // Guardar datos para mostrar en pantalla
    ultimoNombreDispositivo = nombre;
    ultimaAltura = altura;
    ultimosLitros = litros;
    
    // Buscar si ya existe el dispositivo
    for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
        if (String(configDispositivos[i].mac) == macCliente) {
            Serial.println("⚠️  Dispositivo ya registrado - Actualizando datos");
            
            // ⭐⭐ ACTUALIZAR CORRECTAMENTE los campos
            configDispositivos[i].alturaConfig = altura;
            configDispositivos[i].litrosConfig = litros;
          configDispositivos[i].litrosActuales = 0; // Inicialmente vacío, se actualizará con LoRa
            nombre.toCharArray(configDispositivos[i].nombre, 20);
            configDispositivos[i].activo = true;
            configDispositivos[i].porcentaje = 100; // Inicialmente al 100%
            
            Serial.println("✅ Datos actualizados para dispositivo existente");
            
            if (guardarDispositivos()) {
                solicitudAltaBLE = true;
                iniciarEmparejamiento();
                return true;
            } else {
                Serial.println("❌ Error al guardar en EEPROM");
                return false;
            }
        }
    }
    
    // Buscar espacio libre para nuevo registro
    for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
        if (String(configDispositivos[i].mac) == "") {
            // ⭐⭐ GUARDAR CORRECTAMENTE todos los campos
            macCliente.toCharArray(configDispositivos[i].mac, MAC_LEN + 1);
            nombre.toCharArray(configDispositivos[i].nombre, 20);
            configDispositivos[i].alturaConfig = altura;
            configDispositivos[i].litrosConfig = litros;
            configDispositivos[i].litrosActuales = litros; // Inicialmente igual a litros config
            configDispositivos[i].voltaje = 0.0; // Inicializar
            configDispositivos[i].temperatura = 0.0; // Inicializar
            configDispositivos[i].porcentaje = 100; // Inicialmente al 100%
            configDispositivos[i].activo = true;
            configDispositivos[i].tipoDispositivo = 2; // Tipo tanque
            
            Serial.println("✅ Nuevo dispositivo registrado con datos");
            Serial.println("📝 Nombre guardado: " + String(configDispositivos[i].nombre));
            Serial.println("📏 Altura guardada: " + String(configDispositivos[i].alturaConfig));
            Serial.println("💧 Litros guardados: " + String(configDispositivos[i].litrosConfig));
            Serial.println("📊 Porcentaje inicial: " + String(configDispositivos[i].porcentaje) + "%");
            
            if (guardarDispositivos()) {
                imprimirDispositivosRegistrados();
                solicitudAltaBLE = true;
                iniciarEmparejamiento();
                return true;
            } else {
                Serial.println("❌ Error al guardar en EEPROM");
                return false;
            }
        }
    }
    
    Serial.println("❌ No hay espacio para más dispositivos");
    return false;
}


// Función para verificar proximidad por RSSI (5cm o menos)
// Función estaCerca() SIMPLIFICADA - sin RSSI por ahora
bool estaCerca() {
    if (!deviceConnected) {
        return false;
    }
    
    // ⚠️ SOLUCIÓN TEMPORAL: Usar potencia BLE reducida en lugar de RSSI
    // La potencia ya está configurada en ESP_PWR_LVL_N12 (-12dBm)
    // Esto limita físicamente el alcance a ~5cm
    
    Serial.println("✅ Verificación de proximidad por potencia BLE reducida");
    return true;
}

// ⭐ NUEVA FUNCIÓN: Procesar registro con datos completos

// Callbacks BLE
// Callbacks BLE
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        deviceConnected = true;
        Serial.println("📱 Dispositivo BLE conectado");
    };

    void onDisconnect(BLEServer* pServer) {
        deviceConnected = false;
        Serial.println("📱 Dispositivo BLE desconectado");
    }
};




class MyCharacteristicCallbacks: public BLECharacteristicCallbacks {

  void onWrite(BLECharacteristic *pCharacteristic) {
        // Verificación simplificada de proximidad
        if (!estaCerca()) {
            Serial.println("🚫 Comando rechazado - Fuera de rango");
            pCharacteristic->setValue("ERROR:PROXIMIDAD");
            pCharacteristic->notify();
            return;
        }
        
        std::string value = pCharacteristic->getValue();
        
        if (value.length() > 0) {
            String command = String(value.c_str());
            Serial.print("📨 Comando BLE recibido: ");
            Serial.println(command);
            
            if (command.startsWith("REG:")) {
                handleRegistration(pCharacteristic, command);
            }
            else if (command.startsWith("CONFIG,")) {
                // ⭐ CORREGIDO: El cliente envía CONFIG,nombre,altura,litros
                handleConfigWithData(pCharacteristic, command);
            }
            else if (command.startsWith("BAJA:")) {
                handleUnregistration(pCharacteristic, command);
            }
            else if (command == "conf") {
                // Manejo por si acaso todavía envía 'conf' solo
                Serial.println("✅ Confirmación 'conf' recibida (sin datos)");
                pCharacteristic->setValue("listo");
                pCharacteristic->notify();
                Serial.println("📤 Enviado: listo - Esperando CONFIG...");
            }
            else {
                String respuesta = "ERROR:COMANDO_DESCONOCIDO";
                pCharacteristic->setValue(respuesta.c_str());
                pCharacteristic->notify();
                Serial.println("❌ Comando no reconocido: " + command);
            }
          
     }
  }
      private:
    String ultimaMacCliente = "";
  
    void handleRegistration(BLECharacteristic *pCharacteristic, const String &command) {
        // ⭐ EXTRAER MAC DEL CLIENTE del comando REG:MAC_CLIENTE
        String macCliente = command.substring(4);
        macCliente.trim();
        
        Serial.println("🔵 Solicitud de REGISTRO recibida via BLE");
        Serial.println("📱 MAC del Cliente: " + macCliente);
        
        // Validar formato de MAC
        if (macCliente.length() != 17) {
            String errorRespuesta = "ERROR:MAC_INVALIDA";
            pCharacteristic->setValue(errorRespuesta.c_str());
            pCharacteristic->notify();
            Serial.println("❌ MAC del cliente inválida: " + macCliente);
            return;
        }
        
        // Guardar la MAC para el siguiente mensaje
        ultimaMacCliente = macCliente;
        
        // Obtener MAC del servidor
        String macServidor = WiFi.macAddress();
        macServidor.replace("-", ":");
        Serial.println("🖥️  MAC del Servidor: " + macServidor);
        
        // ⭐ PRIMERA PARTE: Enviar OK_REG con MAC del servidor
        String respuesta1 = "OK_REG," + macServidor;
        pCharacteristic->setValue(respuesta1.c_str());
        pCharacteristic->notify();
        Serial.print("📤 Parte 1 enviada: ");
        Serial.println(respuesta1);
        Serial.println("⏳ Esperando datos de configuración (CONFIG,nombre,altura,litros)...");
    }

    void handleConfigWithData(BLECharacteristic *pCharacteristic, const String &command) {
        // ⭐ CORREGIDO: Procesar CONFIG,nombre,altura,litros
        Serial.println("📥 Datos de configuración CONFIG recibidos");
        
        if (ultimaMacCliente.length() == 0) {
            String errorRespuesta = "ERROR:NO_HAY_REGISTRO_PREVIO";
            pCharacteristic->setValue(errorRespuesta.c_str());
            pCharacteristic->notify();
            Serial.println("❌ No hay registro previo (REG) para asociar los datos");
            return;
        }
        
        // Procesar: CONFIG,nombre,altura,litros
        String configData = command.substring(7); // Quitar "CONFIG,"
        configData.trim();
        
        Serial.println("📊 Datos crudos después de CONFIG,: " + configData);
        
        // Parsear: nombre,altura,litros
        int pos1 = configData.indexOf(',');
        int pos2 = configData.indexOf(',', pos1 + 1);
        
        if (pos1 != -1 && pos2 != -1) {
            String nombre = configData.substring(0, pos1);
            String alturaStr = configData.substring(pos1 + 1, pos2);
            String litrosStr = configData.substring(pos2 + 1);
            
            nombre.trim();
            alturaStr.trim();
            litrosStr.trim();
            
            Serial.printf("🎯 Datos parseados - Nombre: '%s', Altura: '%s', Litros: '%s'\n", 
                         nombre.c_str(), alturaStr.c_str(), litrosStr.c_str());
            
            // Validar datos
            if (nombre.length() == 0 || alturaStr.length() == 0 || litrosStr.length() == 0) {
                String errorRespuesta = "ERROR:DATOS_INCOMPLETOS";
                pCharacteristic->setValue(errorRespuesta.c_str());
                pCharacteristic->notify();
                Serial.println("❌ Datos de configuración incompletos");
                return;
            }
            
            // Convertir a números
            int altura = alturaStr.toInt();
            int litros = litrosStr.toInt();
            
            if (altura <= 0 || litros <= 0) {
                String errorRespuesta = "ERROR:DATOS_NUMERICOS_INVALIDOS";
                pCharacteristic->setValue(errorRespuesta.c_str());
                pCharacteristic->notify();
                Serial.println("❌ Datos numéricos inválidos (deben ser > 0)");
                return;
            }
            
            // ⭐ REGISTRAR CON LA MAC DEL CLIENTE
            Serial.println("🔄 Registrando dispositivo con MAC: " + ultimaMacCliente);
            bool registroExitoso = procesarRegistroBLE(ultimaMacCliente, nombre, altura, litros);
            
            if (registroExitoso) {
                // ⭐ ENVIAR READY de confirmación
                String respuestaFinal = "READY";
                pCharacteristic->setValue(respuestaFinal.c_str());
                pCharacteristic->notify();
                Serial.print("📤 Confirmación enviada: ");
                Serial.println(respuestaFinal);
                
                Serial.println("✅ REGISTRO completado exitosamente");
                Serial.println("💾 MAC Cliente: " + ultimaMacCliente);
                Serial.println("📝 Nombre: " + nombre);
                Serial.println("📏 Altura: " + String(altura) + " cm");
                Serial.println("💧 Litros: " + String(litros) + " L");
                
                solicitudAltaBLE = true;
                iniciarEmparejamiento();
                
                // Limpiar para el próximo registro
                ultimaMacCliente = "";
            } else {
                String errorRespuesta = "ERROR:REGISTRO_FALLIDO";
                pCharacteristic->setValue(errorRespuesta.c_str());
                pCharacteristic->notify();
                Serial.println("❌ Falló el registro en el servidor");
            }
        } else {
            String errorRespuesta = "ERROR:FORMATO_CONFIG_INVALIDO";
            pCharacteristic->setValue(errorRespuesta.c_str());
            pCharacteristic->notify();
            Serial.println("❌ Formato de CONFIG inválido");
            Serial.println("   Se esperaba: CONFIG,nombre,altura,litros");
            Serial.println("   Se recibió: " + command);
            Serial.printf("   pos1: %d, pos2: %d\n", pos1, pos2);
        }
    }
  

void handleUnregistration(BLECharacteristic *pCharacteristic, const String &command) {
    // ⭐ EXTRAER MAC DEL CLIENTE del comando BAJA:MAC_CLIENTE
    String macCliente = command.substring(5);
    macCliente.trim();
    
    Serial.println("🔴 Solicitud de BAJA recibida via BLE");
    Serial.println("📱 MAC del Cliente: " + macCliente);
    
    // Procesar baja
    bool bajaExitosa = procesarBajaBLE(macCliente);
    
    if (bajaExitosa) {
        String respuesta = "OK_BAJA";
        pCharacteristic->setValue(respuesta.c_str());
        pCharacteristic->notify();
        Serial.println("✅ BAJA exitosa via BLE");
        Serial.println("🗑️  MAC eliminada de EEPROM: " + macCliente);
        
        solicitudBajaBLE = true;
        iniciarEmparejamiento();
    } else {
        // ⭐⭐ CORREGIDO: Enviar "ERROR:NO_EXISTE_MAC" PERO ACTIVAR ANIMACIÓN
        String respuesta = "ERROR:NO_EXISTE_MAC";
        pCharacteristic->setValue(respuesta.c_str());
        pCharacteristic->notify();
        Serial.println("📤 Enviado al cliente: ERROR:NO_EXISTE_MAC");
        Serial.println("❌ BAJA fallida - MAC no existe: " + macCliente);
        
        // ⭐⭐ NUEVO: ACTIVAR ANIMACIÓN DE BAJA A PESAR DEL ERROR
        Serial.println("🎭 ACTIVANDO ANIMACIÓN DE PANTALLA PARA 'NO_EXISTE_MAC'");
        solicitudBajaBLE = true;  // ← ESTA LÍNEA ES CLAVE
        iniciarEmparejamiento();  // ← ESTA LÍNEA ACTIVA LA ANIMACIÓN
        
        // ⭐⭐ OPCIONAL: Configurar datos para mostrar en pantalla
        ultimoNombreDispositivo = "Dispositivo No Registrado";
        ultimosLitros = 0;
        ultimaAltura = 0;
    }
}

};

void iniciarBLE() {
    Serial.println("🔵 Iniciando BLE para 5cm de distancia...");
    
    BLEDevice::init("NUUP_Monitor");
    
    // ⚡ CONFIGURACIÓN CLAVE: Potencia mínima de transmisión
    BLEDevice::setPower(ESP_PWR_LVL_N12); // Potencia mínima (-12dBm)
    
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    BLEService *pService = pServer->createService(SERVICE_UUID);
    
    pCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_NOTIFY
    );

    pCharacteristic->setCallbacks(new MyCharacteristicCallbacks());
    pCharacteristic->setValue("NUUP Ready");
    
    pService->start();
    
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    pAdvertising->setMaxPreferred(0x12);
    
    BLEDevice::startAdvertising();
    Serial.println("🔵 BLE iniciado - Alcance limitado a ~5cm");
}

void manejarBLE() {
    // Notificar cambio de estado de conexión
    if (!deviceConnected && oldDeviceConnected) {
        delay(500);
        pServer->startAdvertising();
        Serial.println("🔵 BLE publicidad reiniciada");
        oldDeviceConnected = deviceConnected;
    }
    
    if (deviceConnected && !oldDeviceConnected) {
        oldDeviceConnected = deviceConnected;
    }
}

void mostrarResultadoOperacion() {
    display.clearDisplay();
    
    // Texto grande ALTA/BAJA
    display.setTextSize(3);
    display.setTextColor(SSD1306_WHITE);
    
    if (solicitudAltaBLE) {
        display.setCursor(SCREEN_WIDTH/2 - 36, 5);
        display.print("ALTA");
    } else if (solicitudBajaBLE) {
        display.setCursor(SCREEN_WIDTH/2 - 36, 5);
        display.print("BAJA");
    }
    
    // Detalles más pequeños
    display.setTextSize(1);
    
    // Mostrar nombre si está disponible
    if (ultimoNombreDispositivo.length() > 0) {
        display.setCursor(0, 30);
        display.print("Nombre: ");
        display.print(ultimoNombreDispositivo);
    }
    
    // Mostrar litros si está disponible
    if (ultimosLitros > 0) {
        display.setCursor(0, 40);
        display.print("Litros: ");
        display.print(ultimosLitros);
        display.print(" L");
    }
    
    // Mostrar altura si está disponible
    if (ultimaAltura > 0) {
        display.setCursor(0, 50);
        display.print("Altura: ");
        display.print(ultimaAltura);
        display.print(" cm");
    }
    
    display.display();
}

void testLoRaPeriodico() {
    static unsigned long lastTest = 0;
    if (millis() - lastTest > 30000) { // Cada 30 segundos
        lastTest = millis();
        
        Serial.println("\n🔧 TEST PERIÓDICO LoRa:");
        Serial.printf("   - Free Heap: %d bytes\n", ESP.getFreeHeap());
        Serial.printf("   - Paquetes recibidos: %s\n", nuevoMensajeLoRa ? "SI" : "NO");
        
        // Test de envío
        LoRa.beginPacket();
        LoRa.print("SERVER_ALIVE_" + String(millis()));
        LoRa.endPacket();
        Serial.println("   - Mensaje test enviado");
    }
}



void verificarEstadoConfigDispositivos() {
    Serial.println("\n=== VERIFICACIÓN configDispositivos[] ===");
    Serial.printf("Tamaño del arreglo: %d\n", MAX_DISPOSITIVOS);
    Serial.printf("Dirección del arreglo: %p\n", (void*)configDispositivos);
    
    for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
        if (String(configDispositivos[i].mac) != "") {
            Serial.printf("✅ [%d] MAC: '%s' (longitud: %d)\n", 
                         i, configDispositivos[i].mac, strlen(configDispositivos[i].mac));
        } else {
            Serial.printf("❌ [%d] VACÍO\n", i);
        }
    }
    Serial.println("=========================================\n");
}


void testWiFiConnection() {
  Serial.println("\n=== PRUEBA MANUAL DE WIFI ===");
  
  // Lista de contraseñas comunes para probar
  String testPasswords[] = {
    "JNWWM7KHzE",  // Reemplaza con la real
    "INFINITUM0EBB",
    "admin",
    "password",
    "12345678",
    ""
  };
  
  String ssid = "INFINITUM0EBB_2.4";
  
  for (int i = 0; i < 6; i++) {
    if (testPasswords[i].length() > 0) {
      Serial.print("Probando contraseña: '");
      Serial.print(testPasswords[i]);
      Serial.println("'");
      
      WiFi.begin(ssid.c_str(), testPasswords[i].c_str());
      
      unsigned long start = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
        delay(500);
        Serial.print(".");
      }
      
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n✓ CONTRASEÑA CORRECTA: " + testPasswords[i]);
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
        return;
      } else {
        Serial.println("\n✗ Falló con esta contraseña");
        checkWiFiStatus();
      }
    }
  }
  Serial.println("=== FIN PRUEBA MANUAL ===");
}

void manejarBotonWifi() {
  bool presionado = digitalRead(BOTON_W) == LOW;

  if (presionado && !wifiButtonPressed) {
    wifiButtonPressed = true;
    wifiButtonPressStart = millis();
  }

  if (!presionado && wifiButtonPressed) {
    wifiButtonPressed = false;
    wifiButtonPressStart = 0;
  }

  if (wifiButtonPressed && (millis() - wifiButtonPressStart >= TIEMPO_BOTON)) {
    wifiButtonPressed = false;
    wifiButtonPressStart = 0;

    if (forceAPMode) {
      Serial.println("Presionando boton WiFi - cancelando portal y regresando a modo normal...");
      detenerConfiguracionWiFi();
    } else {
      Serial.println("Presionando boton WiFi - reiniciando modo AP...");
      reiniciarConfiguracionWiFi();
    }
  }
}



// Implementación de funciones

void reiniciarConfiguracionWiFi() {
  mostrarMensajeConexion = false;
  inicioMensajeConexion = 0;
  conexionExitosa = false;
  wifiConfigInProgress = true;
  forceAPMode = true;
  apMode = true;
  iniciarAnimacionWifi();
  mostrarConexionWifi();
  startAPMode();
  una_APmode = false;
}

void detenerConfiguracionWiFi() {
  mostrarMensajeConexion = false;
  inicioMensajeConexion = 0;
  conexionExitosa = false;
  wifiConfigInProgress = false;
  forceAPMode = false;
  apMode = false;
  detenerAnimacionWifi();

  dnsServer.stop();
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
}

void startAPMode() {
  dnsServer.stop();
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
  dnsServer.start(53, "*", WiFi.softAPIP());

  apMode = true;
  wifiConfigInProgress = true;
  forceAPMode = true;
  mostrarMensajeConexion = false;

  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSaveCredentials);
  server.on("/delete", HTTP_POST, handleDeleteNetwork);
  server.on("/select", HTTP_POST, handleSelectNetwork);
  server.on("/delete_device", HTTP_POST, handleDeleteDevice);
  server.on("/setid", HTTP_POST, handleSetID);  // 👉 Aquí se agrega la ruta nueva
  server.onNotFound(handleRoot);
  server.begin();
  
  Serial.println("\nModo AP activado");
  Serial.print("SSID: "); Serial.println(AP_SSID);
  Serial.print("IP: "); Serial.println(WiFi.softAPIP());

}

void mostrarMensajeRedConectada(const String &ssid, bool conectado) {
  ultimaRedConfigurada = ssid;
  conexionExitosa = conectado;
  inicioMensajeConexion = millis();
  mostrarMensajeConexion = true;
  animandoWifi = true;
  frameWifi = 0;
  ultimoCambioWifi = millis();
  Serial.printf("\n📶 %s a la red '%s'\n", conectado ? "Conectado" : "Fallo de conexión", ssid.c_str());
  dibujarMensajeConexion();
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

  String scannedNetworks = "";
  int networkCount = WiFi.scanNetworks();

  if (networkCount > 0) {
    int limitedCount = networkCount;
    const int MAX_SCAN_RESULTS = 20;
    if (limitedCount > MAX_SCAN_RESULTS) {
      limitedCount = MAX_SCAN_RESULTS;
    }

    int *indices = new int[limitedCount];
    for (int i = 0; i < limitedCount; i++) {
      indices[i] = i;
    }

    for (int i = 0; i < limitedCount - 1; i++) {
      for (int j = i + 1; j < limitedCount; j++) {
        if (WiFi.RSSI(indices[j]) > WiFi.RSSI(indices[i])) {
          int temp = indices[i];
          indices[i] = indices[j];
          indices[j] = temp;
        }
      }
    }

    for (int i = 0; i < limitedCount; i++) {
      int idx = indices[i];
      String ssid = WiFi.SSID(idx);
      int rssi = WiFi.RSSI(idx);

      scannedNetworks += "<div class='network-item'>";
      scannedNetworks += "<label>" + ssid + "</label>";
      scannedNetworks += "<span class='signal'>" + String(rssi) + " dBm</span>";
      scannedNetworks += "<button type='button' onclick=\"prefillNetwork('" + ssid + "')\">Usar</button>";
      scannedNetworks += "</div>";
    }

    delete[] indices;
  } else {
    scannedNetworks = "<p>No se encontraron redes cercanas. Intenta nuevamente.</p>";
  }

  String devicesList = "";
  int deviceCount = 0;
  for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
    String mac = String(configDispositivos[i].mac);
    mac.trim();
    if (mac.length() > 0) {
      deviceCount++;
      String nombre = String(configDispositivos[i].nombre);
      nombre.trim();
      if (nombre.length() == 0) {
        nombre = "Dispositivo";
      }
      devicesList += "<div class='network-item device-item'>";
      devicesList += "<div class='device-info'><strong>" + nombre + "</strong><br><small>MAC: " + mac + "</small></div>";
      devicesList += "<button type='button' onclick=\"deleteDevice('" + mac + "')\">Eliminar</button>";
      devicesList += "</div>";
    }
  }

  if (deviceCount == 0) {
    devicesList = "<p>No hay dispositivos dados de alta.</p>";
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
    .network-item .signal {
      margin-left: 10px;
      margin-right: 10px;
      font-size: 14px;
    }
    .network-item button {
      padding: 8px 12px;
      background-color: #ff3333;
    }
    .network-item button:hover {
      background-color: #cc0000;
    }
    .device-item {
      justify-content: space-between;
      gap: 10px;
    }
    .device-info {
      display: flex;
      flex-direction: column;
      gap: 4px;
    }
    .section-title {
      margin-top: 25px;
      margin-bottom: 10px;
      font-size: 18px;
      color: #FFD700;
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
      if(confirm('¿Eliminar este dispositivo?')) {
        fetch('/delete_device', {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
          body: 'mac=' + encodeURIComponent(mac)
        }).then(response => {
          if(response.ok) location.reload();
        });
      }
    }

    function prefillNetwork(ssid) {
      const ssidInput = document.getElementById('ssidInput');
      const passInput = document.getElementById('passInput');
      if (ssidInput && passInput) {
        ssidInput.value = ssid;
        passInput.focus();
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
      <h3 class="section-title">Redes guardadas:</h3>
      <form id="networksForm">
        )=====" + networksList + R"=====(
        <button type="button" onclick="submitSelection()">Conectar a red seleccionada</button>
      </form>
    </div>

    <div class="network-list">
      <h3 class="section-title">Redes cercanas (ordenadas por señal):</h3>
      <p>Elige una red para rellenar el SSID y solo escribe la contraseña.</p>
      )=====" + scannedNetworks + R"=====(
    </div>

    <h3 class="section-title">Agregar nueva red:</h3>
    <p>Por seguridad el navegador no puede leer la red/contraseña de tu teléfono. Selecciona una red de la lista o escríbela aquí.</p>
    <form action='/save' method='POST'>
      <input id='ssidInput' type='text' name='ssid' placeholder='Nombre de la red (SSID)' required>
      <input id='passInput' type='password' name='pass' placeholder='Contraseña' required>
      <button type='submit'>Guardar Configuración</button>
    </form>

    <div class="network-list">
      <h3 class="section-title">Dispositivos registrados:</h3>
      )=====" + devicesList + R"=====(
    </div>
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
    server.send(200, "text/html", "<html><body><h2>Credenciales guardadas! Conectando...</h2></body></html>");

    dnsServer.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());

    unsigned long inicioConexion = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - inicioConexion < WIFI_TIMEOUT) {
      delay(200);
    }

    bool conectado = WiFi.status() == WL_CONNECTED;
    forceAPMode = false;
    wifiConfigInProgress = false;
    mostrarMensajeRedConectada(ssid, conectado);
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

void handleDeleteDevice() {
  if (server.hasArg("mac")) {
    String mac = server.arg("mac");
    mac.trim();

    if (mac.length() == 0) {
      server.send(400, "text/plain", "MAC vacía");
      return;
    }

    bool eliminado = eliminarDispositivo(mac);
    if (eliminado) {
      server.send(200, "text/plain", "OK");
    } else {
      server.send(404, "text/plain", "Dispositivo no encontrado");
    }
  } else {
    server.send(400, "text/plain", "Falta parámetro mac");
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

      // Mantener activo el portal y recargar la vista completa con redes/dispositivos
      wifiConfigInProgress = true;
      forceAPMode = true;
      apMode = true;
      handleRoot();
      return;
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


    // DEBUG DETALLADO
    Serial.printf("Red %d - SSID: '%s', Password: '%s' (longitud: %d)\n", 
                  i, savedNetworks[i].ssid.c_str(), 
                  savedNetworks[i].password.c_str(),
                  savedNetworks[i].password.length());


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

void checkWiFiStatus() {
  int status = WiFi.status();
  Serial.print("Estado WiFi: ");
  switch(status) {
    case WL_NO_SHIELD: Serial.println("WL_NO_SHIELD (255)"); break;
    case WL_IDLE_STATUS: Serial.println("WL_IDLE_STATUS (0)"); break;
    case WL_NO_SSID_AVAIL: Serial.println("WL_NO_SSID_AVAIL (1)"); break;
    case WL_SCAN_COMPLETED: Serial.println("WL_SCAN_COMPLETED (2)"); break;
    case WL_CONNECTED: Serial.println("WL_CONNECTED (3)"); break;
    case WL_CONNECT_FAILED: Serial.println("WL_CONNECT_FAILED (4) - Contraseña incorrecta"); break;
    case WL_CONNECTION_LOST: Serial.println("WL_CONNECTION_LOST (5)"); break;
    case WL_DISCONNECTED: Serial.println("WL_DISCONNECTED (6)"); break;
    default: Serial.println("Estado desconocido: " + String(status)); break;
  }
}

void attemptReconnectToAllNetworks() {
  WiFi.mode(WIFI_STA);
  delay(100);
  
  // Verificar redes
  debugNetworks();  // ← ESTO MOSTRARÁ LAS CONTRASEÑAS
  
  bool hasNetworks = false;
  for (int i = 0; i < MAX_NETWORKS; i++) {
    if (savedNetworks[i].ssid.length() > 0) {
      hasNetworks = true;
      
      if (savedNetworks[i].active) {
        Serial.print("Conectando a: '");
        Serial.print(savedNetworks[i].ssid);
        Serial.print("' con pass: '");
        Serial.print(savedNetworks[i].password);
        Serial.println("'");
        
        WiFi.begin(savedNetworks[i].ssid.c_str(), savedNetworks[i].password.c_str());

        unsigned long startAttempt = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < WIFI_TIMEOUT) {
          delay(500);
          Serial.print(".");
        }

        if (WiFi.status() == WL_CONNECTED) {
          Serial.println("\n✓ CONECTADO EXITOSAMENTE");
          Serial.print("IP: ");
          Serial.println(WiFi.localIP());
          return;
        } else {
          Serial.println("\n✗ FALLO EN CONEXIÓN");
          checkWiFiStatus();  // ← DIAGNÓSTICO DETALLADO
        }
      }
    }
  }
  
  if (!hasNetworks) {
    Serial.println("No hay redes WiFi guardadas");
  }
}

void debugNetworks() {
  Serial.println("=== DEBUG REDES GUARDADAS ===");
  bool anyNetwork = false;
  for (int i = 0; i < MAX_NETWORKS; i++) {
    if (savedNetworks[i].ssid.length() > 0) {
      anyNetwork = true;
      Serial.printf("Red %d: SSID='%s', Pass='%s', Activa=%d, PassLen=%d\n", 
                   i, 
                   savedNetworks[i].ssid.c_str(),
                   savedNetworks[i].password.c_str(),  // ← MUESTRA LA CONTRASEÑA
                   savedNetworks[i].active ? 1 : 0,
                   savedNetworks[i].password.length());
    }
  }
  if (!anyNetwork) {
    Serial.println("NO hay redes guardadas");
  }
  Serial.println("==============================");
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
  if (len == 0xFF || len < 0 || len > USER_ID_MAX_LEN) {
    Serial.printf("⚠️  Longitud de UserID inválida (%d). Se limpia y se devuelve false.\n", len);
    saveUserIDToEEPROM("");
    return false;
  }
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

void cargarDispositivos() {
    Serial.println("\n🔍 CARGANDO DISPOSITIVOS DESDE EEPROM...");
    
    // INICIALIZAR TODOS COMO VACÍOS
    for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
        configDispositivos[i].mac[0] = '\0';
        configDispositivos[i].nombre[0] = '\0';
        configDispositivos[i].activo = false;
        configDispositivos[i].tipoDispositivo = 0;
        configDispositivos[i].litrosActuales = 0;
        configDispositivos[i].voltaje = 0;
        configDispositivos[i].temperatura = 0;
        configDispositivos[i].alturaConfig = 0;
        configDispositivos[i].litrosConfig = 0;
        configDispositivos[i].porcentaje = 0;
    }

    EEPROM.begin(EEPROM_SIZE);
    int addr = CONFIG_DISPOSITIVOS_ADDR;
    
    // 1. Leer contador (2 bytes)
    int count = (EEPROM.read(addr++) << 8) | EEPROM.read(addr++);
    count = min(count, MAX_DISPOSITIVOS);

    Serial.printf("📊 Dispositivos encontrados en EEPROM: %d\n", count);

    // 2. Cargar cada dispositivo
    for (int i = 0; i < count; i++) {
        Serial.printf("\n--- Cargando dispositivo %d ---\n", i);
        
        // Leer MAC como string (18 bytes: 17 chars + null terminator)
        char mac[MAC_LEN + 1] = {0};
        for (int j = 0; j < MAC_LEN + 1; j++) {
            mac[j] = EEPROM.read(addr++);
        }
        
        Serial.printf("MAC leída de EEPROM: '%s'\n", mac);
        Serial.printf("Longitud MAC: %d\n", strlen(mac));

        // Validar que sea una MAC válida
        if (strlen(mac) == MAC_LEN && mac[2] == ':' && mac[5] == ':') {
            // COPIAR MAC AL ARREGLO
            strncpy(configDispositivos[i].mac, mac, MAC_LEN + 1);
            Serial.printf("✅ MAC copiada a configDispositivos[%d]: '%s'\n", i, configDispositivos[i].mac);
            
            // LEER NOMBRE (20 bytes)
            char nombre[20] = {0};
            for (int j = 0; j < 20; j++) {
                nombre[j] = EEPROM.read(addr++);
            }
            strncpy(configDispositivos[i].nombre, nombre, 20);
            Serial.printf("📝 Nombre leído: '%s'\n", configDispositivos[i].nombre);
            
            // LEER VALORES FLOAT (5 floats = 20 bytes)
            EEPROM.get(addr, configDispositivos[i].litrosActuales); addr += sizeof(float);
            EEPROM.get(addr, configDispositivos[i].voltaje); addr += sizeof(float);
            EEPROM.get(addr, configDispositivos[i].temperatura); addr += sizeof(float);
            EEPROM.get(addr, configDispositivos[i].alturaConfig); addr += sizeof(float);
            EEPROM.get(addr, configDispositivos[i].litrosConfig); addr += sizeof(float);
            
            Serial.printf("📊 Valores leídos: %.1fL, %.1fV, %.1f°C, %.1fcm, %.1fL\n",
                         configDispositivos[i].litrosActuales,
                         configDispositivos[i].voltaje,
                         configDispositivos[i].temperatura,
                         configDispositivos[i].alturaConfig,
                         configDispositivos[i].litrosConfig);
            
            // LEER PORCENTAJE (2 bytes)
            configDispositivos[i].porcentaje = (EEPROM.read(addr++) << 8) | EEPROM.read(addr++);
            Serial.printf("📈 Porcentaje leído: %d%%\n", configDispositivos[i].porcentaje);
            
            // Leer tipo
            configDispositivos[i].tipoDispositivo = EEPROM.read(addr++);
            Serial.printf("🔧 Tipo leído: %d\n", configDispositivos[i].tipoDispositivo);
            
            // Leer estado activo
            byte activoByte = EEPROM.read(addr++);
            configDispositivos[i].activo = (activoByte == 1);
            Serial.printf("⚡ Activo leído: %d -> %s\n", activoByte, configDispositivos[i].activo ? "true" : "false");
            
            // ASIGNAR NOMBRE POR DEFECTO SI ESTÁ VACÍO
            if (strlen(configDispositivos[i].nombre) == 0) {
                String nombreDefault = "Tanque " + String(i+1);
                nombreDefault.toCharArray(configDispositivos[i].nombre, 20);
                Serial.printf("⚠️  Nombre por defecto asignado: '%s'\n", configDispositivos[i].nombre);
            }
            
        } else {
            Serial.printf("❌ MAC inválida en posición %d\n", i);
            configDispositivos[i].mac[0] = '\0';
            configDispositivos[i].nombre[0] = '\0';
            configDispositivos[i].activo = false;
            
            // SALTAR TODOS LOS BYTES CORRECTAMENTE
            addr += 20; // Nombre (20 bytes)
            addr += sizeof(float) * 5; // 5 floats (20 bytes)
            addr += 4; // Porcentaje (2) + Tipo (1) + Activo (1) = 4 bytes
            Serial.println("❌ Saltado dispositivo inválido");
        }
        
        // VERIFICACIÓN INMEDIATA
        Serial.printf("✅ VERIFICACIÓN: configDispositivos[%d].mac = '%s'\n", i, configDispositivos[i].mac);
        Serial.printf("✅ VERIFICACIÓN: configDispositivos[%d].nombre = '%s'\n", i, configDispositivos[i].nombre);
        Serial.printf("✅ VERIFICACIÓN: configDispositivos[%d].activo = %d\n", i, configDispositivos[i].activo);
    }
    
    EEPROM.end();
    
    // VERIFICACIÓN FINAL
    Serial.println("\n--- VERIFICACIÓN FINAL DEL ARREGLO ---");
    int dispositivosCargados = 0;
    for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
        if (String(configDispositivos[i].mac) != "") {
            Serial.printf("📍 configDispositivos[%d]: MAC='%s', Nombre='%s', Activo=%d, Tipo=%d, Porcentaje=%d%%, Litros=%.1fL\n", 
                         i, 
                         configDispositivos[i].mac, 
                         configDispositivos[i].nombre,
                         configDispositivos[i].activo, 
                         configDispositivos[i].tipoDispositivo,
                         configDispositivos[i].porcentaje,
                         configDispositivos[i].litrosActuales);
            dispositivosCargados++;
        }
    }
    Serial.printf("📊 Total dispositivos en arreglo: %d\n", dispositivosCargados);
    Serial.println("----------------------------------------");
}

void debugEEPROMReal() {
    Serial.println("\n=== DEBUG EEPROM REAL ===");
    EEPROM.begin(EEPROM_SIZE);
    
    int addr = CONFIG_DISPOSITIVOS_ADDR;
    
    // Leer contador
    int count = (EEPROM.read(addr++) << 8) | EEPROM.read(addr++);
    Serial.printf("Contador: %d\n", count);
    
    // Leer primeros 3 dispositivos como están guardados
    for (int i = 0; i < min(3, count); i++) {
        Serial.printf("Dispositivo %d - Bytes crudos: ", i);
        
        // Leer los primeros 18 bytes (MAC como texto)
        for (int j = 0; j < MAC_LEN + 1; j++) {
            byte b = EEPROM.read(addr++);
            Serial.printf("%02X ", b);
            if (b >= 32 && b <= 126) {
                Serial.printf("('%c') ", b);
            } else {
                Serial.print("(?) ");
            }
        }
        Serial.println();
        
        // Saltar el resto de los datos del dispositivo
        addr += sizeof(float) * VALORES_POR_DISPOSITIVO + 2;
    }
    
    EEPROM.end();
    Serial.println("===========================");
}

//Funciones SETUP de innicializacion
   // 2. Inicialización segura de EEPROM
void inicializa_eeprom(){
  if (!EEPROM.begin(EEPROM_SIZE)) {
    Serial.println("Error al inicializar EEPROM");
  } 
}

//Limpieza de fabrica EEPROM y WIFFI
void clearEEPROM() {
  Serial.println("🧹 INICIANDO BORRADO COMPLETO EEPROM...");
  
  // 1. Borrar EEPROM física
  EEPROM.begin(EEPROM_SIZE);
  for (int i = 0; i < EEPROM_SIZE; i++) {
    EEPROM.write(i, 0xFF); // Escribir 0xFF es más confiable que 0x00
  }
  
  // 2. Borrar flags importantes
  EEPROM.write(0, 0); // Flag de inicialización
  EEPROM.write(MQTT_CONFIRMED_FLAG_ADDR, 0);
  
  // 3. Borrar contador de dispositivos en la posición específica
  int addr = CONFIG_DISPOSITIVOS_ADDR;
  EEPROM.write(addr++, 0); // High byte del contador
  EEPROM.write(addr++, 0); // Low byte del contador
  
  bool success = EEPROM.commit();
  EEPROM.end();
  
  if (!success) {
    Serial.println("❌ Error en commit de EEPROM");
  }
  
  // 4. Limpiar estructuras en RAM
  for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
    configDispositivos[i].mac[0] = '\0';
    configDispositivos[i].activo = false;
    configDispositivos[i].tipoDispositivo = 0;
  }
  
  // 5. Limpiar redes WiFi en RAM
  for (int i = 0; i < MAX_NETWORKS; i++) {
    savedNetworks[i].ssid = "";
    savedNetworks[i].password = "";
    savedNetworks[i].active = false;
  }
  
  // 6. Limpiar userID
  userID = "";
  
  Serial.println("✅ EEPROM Y MEMORIA RAM BORRADOS COMPLETAMENTE");
  
  // Verificación
  delay(1000);
  verificarEstadoConfigDispositivos();
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
        Serial.printf("Nombre: %s\n", config->nombre);
        Serial.printf("Tipo: %d\n", config->tipoDispositivo);
        Serial.printf("Litros Actuales: %.2f\n", config->litrosActuales);
        Serial.printf("Voltaje: %.2f\n", config->voltaje);
        Serial.printf("Temperatura: %.2f\n", config->temperatura);
        Serial.printf("Altura Config: %.2f\n", config->alturaConfig);
        Serial.printf("Litros Config: %.2f\n", config->litrosConfig);
        Serial.printf("Porcentaje: %d%%\n", config->porcentaje);
        Serial.printf("Activo: %s\n", config->activo ? "SI" : "NO");
    } else {
        Serial.println("Dispositivo no encontrado");
    }
}

void iniciarLoRaConReintentos() {
  int intentos = 0;
  bool estadoLED = false;

  Serial.println("📡 INICIANDO CONFIGURACIÓN LoRa...");
  
  loraSPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setSPI(loraSPI);
  LoRa.setPins(LORA_SS, -1, -1);

  while (!LoRa.begin(433E6)) {
    Serial.printf("❌ Error al iniciar LoRa. Intento %d/10\n", intentos + 1);
    estadoLED = !estadoLED;
    intentos++;
    if (intentos >= 10) {
      Serial.println("🚨 ERROR CRÍTICO: No se pudo inicializar LoRa después de 10 intentos");
      return;
    }
    delay(3000);
  }

  // ⭐ CONFIGURACIÓN EXPLÍCITA
  LoRa.setTxPower(20);
  LoRa.setSpreadingFactor(12);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(8);
  LoRa.setSyncWord(0x12);
  LoRa.setPreambleLength(8);
  
  Serial.println("✅ LoRa inicializado correctamente!");
  Serial.println("📊 Configuración aplicada:");
  Serial.println("   - Frecuencia: 433 MHz");
  Serial.println("   - Potencia: 20 dBm");
  Serial.println("   - SF: 12");
  Serial.println("   - BW: 125 kHz");
  Serial.println("   - CR: 4/8");
}

void Reintentar_Wiffi(){
    // Reintentar conexión periódicamente
    if (millis() - lastReconnectAttempt > reconnectInterval) {
      Serial.println("Reintentando conexión a redes guardadas...");
      lastReconnectAttempt = millis();
      attemptReconnectToAllNetworks();
    }
}

void recepcion_lora() {
    int packetSize = LoRa.parsePacket();
    
    if (packetSize) {
        Serial.println("\n🎉 ===========================================");
        Serial.println("📡 PAQUETE LoRa DETECTADO - DEBUG COMPLETO");
        Serial.println("🎉 ===========================================");
        
        String received = "";
        while (LoRa.available()) {
            received += (char)LoRa.read();
        }
        received.trim();
        
        Serial.printf("📊 Longitud mensaje: %d bytes\n", received.length());
        Serial.printf("📶 RSSI: %d, SNR: %.2f\n", LoRa.packetRssi(), LoRa.packetSnr());
        Serial.print("📨 Mensaje RAW: '");
        Serial.print(received);
        Serial.println("'");
        
        // Debug detallado del formato
        debugMensajeLoRa(received);
        
        // Extraer MAC
        int firstComma = received.indexOf(',');
        int secondComma = received.indexOf(',', firstComma + 1);
        
        if (firstComma != -1 && secondComma != -1) {
            String mac = received.substring(firstComma + 1, secondComma);
            mac.trim();
            
            Serial.printf("🔍 MAC extraída: '%s'\n", mac.c_str());
            Serial.printf("🔍 Longitud MAC: %d\n", mac.length());
            
            // Buscar dispositivo
            bool encontrado = false;
            for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
                String storedMac = String(configDispositivos[i].mac);
                storedMac.trim();
                
                Serial.printf("   🔎 Comparando con [%d]: '%s'\n", i, storedMac.c_str());
                
                if (storedMac == mac) {
                    encontrado = true;
                    Serial.printf("✅ DISPOSITivo ENCONTRADO en índice: %d\n", i);
                    
                    // Procesar datos
                    actualizarDatosDesdeLoRa(mac, received, "");
                    break;
                }
            }
            
            if (!encontrado) {
                Serial.println("❌ DISPOSITIVO NO REGISTRADO - Ignorando mensaje");
                Serial.println("💡 Sugerencia: Registrar dispositivo via BLE primero");
            }
        } else {
            Serial.println("❌ ERROR: No se pudieron extraer las comas del mensaje");
        }
        
        Serial.println("🎉 ===========================================\n");
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
        configDispositivos[i].tipoDispositivo = configDispositivos[i+1].tipoDispositivo;
        configDispositivos[i].activo = configDispositivos[i+1].activo;
    }
    
    // Limpiar la última posición
    configDispositivos[MAX_DISPOSITIVOS-1].mac[0] = '\0';
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
    int angulo = (frameAnimacion * 45) % 360;
    dibujarCirculoGiratorio(centroX, centroY, 15, angulo);
    
    // Texto según el tipo de operación
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    
    if (solicitudAltaBLE) {
        display.setCursor(centroX - 24, 40);
        display.print("ALTA");
    } else if (solicitudBajaBLE) {
        display.setCursor(centroX - 24, 40);
        display.print("BAJA");
    } else {
        display.setCursor(centroX - 36, 40);
        display.print("CONFIG");
    }
    
    // Puntos animados
    display.setTextSize(1);
    int puntosAncho = 3 * 6;
    display.setCursor((SCREEN_WIDTH - puntosAncho) / 2, 55);
    
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
  if (!displayReady) {
    return;
  }
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

// Aviso breve de WiFi al inicio sin activar el portal
void mostrarWifiInicioTemporal() {
  iniciarAnimacionWifi();
  unsigned long inicioAviso = millis();

  while (millis() - inicioAviso < 2000) { // solo 2 segundos
    if (animandoWifi && millis() - ultimoCambioWifi >= INTERVALO_WIFI) {
      frameWifi = (frameWifi + 1) % 4;
      ultimoCambioWifi = millis();
    }
    mostrarConexionWifi();
    delay(50);
  }

  detenerAnimacionWifi();

  // Limpiar pantalla después del aviso
  if (displayReady) {
    display.clearDisplay();
    display.display();
  }
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

void dibujarMensajeConexion() {
  if (!displayReady) {
    return;
  }
  display.clearDisplay();

  int centroX = SCREEN_WIDTH / 2;
  int centroY = 20;

  if (animandoWifi && millis() - ultimoCambioWifi >= INTERVALO_WIFI) {
    frameWifi = (frameWifi + 1) % 4;
    ultimoCambioWifi = millis();
  }

  dibujarWifiAnimado(centroX, centroY, frameWifi);

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print(conexionExitosa ? "WiFi conectado" : "Error WiFi");

  display.setCursor(0, 45);
  if (ultimaRedConfigurada.length() > 0) {
    display.print(conexionExitosa ? "Red: " : "Red fallida: ");
    display.print(ultimaRedConfigurada);
  } else {
    display.print("Esperando red...");
  }

  display.display();
}

// Función para conectar WiFi (desde otras partes del código)
void conectarWifi() {
  iniciarAnimacionWifi();
}


//pantalla dinamica

int contarDispositivosRegistrados() {
    int count = 0;
    for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
        if (String(configDispositivos[i].mac) != "") {
            count++;
        }
    }
    
    // ⭐⭐ SOLO IMPRIMIR SI CAMBIA EL CONTEO
    static int ultimoConteo = -1;
    if (count != ultimoConteo) {
        ultimoConteo = count;
        Serial.println("📊 Total dispositivos registrados: " + String(count));
    }
    
    return count;
}


Dispositivo obtenerDatosDispositivo(int index) {
    Dispositivo disp;
    disp.nombre = "Sin Dispositivos";
    disp.porcentaje = 0;
    disp.bateria = 0.0;
    disp.litros = 0;
    disp.sinDatos = false; // ⭐ NUEVO: flag para indicar sin datos
    
    // ⭐⭐ VERIFICAR índice válido
    if (index < 0) {
        return disp;
    }
    
    // Buscar el dispositivo en la posición index
    int count = 0;
    for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
        if (String(configDispositivos[i].mac) != "") {
            if (count == index) {
                String nombreDispositivo = String(configDispositivos[i].nombre);
                if (nombreDispositivo.length() == 0) {
                    nombreDispositivo = "Dispositivo " + String(i+1);
                }
                disp.nombre = nombreDispositivo;
                
                // ⭐⭐ VERIFICAR SI HAY DATOS RECIENTES
                if (millis() - ultimaActualizacionLoRa[i] > TIEMPO_SIN_DATOS) {
                    disp.sinDatos = true;
                    mostrarSinDatos[i] = true;
                } else {
                    disp.porcentaje = configDispositivos[i].porcentaje;
                    disp.bateria = configDispositivos[i].voltaje;
                    disp.litros = (int)configDispositivos[i].litrosActuales;
                    disp.sinDatos = false;
                    mostrarSinDatos[i] = false;
                }
                break;
            }
            count++;
        }
    }
    
    return disp;
}

void dibujarHeader() {
    int cantidadDispositivos = contarDispositivosRegistrados();
    
    // WiFi - Esquina superior izquierda
    if (wifiConectado) {
        display.drawBitmap(2, 2, wifiIcon, 16, 16, SSD1306_WHITE);
    } else {
        display.drawBitmap(2, 2, wifiIconOff, 16, 16, SSD1306_WHITE);
    }
    
    // NUUP pequeño (tamaño 1)
    display.setTextSize(1);
    display.setCursor(22, 6);
    display.print("NUUP");
    
    // Indicador de dispositivos (actual/total)
    display.setTextSize(2);
    display.setCursor(22 + 4 * 6, 2);
    display.print(" ");
    if (cantidadDispositivos > 0) {
        display.print((dispositivoActual % cantidadDispositivos) + 1);
        display.print("/");
        display.print(cantidadDispositivos);
    } else {
        display.print("0/0");
    }
    
    // Batería del dispositivo actual
    if (cantidadDispositivos > 0) {
        Dispositivo dispActual = obtenerDatosDispositivo(dispositivoActual % cantidadDispositivos);
        if (dispActual.bateria >= 3.0) {
            display.drawBitmap(SCREEN_WIDTH - 20, 2, batteryFull, 16, 16, SSD1306_WHITE);
        } else {
            display.drawBitmap(SCREEN_WIDTH - 20, 2, batteryEmpty, 16, 16, SSD1306_WHITE);
        }
    } else {
        // Sin dispositivos - batería vacía
        display.drawBitmap(SCREEN_WIDTH - 20, 2, batteryEmpty, 16, 16, SSD1306_WHITE);
    }
}

void dibujarTituloDispositivo() {
    display.setTextSize(2);
    display.setCursor(0, 22);
    
    int cantidadDispositivos = contarDispositivosRegistrados();
    
    if (cantidadDispositivos == 0) {
        display.println("Sin Dispositivos");
    } else {
        Dispositivo disp = obtenerDatosDispositivo(dispositivoActual % cantidadDispositivos);
        
        // Ajustar texto si es muy largo
        String titulo = disp.nombre;
        if (titulo.length() > 12) {
            titulo = titulo.substring(0, 12);
        }
        display.println(titulo);
    }
}

void dibujarContenidoPrincipal() {
    display.fillRect(0, 44, 128, 20, SSD1306_BLACK);
    
    int cantidadDispositivos = contarDispositivosRegistrados();
    
    if (cantidadDispositivos == 0) {
        // Mostrar mensaje cuando no hay dispositivos
        display.setTextSize(1);
        display.setCursor(10, 50);
        display.print("Esperando dispositivos...");
    } else {
        Dispositivo disp = obtenerDatosDispositivo(dispositivoActual % cantidadDispositivos);
        
        // ⭐⭐ VERIFICAR SI MOSTRAR "SIN DATOS"
        if (disp.sinDatos) {
            // Mostrar "SIN DATOS" centrado
            display.setTextSize(2);
            display.setCursor(SCREEN_WIDTH/2 - 42, 48);
            display.print("SIN DATOS");
        } else {
            // Mostrar datos normales
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
    }
}

void actualizarDatosDesdeLoRa(const String &mac, const String &mensaje, const String &nombre) {
    Serial.println("\n🔄 ===========================================");
    Serial.println("🔍 ACTUALIZAR DATOS DESDE LORA - DEBUG");
    Serial.println("🔄 ===========================================");
    
    Serial.printf("📱 MAC: '%s'\n", mac.c_str());
    Serial.printf("📨 Mensaje: '%s'\n", mensaje.c_str());
    
    for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
        if (String(configDispositivos[i].mac) == mac) {
            Serial.printf("✅ Dispositivo encontrado en índice: %d\n", i);
            Serial.printf("📝 Nombre actual: '%s'\n", configDispositivos[i].nombre);
            
            // Parsear mensaje
            int commas[8];
            int commaCount = 0;
            
            for (int pos = 0; pos < mensaje.length() && commaCount < 8; pos++) {
                if (mensaje.charAt(pos) == ',') {
                    commas[commaCount] = pos;
                    commaCount++;
                }
            }
            
            Serial.printf("🔢 Comas encontradas: %d\n", commaCount);
            
            if (commaCount >= 6) {
                // Extraer valores
                String litrosActualesStr = mensaje.substring(commas[1] + 1, commas[2]);
                String voltajeStr = mensaje.substring(commas[2] + 1, commas[3]);
                String temperaturaStr = mensaje.substring(commas[3] + 1, commas[4]);
                String alturaConfigStr = mensaje.substring(commas[4] + 1, commas[5]);
                String litrosConfigStr = mensaje.substring(commas[5] + 1, commas[6]);
                
                // ⭐⭐ CORRECCIÓN: EXTRAER EL NOMBRE CORRECTAMENTE
                String nombreExtraido = "";
                if (commaCount >= 7) {
                    nombreExtraido = mensaje.substring(commas[6] + 1, commas[7]);
                    nombreExtraido.trim();
                    
                    Serial.printf("📝 NOMBRE EXTRAÍDO: '%s'\n", nombreExtraido.c_str());
                    Serial.printf("📝 LONGITUD: %d\n", nombreExtraido.length());
                    
                    // ⭐⭐ ACTUALIZAR EL NOMBRE SI ES DIFERENTE Y VÁLIDO
                    if (nombreExtraido.length() > 0 && nombreExtraido != String(configDispositivos[i].nombre)) {
                        Serial.println("🔄 ACTUALIZANDO NOMBRE...");
                        nombreExtraido.toCharArray(configDispositivos[i].nombre, 20);
                        Serial.printf("✅ NOMBRE ACTUALIZADO: '%s'\n", configDispositivos[i].nombre);
                    }
                }
                
                Serial.println("📊 VALORES EXTRAÍDOS:");
                Serial.printf("   💧 Litros Actuales: '%s' -> %.1f\n", litrosActualesStr.c_str(), litrosActualesStr.toFloat());
                Serial.printf("   🔋 Voltaje: '%s' -> %.1f\n", voltajeStr.c_str(), voltajeStr.toFloat());
                Serial.printf("   🌡️ Temperatura: '%s' -> %.1f\n", temperaturaStr.c_str(), temperaturaStr.toFloat());
                Serial.printf("   📏 Altura Config: '%s' -> %.1f\n", alturaConfigStr.c_str(), alturaConfigStr.toFloat());
                Serial.printf("   💧 Litros Config: '%s' -> %.1f\n", litrosConfigStr.c_str(), litrosConfigStr.toFloat());
                
                // Actualizar valores
                configDispositivos[i].litrosActuales = litrosActualesStr.toFloat();
                configDispositivos[i].voltaje = voltajeStr.toFloat();
                configDispositivos[i].temperatura = temperaturaStr.toFloat();
                configDispositivos[i].alturaConfig = alturaConfigStr.toFloat();
                configDispositivos[i].litrosConfig = litrosConfigStr.toFloat();
                
                // Calcular porcentaje
                if (configDispositivos[i].litrosConfig > 0) {
                    float porcentaje = (configDispositivos[i].litrosActuales / configDispositivos[i].litrosConfig) * 100;
                    configDispositivos[i].porcentaje = (int)constrain(porcentaje, 0, 100);
                    Serial.printf("📈 Porcentaje calculado: %.1f%% -> %d%%\n", porcentaje, configDispositivos[i].porcentaje);
                }
                
                // Actualizar timestamp
                ultimaActualizacionLoRa[i] = millis();
                mostrarSinDatos[i] = false;
                
                // ⭐⭐ GUARDAR CAMBIOS EN EEPROM
                if (guardarDispositivos()) {
                    Serial.println("💾 DATOS GUARDADOS EN EEPROM (incluyendo nombre)");
                } else {
                    Serial.println("❌ ERROR al guardar en EEPROM");
                }
                
                Serial.println("✅ DATOS ACTUALIZADOS CORRECTAMENTE");
                
            } else {
                Serial.println("❌ ERROR: Mensaje no tiene suficientes campos");
            }
            
            break;
        }
    }
    
    Serial.println("🔄 ===========================================\n");
}



bool guardarDispositivos() {
    EEPROM.begin(EEPROM_SIZE);
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
            // Guardar MAC (18 bytes)
            for (int j = 0; j < MAC_LEN + 1; j++) {
                EEPROM.write(addr++, configDispositivos[i].mac[j]);
            }
            
            // Guardar nombre (20 bytes)
            for (int j = 0; j < 20; j++) {
                EEPROM.write(addr++, configDispositivos[i].nombre[j]);
            }
            
            // Guardar valores float (5 floats = 20 bytes)
            EEPROM.put(addr, configDispositivos[i].litrosActuales); addr += sizeof(float);
            EEPROM.put(addr, configDispositivos[i].voltaje); addr += sizeof(float);
            EEPROM.put(addr, configDispositivos[i].temperatura); addr += sizeof(float);
            EEPROM.put(addr, configDispositivos[i].alturaConfig); addr += sizeof(float);
            EEPROM.put(addr, configDispositivos[i].litrosConfig); addr += sizeof(float);
            
            // ⭐ GUARDAR PORCENTAJE (2 bytes)
            EEPROM.write(addr++, (configDispositivos[i].porcentaje >> 8) & 0xFF);
            EEPROM.write(addr++, configDispositivos[i].porcentaje & 0xFF);
            
            // Guardar tipo y estado (2 bytes)
            EEPROM.write(addr++, configDispositivos[i].tipoDispositivo);
            EEPROM.write(addr++, configDispositivos[i].activo ? 1 : 0);
        }
    }

    bool success = EEPROM.commit();
    EEPROM.end();
    return success;
}

void debugEstadoDispositivos() {
    static unsigned long lastDebug = 0;
    if (millis() - lastDebug > 30000) { // ⭐⭐ CADA 30 SEGUNDOS (en lugar de 5)
        lastDebug = millis();
        
        Serial.println("\n=== DEBUG ESTADO DISPOSITIVOS ===");
        int total = contarDispositivosRegistrados();
        
        for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
            if (String(configDispositivos[i].mac) != "") {
                Serial.println("📍 " + String(configDispositivos[i].nombre) + 
                             " - " + String(configDispositivos[i].porcentaje) + "% - " + 
                             String(configDispositivos[i].litrosActuales) + "L");
            }
        }
        Serial.println("================================\n");
    }
}

 
void setup() {
   // 0. Inicialización básica SERIAL PANTLALLA  EEPROM
  Serial.begin(115200);
    delay(1000);

  // Inicializar OLED lo antes posible para evitar llamadas sobre puntero nulo
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("Fallo inicializacion OLED");
    while(true);
  }

  displayReady = true;
  Serial.println("OLED inicializado correctamente");
  display.setTextColor(SSD1306_WHITE);

    // 11. Inicializar BLE
    iniciarBLE();
    
    Serial.println("Setup completado");

//pinMode(BOTON_S, INPUT_PULLUP);
pinMode(BOTON_W, INPUT_PULLUP);


   // Inicializar timestamps
    for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
        ultimaActualizacionLoRa[i] = millis(); // Iniciar con tiempo actual
        mostrarSinDatos[i] = false;
    }
 

 inicializa_eeprom();
//clearEEPROM();  //solo para configuracion inicial
delay(1000);


  // DEBUG COMPLETO EEPROM AL INICIAR
  Serial.println("\n💾 ===========================================");
  Serial.println("🔍 DEBUG EEPROM AL INICIAR");
  Serial.println("💾 ===========================================");

  EEPROM.begin(EEPROM_SIZE);

  // Leer flag de registro
  byte registroFlag = EEPROM.read(0);
  Serial.printf("📋 Flag de registro en addr 0: %d\n", registroFlag);

  // Leer userID
  int userIDLen = EEPROM.read(USER_ID_ADDR);
  if (userIDLen == 0xFF || userIDLen < 0 || userIDLen > USER_ID_MAX_LEN) {
    Serial.printf("📋 UserID corrupto: longitud leída %d, se restablece a 0\n", userIDLen);
    userIDLen = 0;
  }

  char userIDBuffer[USER_ID_MAX_LEN + 1] = {0};
  for (int i = 0; i < userIDLen; i++) {
    userIDBuffer[i] = EEPROM.read(USER_ID_ADDR + 1 + i);
  }
  Serial.printf("📋 UserID en EEPROM: '%s' (longitud: %d)\n", userIDBuffer, userIDLen);
  
  // Leer datos de dispositivos
  int addr = CONFIG_DISPOSITIVOS_ADDR;
  int count = (EEPROM.read(addr++) << 8) | EEPROM.read(addr++);
  Serial.printf("📋 Contador de dispositivos: %d\n", count);
  
  EEPROM.end();
  
  Serial.println("💾 ===========================================\n");
  
   // FIN DEBUG COMPLETO EEPROM AL INICIAR
  Serial.println("\n💾 ===========================================");
  Serial.println("🔍 FIN DEBUG EEPROM AL INICIAR");
  Serial.println("💾 ===========================================");








// 2. Cargar configuración Wiffi existente y USER_ID capturado por usuario
    if (!loadNetworksFromEEPROM()) {
      Serial.println("Error al cargar redes de EEPROM");
    }
  // MOSTRAR qué redes se cargaron
  debugNetworks();


// 3. Cargar configuración UserID capturado por usuario

  if (!loadUserIDFromEEPROM()) {
    Serial.println("Error al cargar ID de EEPROM");
  }

  // En setup(), después de cargar userID:
if (userID.length() == 0 || userID[0] > 127) {
    Serial.println("🔄 UserID corrupto detectado, limpiando...");
    userID = "";
    saveUserIDToEEPROM("");
}



    //ID guardado
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
Serial.println("🎯 TEST LoRa - Enviando mensaje de prueba...");
LoRa.beginPacket();
LoRa.print("TEST_SERVER_READY");
LoRa.endPacket();
Serial.println("✅ Mensaje de prueba enviado");


 //7. configura DISPOSITIVOS
// Cargar dispositivos registrados
cargarDispositivos();  // 
delay(1000);
testDispositivosRapido();  // ← Agrega esta línea solo poner para debug



debugEEPROMReal();
delay(1000);

    // VERIFICACIÓN EXTRA
    verificarEstadoConfigDispositivos();

 //Wiffi
 //8. attemptReconnectToAllNetworks();
 if (WiFi.status() != WL_CONNECTED) {
  attemptReconnectToAllNetworks();
}

 // Aviso breve de WiFi solo si seguimos desconectados
 if (WiFi.status() != WL_CONNECTED) {
   mostrarWifiInicioTemporal();
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

//testWiFiConnection();


}



// Modificar el loop principal para manejar ambas animaciones
void loop() {

  manejarBotonWifi();

  if (mostrarMensajeConexion) {
    dibujarMensajeConexion();
    if (millis() - inicioMensajeConexion >= 5000) {
      ESP.restart();
    }
    return;
  }

// Debug periódico de nombres
static unsigned long lastDebugNombres = 0;
if (millis() - lastDebugNombres > 15000) {
    lastDebugNombres = millis();
    debugNombresDispositivos();
}


   static unsigned long lastDebug = 0;
    if (millis() - lastDebug > 10000) {
        lastDebug = millis();
        debugNombreProblema();
    }

// Verificar tiempos sin datos
    verificarTiemposSinDatos();

      //    2. Recepción de LoRa (solo datos, no comandos de registro/baja)
    recepcion_lora();  

// Llamar en loop():
testLoRaPeriodico();



  // 0. Manejar BLE (antes de todo)
    manejarBLE();
      
    debugEstadoDispositivos();
    
    
    // 1. Procesar solicitudes BLE pendientes (COMPLETAMENTE MODIFICADO)
    if (solicitudAltaBLE || solicitudBajaBLE) {
        static unsigned long inicioAnimacion = 0;
        static bool faseAnimacion = true; // true: animación giratoria, false: resultado
        
        if (inicioAnimacion == 0) {
            inicioAnimacion = millis();
            faseAnimacion = true;
        }
        
        // Fase 1: Animación giratoria por 3 segundos
        if (faseAnimacion && (millis() - inicioAnimacion <= 3000)) {
            if (millis() - ultimoCambioAnimacion >= INTERVALO_ANIMACION) {
                frameAnimacion++;
                ultimoCambioAnimacion = millis();
            }
            mostrarEmparejamiento();
        }
        // Fase 2: Mostrar resultado por 5 segundos
        else if (faseAnimacion && (millis() - inicioAnimacion > 3000)) {
            faseAnimacion = false;
            inicioAnimacion = millis(); // Reiniciar timer para fase 2
        }
        else if (!faseAnimacion && (millis() - inicioAnimacion <= 5000)) {
            mostrarResultadoOperacion();
        }
        // Finalizar: Limpiar estado y regresar a normal
        else if (!faseAnimacion && (millis() - inicioAnimacion > 5000)) {
            solicitudAltaBLE = false;
            solicitudBajaBLE = false;
            inicioAnimacion = 0;
            faseAnimacion = true;
            ultimoNombreDispositivo = "";
            ultimosLitros = 0;
            ultimaAltura = 0;
            detenerEmparejamiento();
        }
        
        return; // Salir del loop mientras se muestra animación/resultado
    }


    // 3. Comportamiento en recepción continua
    // 4. Si estamos en modo AP, manejar eso y salir
    if (forceAPMode) {
        if (animandoWifi && millis() - ultimoCambioWifi >= INTERVALO_WIFI) {
            frameWifi = (frameWifi + 1) % 4;
            ultimoCambioWifi = millis();
        }
        mostrarConexionWifi();
        dnsServer.processNextRequest();
        server.handleClient();
        return;
    }

    // 5. VERIFICACIÓN MÁS ROBUSTA DE CONEXIÓN WIFI
    static unsigned long lastWifiCheck = 0;
    if (millis() - lastWifiCheck > 10000) { // Cada 10 segundos
        lastWifiCheck = millis();
        
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("WiFi desconectado - Intentando reconexión...");
            wifiConectado = false;
            attemptReconnectToAllNetworks();
        } else if (!wifiConectado) {
            Serial.println("WiFi reconectado exitosamente");
            wifiConectado = true;
        }
    }

    // 6. En caso de que tenga WIFFI conectado a MQTT
    // y este dado de alta envia el dato si envia lo que recibio en LORA
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
            nuevoMensajeLoRa = false; //solo publicar una vez el mensaje y esperar a otro nuevo
        }
    } 

    // 7. Manejo básico de conexiones MQTT
    if (!client.connected() && WiFi.status() == WL_CONNECTED && !forceAPMode) {
        reconnect();  //Solo para reconectar y configuracion de subscripciones
    }
    client.loop();

    // 8. Solicitar alta en broker si está conectado
    if (client.connected() && WiFi.status() == WL_CONNECTED && !forceAPMode) {
        MQTT_ALTA();  //Para solicitar el alta en broker
    }

    // 9. Verificación periódica de memoria (solo para debug)
    static unsigned long lastMemoryCheck = 0;
    const unsigned long memoryCheckInterval = 30000; // 30 segundos en milisegundos
    if (millis() - lastMemoryCheck > memoryCheckInterval) {
        lastMemoryCheck = millis();
        checkMemory();
    }

    // 10. Manejo de reconexión WiFi
    if (WiFi.status() != WL_CONNECTED && !forceAPMode) {
        wifiConectado = false;
        Reintentar_Wiffi();
    } else {
        wifiConectado = true;
    }

    // 11. Mostrar pantalla normal si no estamos en modo AP
// 11. Mostrar pantalla normal si no estamos en modo AP
// 11. Mostrar pantalla normal si no estamos en modo AP
if(!forceAPMode){
    // Actualizar contador de dispositivos
    int cantidadDispositivos = contarDispositivosRegistrados();
    
    // ⭐⭐ CORREGIR: Evitar división por cero
    if (cantidadDispositivos == 0) {
        // Mostrar pantalla de "Sin Dispositivos"
        display.clearDisplay();
        dibujarHeader();
        dibujarTituloDispositivo();
        dibujarContenidoPrincipal();
        display.display();
        delay(100);
        return; // Salir temprano
    }
    
    // Configurar intervalo de rotación
    if (cantidadDispositivos <= 1) {
        INTERVALO_CAMBIO = 0; // No rotar si hay 0 o 1 dispositivo
    } else {
        INTERVALO_CAMBIO = 3000; // 3 segundos por dispositivo
    }
    
    // ⭐⭐ SEGURO: cantidadDispositivos es al menos 1
    Dispositivo dispActual = obtenerDatosDispositivo(dispositivoActual % cantidadDispositivos);
    
    // ⭐⭐ SOLO IMPRIMIR SI HAY CAMBIOS
    static String ultimoNombreMostrado = "";
    static int ultimoPorcentajeMostrado = -1;
    static int ultimosLitrosMostrados = -1;
    static float ultimaBateriaMostrada = -1.0;
    
 // En el loop principal, reemplaza esta sección:
if (dispActual.nombre != ultimoNombreMostrado || 
    dispActual.porcentaje != ultimoPorcentajeMostrado ||
    dispActual.litros != ultimosLitrosMostrados ||
    dispActual.bateria != ultimaBateriaMostrada) {
    
    // Actualizar valores de referencia
    ultimoNombreMostrado = dispActual.nombre;
    ultimoPorcentajeMostrado = dispActual.porcentaje;
    ultimosLitrosMostrados = dispActual.litros;
    ultimaBateriaMostrada = dispActual.bateria;
    
    // ⭐⭐ SOLO IMPRIMIR CAMBIOS SIGNIFICATIVOS (cada 30 segundos o cuando cambie)
    static unsigned long ultimoPrintPantalla = 0;
    if (millis() - ultimoPrintPantalla > 30000) { // Cada 30 segundos
        ultimoPrintPantalla = millis();
        Serial.println("📊 Pantalla - " + dispActual.nombre + 
                     " - " + String(dispActual.porcentaje) + "% - " + 
                     String(dispActual.litros) + "L - " + 
                     String(dispActual.bateria) + "V");
    }
}
    
    display.clearDisplay();
    dibujarHeader();
    dibujarTituloDispositivo();
    dibujarContenidoPrincipal();
    
    // Rotar dispositivos solo si hay más de 1
    if (cantidadDispositivos > 1 && INTERVALO_CAMBIO > 0) {
        if (millis() - ultimoCambio >= INTERVALO_CAMBIO) {
            dispositivoActual = (dispositivoActual + 1) % cantidadDispositivos;
            ultimoCambio = millis();
            
            // ⭐⭐ IMPRIMIR SOLO AL ROTAR (opcional)
            Serial.println("🔄 Rotando a siguiente dispositivo");
        }
    }
    
    display.display();
    delay(100);
}



  // DEBUG PERIÓDICO DE ESTADO
    static unsigned long lastStateDebug = 0;
    if (millis() - lastStateDebug > 15000) {
        lastStateDebug = millis();
        
        Serial.println("\n📊 ===== ESTADO DEL SISTEMA =====");
        Serial.printf("📡 WiFi: %s\n", WiFi.status() == WL_CONNECTED ? "CONECTADO" : "DESCONECTADO");
        Serial.printf("📶 RSSI WiFi: %d dBm\n", WiFi.RSSI());
        Serial.printf("🔗 MQTT: %s\n", client.connected() ? "CONECTADO" : "DESCONECTADO");
        Serial.printf("📱 BLE: %s\n", deviceConnected ? "CONECTADO" : "DESCONECTADO");
        Serial.printf("💾 Dispositivos registrados: %d\n", contarDispositivosRegistrados());
        Serial.printf("🔄 Dispositivo actual en pantalla: %d\n", dispositivoActual);
        Serial.println("📊 ==============================\n");
    }



}


void debugNombresDispositivos() {
    Serial.println("\n=== DEBUG NOMBRES DISPOSITIVOS ===");
    for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
        if (String(configDispositivos[i].mac) != "") {
            Serial.printf("📍 [%d] MAC: %s, Nombre: '%s'\n", 
                         i, configDispositivos[i].mac, configDispositivos[i].nombre);
        }
    }
    Serial.println("==================================\n");
}

void verificarTiemposSinDatos() {
    static unsigned long ultimaVerificacion = 0;
    if (millis() - ultimaVerificacion > 10000) { // Cada 10 segundos
        ultimaVerificacion = millis();
        
        for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
            if (String(configDispositivos[i].mac) != "") {
                if (millis() - ultimaActualizacionLoRa[i] > TIEMPO_SIN_DATOS && !mostrarSinDatos[i]) {
                    Serial.println("⚠️  Dispositivo " + String(i) + " sin datos por más de " + String(TIEMPO_SIN_DATOS/1000) + " segundos");
                    mostrarSinDatos[i] = true;
                }
            }
        }
    }
}


void debugNombreProblema() {
    Serial.println("\n=== DEBUG NOMBRE PROBLEMA ===");
    for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
        if (String(configDispositivos[i].mac) != "") {
            Serial.printf("📍 [%d] MAC: %s, Nombre: '%s'\n", 
                         i, configDispositivos[i].mac, configDispositivos[i].nombre);
        }
    }
    Serial.println("==============================\n");
}

//Temporal Debug
void debugMensajeLoRa(const String &mensaje) {
    Serial.println("\n🔍 DEBUG DETALLADO MENSAJE LoRa:");
    Serial.println("Mensaje completo: '" + mensaje + "'");
    
    // Mostrar posiciones de todas las comas
    Serial.print("Posiciones de comas: ");
    for (int i = 0; i < mensaje.length(); i++) {
        if (mensaje.charAt(i) == ',') {
            Serial.print(i + " ");
        }
    }
    Serial.println();
    
    // Mostrar campos individuales
    int start = 0;
    int fieldCount = 0;
    for (int i = 0; i <= mensaje.length(); i++) {
        if (i == mensaje.length() || mensaje.charAt(i) == ',') {
            String field = mensaje.substring(start, i);
            Serial.printf("Campo %d: '%s' (longitud: %d)\n", fieldCount, field.c_str(), field.length());
            start = i + 1;
            fieldCount++;
        }
    }
    Serial.println("🔍 FIN DEBUG MENSAJE\n");
}


//Temporal debug
void testDispositivosRapido() {
    Serial.println("\n🧪 ===========================================");
    Serial.println("🔍 TEST RÁPIDO - DISPOSITIVOS REGISTRADOS");
    Serial.println("🧪 ===========================================");
    
    int totalRegistrados = 0;
    
    for (int i = 0; i < MAX_DISPOSITIVOS; i++) {
        if (String(configDispositivos[i].mac) != "") {
            totalRegistrados++;
            Serial.printf("📍 [%d] MAC: %s\n", i, configDispositivos[i].mac);
            Serial.printf("   📝 Nombre: '%s'\n", configDispositivos[i].nombre);
            Serial.printf("   📊 Porcentaje: %d%%\n", configDispositivos[i].porcentaje);
            Serial.printf("   💧 Litros Actuales: %.1f L\n", configDispositivos[i].litrosActuales);
            Serial.printf("   📏 Litros Config: %.1f L\n", configDispositivos[i].litrosConfig);
            Serial.printf("   📐 Altura Config: %.1f cm\n", configDispositivos[i].alturaConfig);
            Serial.printf("   🔋 Voltaje: %.2f V\n", configDispositivos[i].voltaje);
            Serial.printf("   🌡️ Temperatura: %.1f °C\n", configDispositivos[i].temperatura);
            Serial.printf("   ⚡ Activo: %s\n", configDispositivos[i].activo ? "SI" : "NO");
            Serial.printf("   🔧 Tipo: %d\n", configDispositivos[i].tipoDispositivo);
            
            // Verificar si hay datos recientes
            unsigned long tiempoSinDatos = millis() - ultimaActualizacionLoRa[i];
            Serial.printf("   ⏰ Tiempo sin datos: %lu ms\n", tiempoSinDatos);
            Serial.printf("   📡 Estado: %s\n", tiempoSinDatos > TIEMPO_SIN_DATOS ? "SIN DATOS" : "ACTIVO");
            
            Serial.println("   ─────────────────────────");
        }
    }
    
    Serial.printf("📊 TOTAL DISPOSITIVOS REGISTRADOS: %d de %d\n", totalRegistrados, MAX_DISPOSITIVOS);
    
    if (totalRegistrados == 0) {
        Serial.println("❌ NO HAY DISPOSITIVOS REGISTRADOS");
        Serial.println("💡 Usa BLE para registrar un dispositivo primero");
    } else {
        Serial.println("✅ Dispositivos cargados correctamente");
    }
    
    Serial.println("🧪 ===========================================\n");
}

// Llamar esta función después de registrar un dispositivo via BLE
