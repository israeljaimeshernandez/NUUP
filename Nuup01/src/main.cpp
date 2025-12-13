/******************************************************************************
 * ╔═══════════════════════════════════════════════════════════════════════╗
 * ║                          NUUUP AI                                     ║
 * ║                   Advanced Agentic Coding System                      ║
 * ╚═══════════════════════════════════════════════════════════════════════╝
 *
 * Proyecto:    NUUP - Sistema de Monitoreo de Tanques de Agua
 * Componente:  NUUP01 - Sensor de Nivel para Tanque/Tinaco
 * Hardware:    ESP32 NodeMCU-32S (Espressif ESP32)
 * Plataforma:  PlatformIO + Arduino Framework
 *
 * DESCRIPCIÓN:
 * Dispositivo sensor instalado en tanques de agua que mide el nivel mediante
 * sensor ultrasónico HC-SR04, transmite datos vía LoRa al Monitor01, y gestiona
 * consumo de energía mediante deep sleep inteligente.
 *
 * CAPACIDADES:
 * - Medición de nivel con sensor ultrasónico (2-400cm)
 * - Cálculo de litros basado en geometría del tanque
 * - Transmisión LoRa de telemetría cada 20 segundos
 * - Portal WiFi cautivo para configuración web
 * - Cliente BLE para emparejamiento con Monitor
 * - Análisis local de consumo con IA
 * - Detección de fugas y patrones anormales
 * - Deep sleep con wake-up por timer o impacto
 * - Sensor de vibración para detección de golpes
 * - LEDs indicadores de estado (verde/rojo)
 *
 * COMPILADO CON: Antigravity AI - Google DeepMind
 * FECHA: 2025-12-03
 *
 ******************************************************************************/

// 97 - 2025-06-15 Confirmación LoRa: espera progresiva por intento, trazas de mensajes inesperados y compatibilidad reforzada.
// 96 - 2025-06-11 Potencia LoRa: barrido dinámico 2-12 dBm tras impacto, confirmación configuracion/MAC/confirmacion y persistencia en EEPROM.
// 01 - 2025-05-24 Ajuste de doble/triple toque para despertar, espera
//      ampliada en modo AP tras abrir la página, envíos LoRa cada 20s y
//      proceso de baja con parpadeo/validación extendidos.
// 02 - 2025-05-25 Calibración fina del sensor de impacto para toques suaves;
//      ajustar IMPACTO_UMBRAL_ANALOGICO (sensibilidad), IMPACTO_MIN_SEPARACION_MS
//      (rebote), IMPACTO_VENTANA_MS (ventana de conteo), IMPACTO_MUESTRAS_BASE
//      (línea base) y los límites IMPACTO_MIN_TOQUES/IMPACTO_MAX_TOQUES según
//      la respuesta del hardware.
// 06 - 2025-06-06 Corrección: textos de alcance movidos a configuración inicial, portal sin mensajes de alcance y consecutivo actualizado.
// 05 - 2025-06-05 Ajuste: consecutivo con variable de cercanía BLE ajustable,
//      detalle de alcance WiFi/AP al crear la red y guía en español para reducir o aumentar cobertura.
// 04 - 2025-06-04 Límite de emparejamiento BLE a ~5cm: se exige RSSI cercano,
//      se imprime el alcance y se refuerza la bitácora en español.
// 03 - 2025-05-26 Ventana de vigilia por impacto ahora configurable (1 minuto
//      por defecto) con LED verde parpadeando al esperar BLE/WiFi, sólido si
//      hay cliente en portal web y reinicio completo al finalizar la vigilia.

// ============================================================================
// LEYENDA: Rama 'work' - Última actualización: persistencia web sin registro, MAC nula hasta READY y limpiezas solo por baja/botón.
// ============================================================================
// CONFIGURACIÓN PRINCIPAL - DEFINICIONES ÚNICAS
// ============================================================================

// --- Incluir librerías ---
#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <math.h>
#include <cstring>
#include "driver/rtc_io.h"
#include <algorithm>
#include "esp_task_wdt.h"
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <DNSServer.h>
DNSServer dnsServer;

// --- Declarar objetos globales ---
WebServer server(80);

// --- Variables globales ---
bool modoConfiguracionActivo = false;
unsigned long tiempoInicioConfiguracion = 0;
#define TIEMPO_MAXIMO_CONFIGURACION 0 // 0 = sin límite mientras haya cliente

// --- Pines ---
#define LED_VERDE_PIN 27
#define LED_ROJO_PIN 26
#define SENSOR_IMPACTO_PIN 33
#define LED_PIN LED_VERDE_PIN
#define ADC_PIN 34
#define LORA_SS 5
#define LORA_RST -1
#define LORA_DIO0 -1

// --- Calibración de sensibilidad de impacto (ajustables) ---
// 01) Ventana máxima para capturar toques consecutivos (ms)
const uint16_t IMPACTO_VENTANA_MS = 1500;
// 02) Tiempo mínimo entre toques para evitar rebotes (ms)
const uint16_t IMPACTO_MIN_SEPARACION_MS = 70;
// 03) Umbral mínimo de caída analógica respecto al valor base para contar un toque
const uint16_t IMPACTO_UMBRAL_ANALOGICO = 25;
// 04) Muestras usadas para estimar el nivel en reposo del sensor
const uint8_t IMPACTO_MUESTRAS_BASE = 16;
// 05) Cantidad mínima de toques válidos para aceptar el despertar
const uint8_t IMPACTO_MIN_TOQUES = 1;
// 06) Cantidad máxima de toques válidos (se ignoran adicionales)
const uint8_t IMPACTO_MAX_TOQUES = 3;
// 07) Tiempo en vigilia tras despertar por impacto para detectar BLE/WiFi (ms)
const uint32_t IMPACTO_TIEMPO_VIGILIA_MS = 60000; // Por defecto 1 minuto

// --- Tiempos ÚNICOS ---
#define INTERVALO_ENVIO_DATOS 20000      // 20 segundos entre envíos LoRa (registrado)
#define INTERVALO_ENVIO_CAMBIO 5000      // 5 segundos cuando hubo cambios recientes
#define INTERVALO_ENVIO_FORZOSO 20000    // 20 segundos máximo sin cambios
#define TIEMPO_ESPERA_CONFIRMACION_INICIAL 2000  // 2 segundos base de espera por confirmación LoRa
#define INCREMENTO_ESPERA_CONFIRMACION 500       // Aumento progresivo por intento para dar margen de sincronización
#define REINTENTOS_CONFIRMACION 5        // Envío inicial + 4 reintentos antes de reiniciar
#define LORA_POTENCIA_MIN_DBM 2          // Potencia mínima para el barrido dinámico
#define LORA_POTENCIA_MAX_DBM 12         // Potencia máxima objetivo para el sensor
#define LORA_POTENCIA_DEFECTO_DBM 2      // Potencia por defecto si no hay confirmaciones
#define REINTENTOS_CONFIG_POTENCIA 5     // Veces que se intercambia configuracion/MAC/confirmacion
#define INTERVALO_ESCANEO_ALTA 10000  // 10 segundos (búsqueda activa extendida)
#define INTERVALO_ESCANEO_BAJA 15000  // 15 segundos (monitoreo)
#define INTERVALO_PARPADEO 62
#define INTERVALO_PARPADEO2 1000

// --- Banderas de mantenimiento ---
const bool LIMPIEZA_FABRICA_EN_SETUP = false; // Cambiar a true para limpiar EEPROM y reiniciar en setup

// --- EEPROM ---
#define EEPROM_SIZE 128
#define EEPROM_ADDR_REGISTRADO 0
#define EEPROM_ADDR_DATOS 1

// --- Configuración WiFi AP ---
const char* ssidAP = "NUUP01_Configuracion";
const char* passwordAP = ""; // Sin contraseña

// --- Alcances ajustables ---
// BLE: RSSI_MIN_APAREAMIENTO controla la proximidad mínima (por defecto ~5 cm).
// WiFi/AP: alcanceWiFiMaximo fija la cobertura objetivo en metros y potenciaTxWiFi define la fuerza de transmisión.

// --- Variables WiFi ---
int alcanceWiFiMaximo = 1; // metros
int potenciaTxWiFi = 8;    // Potencia de transmisión

// --- Estructura de datos ---
struct DispositivoData {
    char mac[18] = "";
    char nombre[21] = "";
    uint32_t altura = 0;
    uint32_t litros = 0;
    uint8_t potenciaLoRaDbm = LORA_POTENCIA_DEFECTO_DBM;
};

// --- Variables globales ---
DispositivoData dispositivo;
bool registrado = false;
String macAddress = "";
int counter = 0;

// --- Variables para configuración web ---
String nombreDispositivo = "Tinaco villas 1";
int alturaDispositivo = 180;
int litrosDispositivo = 1100;

// ============================================================================
// VARIABLES GLOBALES BLE Y SISTEMA
// ============================================================================

// Variables BLE
BLEClient* pClient;
BLERemoteCharacteristic* pRemoteCharacteristic;
bool deviceConnected = false;
bool doConnect = false;
bool comandoPendiente = false;
String targetDeviceName = "NUUP_Monitor";
BLEAdvertisedDevice* myDevice;
int RSSI_MIN_APAREAMIENTO = -45; // dBm necesarios para estar a ~5 cm (ajustable)

String macRegistrada = "";
bool esperandoDatosConfig = false;
unsigned long tiempoEsperaConfig = 0;
#define TIMEOUT_CONFIG 5000 // 5 segundos para recibir datos de configuración

bool pendienteEnvioConfig = false;
unsigned long tiempoProgramadoEnvio = 0;
#define DELAY_ENTRE_MENSAJES 500  // ms entre REG y CONFIG

// BLE Configuration
#define SERVICE_UUID        "4e555550-2024-1337-8001-123456789abc"
#define CHARACTERISTIC_UUID "4e555550-2024-1337-8002-123456789abc"

// Otras variables globales
bool botonPresionado = false;
unsigned long tiempoInicioPresion = 0;
unsigned long ultimoEnvioDatos = 0;
unsigned long intervaloEnvioActual = INTERVALO_ENVIO_FORZOSO;
String ultimoMensajeConfirmado = "";
unsigned long ultimoCambioLedRojo = 0;
bool estadoLedRojo = false;
unsigned long ultimoEscaneoBLE = 0;
uint8_t potenciaLoRaActualDbm = LORA_POTENCIA_DEFECTO_DBM;
bool recalibrarPotenciaLoRa = false;

unsigned long tiempoInicioRegistro = 0;
#define TIMEOUT_REGISTRO_COMPLETO 10000
bool enProcesoRegistro = false;

bool bajaAutomaticaActivada = false;
unsigned long tiempoInicioBaja = 0;
#define TIMEOUT_BAJA 15000
bool wakeByImpact = false;
unsigned long inicioVigiliaImpacto = 0;

// Variables sensor
const int trigPin = 21;
const int echoPin = 22;
#define MAX_REINTENTOS 20
#define TIEMPO_ENTRE_REINTENTOS 1000
#define TIEMPO_RESET_SENSOR 1000
int reintentosRestantes = MAX_REINTENTOS;
unsigned long ultimoReintento = 0;

// Variables RTC
RTC_DATA_ATTR unsigned long ultimoEnvio = 0;
RTC_DATA_ATTR unsigned long ultimoEscaneoBLESleep = 0;
RTC_DATA_ATTR bool bajaAutomaticaActivadaSleep = false;
RTC_DATA_ATTR bool solicitudBajaPendiente = false;
RTC_DATA_ATTR unsigned long ultimoWakeup = 0;
RTC_DATA_ATTR unsigned long tiempoFinBaja = 0;
RTC_DATA_ATTR bool esperaDespuesBaja = false;
#define TIEMPO_ESPERA_DESPUES_BAJA 15000

// Configuración LED
#define SECUENCIA_LED_FIJO 10000
#define SECUENCIA_LED_PARPADEANTE 5000
#define SECUENCIA_INTERVALO_PARPADEO 200
#define PARPADEO_LED_RAPIDO_MS 250
#define PARPADEO_LED_LENTO_MS 1000
#define DURACION_PARPADEO_PROCESO_BAJA_MS 5000
#define DURACION_PARPADEO_FINAL_BAJA_MS 10000
#define PARPADEO_LORA_INTERVALO_MS 100
#define DURACION_PARPADEO_LORA_MS 500
#define PARPADEO_WAKE_ROJO_MS 250
#define DURACION_PARPADEO_WAKE_MS 500
#define DURACION_PARPADEO_EMPAREJANDO_MS 5000
#define INTERVALO_PARPADEO_EMPAREJANDO_MS 250
#define DURACION_LED_CONFIRMACION_MS 5000

// Estructuras IA
struct MedicionHistorial {
    unsigned long timestamp;
    int litros;
};

struct AnalisisConsumo {
    float promedioDiario;
    float promedioSemanal; 
    float promedioMensual;
    int notificacion;
    String mensajeNotificacion;
};

struct EstadisticasConsumo {
    MedicionHistorial historial24h[288];
    int indiceHistorial = 0;
    int totalMediciones = 0;
    unsigned long ultimoResetEstadisticas = 0;
    int medicionesConsecutivasBajas = 0;
    int consumoConsecutivo = 0;
};

EstadisticasConsumo estadisticas;
AnalisisConsumo analisisActual;
unsigned long ultimoAnalisis = 0;
#define INTERVALO_ANALISIS 300000

// Constantes para umbrales
#define UMBRAL_FUGA 3
#define UMBRAL_BAJO_NIVEL 6
#define CONSUMO_MINIMO_NORMAL 5

// ============================================================================
// PROTOTIPOS DE FUNCIÓN
// ============================================================================

// Funciones WiFi y Web
void configurarWiFiAP();
void configurarServidorWeb();
void mostrarPaginaConfig();
void guardarConfigWeb();
void manejarReinicio();
void manejarRestauracionFabrica();
void configurarAlcanceWiFi(int metros);
void verificarConexionCliente();

// Funciones BLE
void scanForDevices();
bool connectToServer();
void sendCommand(String command);
void procesarComandoBLE(String comando);
void completarRegistro(String macServidor, String nombre, String alturaStr, String litrosStr);
void debugEstadoBLE();
void verificarConexionYCercania();
void debugConexionBLE();
void parpadearLED(int pin, unsigned long intervalo, unsigned long duracion);

// Funciones del sistema
void establecerValoresDeFabrica();
void inicializarDispositivo();
void guardarDatosEnEEPROM();
void leerDatosDeEEPROM();
void imprimirDatosDispositivo();
void limpiarEEPROMYReiniciar();
bool enviarDatos(int distancia);
bool intercambiarPotenciaConMonitor(uint8_t potenciaConfirmada);
float measureDistance();
int obtenerDistanciaValida();
int calcularLitros(int distancia, uint32_t alturaTotal, uint32_t litrosTotal);
void resetearSensorUltrasonico();
void entrarDeepSleep();
void prepararParaDeepSleep();
void iniciarLoRaConReintentos();
void manejarLED();
void ejecutarSecuenciaLED(String tipoOperacion);
void modoEsperaDespuesBaja();

// Funciones IA
void calcularPromediosBasicos();
bool detectarPosibleFuga();
bool detectarConsumoIrregular();
bool detectarNivelBajo(int litrosActuales);
void agregarAlHistorial(int litros);
void analizarPatronesConsumo(int litrosActuales);
String obtenerDatosIA();

// Callbacks BLE
class MyClientCallback : public BLEClientCallbacks {
    void onConnect(BLEClient* pclient);
    void onDisconnect(BLEClient* pclient);
};

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice);
};

// ============================================================================
// IMPLEMENTACIÓN DE FUNCIONES WiFi Y WEB
// ============================================================================

void configurarServidorWeb() {
    // ============================================================================
    // CAPTIVE PORTAL - Redirección automática cuando se conecta al WiFi
    // ============================================================================
    
    // Android captive portal check
    server.on("/generate_204", HTTP_GET, []() {
        Serial.println("📱 Captive Portal Android detectado - Redirigiendo a página principal...");
        server.sendHeader("Location", "http://192.168.4.1/");
        server.send(302, "text/plain", "");
    });
    
    // Apple/iOS captive portal check
    server.on("/hotspot-detect.html", HTTP_GET, []() {
        Serial.println("📱 Captive Portal Apple detectado - Redirigiendo a página principal...");
        server.sendHeader("Location", "http://192.168.4.1/");
        server.send(302, "text/plain", "");
    });
    
    // Windows captive portal check
    server.on("/connecttest.txt", HTTP_GET, []() {
        Serial.println("📱 Captive Portal Windows detectado - Enviando respuesta...");
        server.send(200, "text/plain", "Microsoft NCSI");
    });
    
    // Microsoft redirect
    server.on("/fwlink", HTTP_GET, []() {
        Serial.println("📱 Captive Portal Microsoft detectado - Redirigiendo...");
        server.sendHeader("Location", "http://192.168.4.1/");
        server.send(302, "text/plain", "");
    });
    
    // Kindle and other devices
    server.on("/library/test/success.html", HTTP_GET, []() {
        Serial.println("📱 Captive Portal Kindle detectado - Redirigiendo...");
        server.sendHeader("Location", "http://192.168.4.1/");
        server.send(302, "text/plain", "");
    });
    
    // Captive portal for various devices
    server.on("/ncsi.txt", HTTP_GET, []() {
        Serial.println("📱 Captive Portal genérico detectado - Enviando respuesta...");
        server.send(200, "text/plain", "OK");
    });
    
    // Redirección para cualquier otra página no definida
    server.onNotFound([]() {
        Serial.println("📱 Petición a página no encontrada - Redirigiendo a principal...");
        server.sendHeader("Location", "http://192.168.4.1/");
        server.send(302, "text/plain", "");
    });

    // ============================================================================
    // ENDPOINTS PRINCIPALES DE LA APLICACIÓN
    // ============================================================================
    
    // Página principal de configuración
    server.on("/", HTTP_GET, []() {
        Serial.println("📱 Cliente conectado al servidor web - Mostrando página de configuración");
        mostrarPaginaConfig();
    });
    
    // Endpoint para guardar configuración
    server.on("/guardar", HTTP_POST, []() {
        Serial.println("💾 Solicitud de guardar configuración recibida");
        guardarConfigWeb();
    });
    
    // Endpoint para reinicio
    server.on("/reiniciar", HTTP_GET, []() {
        Serial.println("🔄 Solicitud de reinicio recibida");
        manejarReinicio();
    });
    
    // Endpoint para restauración de fábrica
    server.on("/fabrica", HTTP_GET, []() {
        Serial.println("🚨 Solicitud de restauración de fábrica recibida");
        manejarRestauracionFabrica();
    });
    
    server.begin();
    Serial.println("✅ Servidor web iniciado en puerto 80");
    Serial.println("✅ Captive Portal ACTIVO - Redirección automática configurada");
}

void configurarAlcanceWiFi(int metros) {
    alcanceWiFiMaximo = metros;
    
    // Mapear metros a valores de potencia WiFi correctos
    wifi_power_t potenciaWiFi;
    switch(metros) {
        case 1:  // ~1 metro - Potencia mínima
            potenciaWiFi = WIFI_POWER_2dBm;  // 2 dBm
            potenciaTxWiFi = 2;
            break;
        case 2:  // ~2 metros - Potencia baja
            potenciaWiFi = WIFI_POWER_5dBm;  // 5 dBm  
            potenciaTxWiFi = 5;
            break;
        case 5:  // ~5 metros - Potencia media
            potenciaWiFi = WIFI_POWER_11dBm; // 11 dBm
            potenciaTxWiFi = 11;
            break;
        case 10: // ~10 metros - Potencia alta
            potenciaWiFi = WIFI_POWER_17dBm; // 17 dBm
            potenciaTxWiFi = 17;
            break;
        default: // Máxima potencia
            potenciaWiFi = WIFI_POWER_19_5dBm; // 19.5 dBm
            potenciaTxWiFi = 19;
            break;
    }
    
    // ⭐⭐ CORRECCIÓN: Usar el tipo correcto wifi_power_t
    WiFi.setTxPower(potenciaWiFi);
    
    Serial.printf("📶 WiFi configurado - Alcance: %d metro(s)\n", alcanceWiFiMaximo);
    Serial.printf("   ⚡ Potencia TX: %d dBm\n", potenciaTxWiFi);
}

void configurarWiFiAP() {
    Serial.println("\n🌐 INICIANDO WiFi AP CON CAPTIVE PORTAL...");
    
    // Configurar WiFi AP
    WiFi.mode(WIFI_AP);
    
    // Configurar alcance mínimo por defecto (1 metro)
    configurarAlcanceWiFi(1);
    Serial.printf("ℹ️  Alcance AP actual: %d m (potencia %d dBm). Ajusta alcanceWiFiMaximo o llama configurarAlcanceWiFi() para modificarlo.\n", alcanceWiFiMaximo, potenciaTxWiFi);

    bool apStatus = WiFi.softAP(ssidAP, passwordAP);
    
    if (apStatus) {
        Serial.println("✅ WiFi AP CONFIGURADO EXITOSAMENTE");
        Serial.printf("   📶 SSID: %s\n", ssidAP);
        Serial.printf("   📍 IP: %s\n", WiFi.softAPIP().toString().c_str());
        Serial.printf("   🎯 Alcance: %d metro(s)\n", alcanceWiFiMaximo);
        
        // ⭐⭐ INICIAR DNS SERVER PARA CAPTIVE PORTAL
        dnsServer.start(53, "*", WiFi.softAPIP());
        Serial.println("   🔄 DNS Server iniciado - Todas las peticiones redirigidas");
    } else {
        Serial.println("❌ ERROR: No se pudo iniciar WiFi AP");
        return;
    }
    
    // Configurar servidor web
    configurarServidorWeb();
    
    Serial.println("✅ CAPTIVE PORTAL ACTIVO - Página se abrirá automáticamente");
}

void verificarConexionCliente() {
    int clientes = WiFi.softAPgetStationNum();
    static int clientesAnteriores = 0;
    
    if (clientes != clientesAnteriores) {
        if (clientes > 0) {
            modoConfiguracionActivo = true;
            tiempoInicioConfiguracion = millis();
            Serial.println("📱 Cliente conectado - MODO CONFIGURACIÓN");
        } else {
            modoConfiguracionActivo = false;
            Serial.println("📱 Cliente desconectado - MODO NORMAL");

            // Si el despertar fue por impacto y ya salió el usuario, reiniciar para cerrar sesión AP
            if (wakeByImpact) {
                Serial.println("🔁 Configuración por impacto finalizada - Reiniciando ESP32...");
                delay(500);
                ESP.restart();
            }
        }
        clientesAnteriores = clientes;
    }

    if (TIEMPO_MAXIMO_CONFIGURACION > 0 &&
        modoConfiguracionActivo &&
        (millis() - tiempoInicioConfiguracion > TIEMPO_MAXIMO_CONFIGURACION)) {
        modoConfiguracionActivo = false;
        Serial.println("⏰ Timeout configuración");
    }
}

bool confirmarGolpesImpacto() {
    Serial.println("🔔 Detectando doble/triple toque para despertar...");

    // Calibrar el nivel de referencia con varias lecturas suaves
    uint32_t acumulado = 0;
    for (uint8_t i = 0; i < IMPACTO_MUESTRAS_BASE; i++) {
        acumulado += analogRead(SENSOR_IMPACTO_PIN);
        delay(2);
    }
    uint16_t baseReposo = acumulado / IMPACTO_MUESTRAS_BASE;
    Serial.printf("📏 Nivel base de impacto: %u (umbral: -%u)\n", baseReposo, IMPACTO_UMBRAL_ANALOGICO);
    Serial.printf("🎚️  Sensibilidad aumentada: se registrará golpe con caída ≥%u (50%% del umbral previo)\n",
                  IMPACTO_UMBRAL_ANALOGICO);

    int toquesDetectados = 1; // Primer toque es el que despertó
    unsigned long inicioVentana = millis();
    bool ultimoEstado = digitalRead(SENSOR_IMPACTO_PIN);
    unsigned long ultimoToque = inicioVentana;

    while (millis() - inicioVentana < IMPACTO_VENTANA_MS) {
        bool estadoActual = digitalRead(SENSOR_IMPACTO_PIN);
        uint16_t lecturaAnalogica = analogRead(SENSOR_IMPACTO_PIN);
        bool posibleToquePorAnalogico = baseReposo > lecturaAnalogica &&
                                        (baseReposo - lecturaAnalogica) >= IMPACTO_UMBRAL_ANALOGICO;
        bool transicionDigital = (ultimoEstado == HIGH && estadoActual == LOW);

        if ((transicionDigital || posibleToquePorAnalogico) &&
            (millis() - ultimoToque) >= IMPACTO_MIN_SEPARACION_MS) {
            toquesDetectados++;
            ultimoToque = millis();
            Serial.printf("💥 Toque %d registrado (analog=%u, base=%u)\n", toquesDetectados, lecturaAnalogica, baseReposo);
        }

        ultimoEstado = estadoActual;

        if (toquesDetectados >= IMPACTO_MAX_TOQUES) {
            break; // No se requieren más de tres golpes
        }
    }

    Serial.printf("🔎 Total de toques detectados: %d\n", toquesDetectados);
    Serial.printf("📈 Indicador de impacto: base %u, umbral -%u, toques válidos %s\n",
                  baseReposo,
                  IMPACTO_UMBRAL_ANALOGICO,
                  toquesDetectados >= IMPACTO_MIN_TOQUES && toquesDetectados <= IMPACTO_MAX_TOQUES
                      ? "✅ dentro del rango"
                      : "❌ insuficientes/excesivos");
    return toquesDetectados >= IMPACTO_MIN_TOQUES && toquesDetectados <= IMPACTO_MAX_TOQUES;
}

void mostrarPaginaConfig() {
    String html = R"=====(
<!DOCTYPE html>
<html>
<head>
    <title>NUUP - Configuraci&oacute;n</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { 
            font-family: Arial; 
            background: #121212; 
            color: #FFD700; 
            padding: 20px;
            text-align: center;
        }
        .container { 
            max-width: 400px; 
            margin: 0 auto;
            background: #1a1a1a;
            padding: 25px;
            border-radius: 10px;
            box-shadow: 0 0 10px rgba(255, 215, 0, 0.3);
        }
        h2 { color: #FFD700; margin-bottom: 20px; }
        input, button { 
            width: 90%; 
            padding: 12px; 
            margin: 8px 0; 
            border: none;
            border-radius: 5px;
            font-size: 16px;
        }
        input { 
            background: #2a2a2a; 
            color: #FFD700;
            border: 1px solid #FFD700;
        }
        .btn-guardar { 
            background: #FFD700; 
            color: #000; 
            font-weight: bold;
        }
        .btn-reiniciar { 
            background: #007BFF; 
            color: #fff; 
        }
        .btn-fabrica { 
            background: #DC3545; 
            color: #fff; 
        }
        hr { 
            border: 1px solid #333; 
            margin: 20px 0; 
        }
    </style>
</head>
<body>
    <div class="container">
        <h2>🔧 Configurar Dispositivo</h2>
        <p>Captura los datos del sensor y guarda la configuración.</p>

        <form action="/guardar" method="post" id="config-form">
            <input type="text" name="nombre" value=")=====" + String(dispositivo.nombre) + R"=====(" placeholder="Nombre del dispositivo" required>
            <input type="number" name="altura" value=")=====" + String(dispositivo.altura) + R"=====(" placeholder="Altura total (cm)" required>
            <input type="number" name="litros" value=")=====" + String(dispositivo.litros) + R"=====(" placeholder="Capacidad total (litros)" required>
            
            <button class="btn-guardar" type="submit">💾 Guardar Configuraci&oacute;n</button>
        </form>
        
        <hr>
        
        <button class="btn-reiniciar" onclick="confirmarReinicio()">🔄 Reiniciar Dispositivo</button>
        <br>
        <button class="btn-fabrica" onclick="confirmarFabrica()">🚨 Restaurar a Valores de F&aacute;brica</button>
    </div>

    <script>
        function confirmarReinicio() {
            if(confirm('¿Estás seguro de que deseas reiniciar el dispositivo?')) {
                window.location.href = '/reiniciar';
            }
        }
        function confirmarFabrica() {
            if(confirm('¡ADVERTENCIA! Esto borrará toda la configuración. ¿Continuar?')) {
                window.location.href = '/fabrica';
            }
        }
    </script>
</body>
</html>
)=====";

    server.send(200, "text/html", html);
}



void guardarConfigWeb() {
    Serial.println("\n💾 PROCESANDO CONFIGURACIÓN WEB COMPLETA...");

    // Esta ruta se ejecuta solo cuando el usuario envía el formulario del portal
    // cautivo. Actualiza las variables locales con los valores capturados, pero
    // NO marca el dispositivo como registrado para evitar altas prematuras; la
    // MAC y el flag de registro se fijan únicamente tras confirmar READY por BLE.
    // Verificar parámetros - SOLO los que existen en el formulario
    if (server.hasArg("nombre") && server.hasArg("altura") && server.hasArg("litros")) {
        String nuevoNombre = server.arg("nombre");
        int nuevaAltura = server.arg("altura").toInt();
        int nuevosLitros = server.arg("litros").toInt();
        
        Serial.printf("📥 Datos recibidos:\n");
        Serial.printf("   Nombre: '%s'\n", nuevoNombre.c_str());
        Serial.printf("   Altura: %d cm\n", nuevaAltura);
        Serial.printf("   Litros: %d L\n", nuevosLitros);
        
        // Validar datos
        if (nuevoNombre.length() > 0 && nuevaAltura > 0 && nuevosLitros > 0) {
            // Actualizar estructura dispositivo
            strncpy(dispositivo.nombre, nuevoNombre.c_str(), sizeof(dispositivo.nombre)-1);
            dispositivo.altura = nuevaAltura;
            dispositivo.litros = nuevosLitros;
            
            // Actualizar variables globales
            nombreDispositivo = nuevoNombre;
            alturaDispositivo = nuevaAltura;
            litrosDispositivo = nuevosLitros;
            
            // ⭐⭐ CONFIGURAR ALCANCE WiFi FIJO (1 metro por defecto)
            configurarAlcanceWiFi(1);
            
            // Guardar en EEPROM (sin marcar registro)
            guardarDatosEnEEPROM();
            
            // ⭐⭐ APAGAR WiFi ANTES DE REINICIAR
            Serial.println("📴 Apagando WiFi AP...");
            WiFi.softAPdisconnect(true);
            delay(100);
            
            // Respuesta de éxito
            String html = R"=====(
<html>
<head>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { 
            font-family: Arial; 
            background: #121212; 
            color: #00FF00; 
            text-align: center; 
            padding: 40px;
        }
        .container {
            background: #1a1a1a;
            padding: 30px;
            border-radius: 10px;
            border: 2px solid #00FF00;
            max-width: 400px;
            margin: 0 auto;
        }
        h2 { color: #00FF00; }
        .status {
            background: #2a2a2a;
            padding: 15px;
            border-radius: 5px;
            margin: 15px 0;
        }
    </style>
</head>
<body>
    <div class="container">
        <h2>✅ Configuración Guardada Exitosamente</h2>
        <div class="status">
            <p><strong>📝 Datos actualizados:</strong></p>
            <p>Nombre: )=====" + nuevoNombre + R"=====(</p>
            <p>Altura: )=====" + String(nuevaAltura) + R"=====( cm</p>
            <p>Capacidad: )=====" + String(nuevosLitros) + R"=====( L</p>
        </div>
        <p>📴 WiFi se ha desconectado</p>
        <p>🔄 Reiniciando en 3 segundos...</p>
    </div>
    <script>
        setTimeout(function() {
            document.body.innerHTML = '<div class="container"><h2>🔄 Reiniciando...</h2><p>El dispositivo se está reiniciando</p></div>';
        }, 3000);
    </script>
</body>
</html>
)=====";
            server.send(200, "text/html", html);
            
            Serial.println("✅ CONFIGURACIÓN COMPLETA - WiFi AP apagado");
            Serial.println("🔄 Reiniciando en 3 segundos...");
            
            // Secuencia LED de confirmación
            for(int i = 0; i < 6; i++) {
                digitalWrite(LED_PIN, !digitalRead(LED_PIN));
                delay(PARPADEO_LED_RAPIDO_MS);
            }
            
            delay(3000);
            ESP.restart();
            
        } else {
            server.send(400, "text/plain", "Error: Datos inválidos");
            Serial.println("❌ Error: Datos de configuración inválidos");
        }
    } else {
        server.send(400, "text/plain", "Error: Faltan parámetros");
        Serial.println("❌ Error: Faltan parámetros en la solicitud");
    }
}

void manejarReinicio() {
    // Respuesta inmediata
    server.send(200, "text/html", 
        "<html><body style='background:#121212; color:#00FF00; text-align:center; padding:40px;'>"
        "<h2>🔄 Reiniciando Dispositivo</h2>"
        "<p>Apagando WiFi y reiniciando...</p>"
        "<p>Reinicio en 3 segundos.</p>"
        "</body></html>");
    
    Serial.println("🔄 REINICIO SOLICITADO VÍA WEB - Apagando WiFi...");
    
    // ⭐⭐ NUEVO: APAGAR WiFi ANTES DE REINICIAR
    WiFi.softAPdisconnect(true);
    delay(100);
    
    // Secuencia LED
    for(int i = 0; i < 3; i++) {
        digitalWrite(LED_PIN, HIGH);
        delay(500);
        digitalWrite(LED_PIN, LOW);
        delay(500);
    }
    
    delay(3000);
    ESP.restart();
}


void manejarRestauracionFabrica() {
    // Respuesta inmediata
    server.send(200, "text/html", 
        "<html><body style='background:#121212; color:#FF4444; text-align:center; padding:40px;'>"
        "<h2>🚨 Restauración de Fábrica</h2>"
        "<p>Borrando configuración y apagando WiFi...</p>"
        "<p>Reiniciando en 5 segundos.</p>"
        "</body></html>");
    
    Serial.println("🚨 RESTAURACIÓN DE FÁBRICA SOLICITADA VÍA WEB");
    
    // Ejecutar secuencia LED de advertencia
    for(int i = 0; i < 10; i++) {
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
        delay(200);
    }
    
    // ⭐⭐ NUEVO: APAGAR WiFi ANTES DE LIMPIAR
    Serial.println("📴 Apagando WiFi AP...");
    WiFi.softAPdisconnect(true);
    delay(100);
    
    // Limpiar EEPROM y reiniciar
    limpiarEEPROMYReiniciar();
}


// ============================================================================
// IMPLEMENTACIÓN DE FUNCIONES BLE
// ============================================================================

void MyClientCallback::onConnect(BLEClient* pclient) {
    Serial.println("✅ Conectado al servidor NUUP");
    deviceConnected = true;
}

void MyClientCallback::onDisconnect(BLEClient* pclient) {
    Serial.println("❌ Desconectado del servidor NUUP");
    deviceConnected = false;
    doConnect = false;
}

void MyAdvertisedDeviceCallbacks::onResult(BLEAdvertisedDevice advertisedDevice) {
    String deviceName = String(advertisedDevice.getName().c_str());
    String deviceAddress = String(advertisedDevice.getAddress().toString().c_str());
  int rssi = advertisedDevice.getRSSI();
    
    Serial.printf("   📶 Dispositivo: '%s'", deviceName.c_str());
    Serial.printf(" - MAC: %s", deviceAddress.c_str());
    Serial.printf(" - RSSI: %d dBm", rssi);
    
  if (deviceName == targetDeviceName) {
        Serial.println(" - 🎯 **NUUP_Monitor ENCONTRADO!**");

        if (rssi < RSSI_MIN_APAREAMIENTO) {
            Serial.printf("      ⛔ RSSI %d dBm es demasiado débil: acércalo a ~5 cm (>= %d dBm) para emparejar.\n", rssi, RSSI_MIN_APAREAMIENTO);
            return;
        }

        advertisedDevice.getScan()->stop();
        myDevice = new BLEAdvertisedDevice(advertisedDevice);
        doConnect = true;
        
        // Información de distancia aproximada
        if (rssi >= -45) {
            Serial.println("      📍 Distancia estimada: ~5cm");
        } else if (rssi >= -60) {
            Serial.println("      📍 Distancia estimada: ~10cm");
        } else if (rssi >= -70) {
            Serial.println("      📍 Distancia estimada: ~20cm");
        } else if (rssi >= -80) {
            Serial.println("      📍 Distancia estimada: ~50cm");
        } else {
            Serial.println("      📍 Distancia estimada: >1m");
        }
    } else {
        if (deviceName.length() > 0) {
            Serial.println(" - Otro dispositivo");
        } else {
            Serial.println(" - Sin nombre");
        }
    }
}

bool connectToServer() {
    if (myDevice == nullptr) {
        Serial.println("❌ Error: No hay dispositivo para conectar");
        return false;
    }
    
    Serial.print("🔗 Conectando a: ");
    Serial.println(myDevice->getAddress().toString().c_str());
    
    int rssiInicial = myDevice->getRSSI();
    Serial.printf("📶 RSSI inicial: %d dBm\n", rssiInicial);
    
    pClient = BLEDevice::createClient();
    pClient->setClientCallbacks(new MyClientCallback());
    
    // Configurar potencia máxima para conexión
    BLEDevice::setPower(ESP_PWR_LVL_P7, ESP_BLE_PWR_TYPE_CONN_HDL0);
    
    Serial.println("⏳ Intentando conexión (3 segundos máximo)...");
    
    // Conectar con timeout más corto
    unsigned long startTime = millis();
    bool connected = false;
    
    while (millis() - startTime < 3000) {
        connected = pClient->connect(myDevice);
        if (connected) {
            Serial.println("✅ Conectado al servidor BLE");
            break;
        }
        delay(100);
        manejarLED(); // ACTUALIZAR LED DURANTE LA CONEXIÓN
    }
    
    if (!connected) {
        Serial.println("❌ Falló la conexión - Timeout de 3 segundos");
        return false;
    }
    
    // Obtener el servicio
    BLERemoteService* pRemoteService = pClient->getService(SERVICE_UUID);
    if (pRemoteService == nullptr) {
        Serial.println("❌ No se pudo encontrar el servicio");
        pClient->disconnect();
        return false;
    }
    
    // Obtener la característica
    pRemoteCharacteristic = pRemoteService->getCharacteristic(CHARACTERISTIC_UUID);
    if (pRemoteCharacteristic == nullptr) {
        Serial.println("❌ No se pudo encontrar la característica");
        pClient->disconnect();
        return false;
    }
    
    // Configurar notificaciones si está disponible
    if(pRemoteCharacteristic->canNotify()) {
        pRemoteCharacteristic->registerForNotify([](BLERemoteCharacteristic* pBLERemoteCharacteristic, 
                                                  uint8_t* pData, size_t length, bool isNotify) {
            // Crear buffer para el mensaje completo
            char* buffer = (char*)malloc(length + 1);
            if (buffer) {
                memcpy(buffer, pData, length);
                buffer[length] = '\0'; // Null terminator
                
                String respuesta = String(buffer);
                Serial.print("📥 Notificación recibida (");
                Serial.print(length);
                Serial.print(" bytes): ");
                Serial.println(respuesta);
                
                free(buffer);
                procesarComandoBLE(respuesta);
            }
        });
    }
    
    // Configurar MTU más grande para mensajes largos
    pClient->setMTU(100); // Aumentar MTU a 100 bytes
    
    deviceConnected = true;
    Serial.println("🎉 Conexión BLE establecida exitosamente");
    return true;
}

void sendCommand(String command) {
    if (pClient == nullptr) {
        Serial.println("❌ Error: Cliente BLE no inicializado");
        return;
    }
    
    if (!pClient->isConnected()) {
        Serial.println("❌ Error: Cliente BLE no conectado");
        deviceConnected = false;
        return;
    }
    
    if (pRemoteCharacteristic == nullptr) {
        Serial.println("❌ Error: Característica BLE no disponible");
        return;
    }
    
    if (!pRemoteCharacteristic->canWrite()) {
        Serial.println("❌ Error: No se puede escribir en la característica");
        return;
    }
    
    Serial.print("📤 Enviando comando: ");
    Serial.println(command);
    
    // Verificar longitud del comando
    if (command.length() > 100) {
        Serial.println("⚠️  Comando muy largo, podría truncarse");
    }
    
    // Enviar comando con manejo de errores
    try {
        pRemoteCharacteristic->writeValue(command.c_str(), command.length());
        Serial.println("✅ Comando enviado exitosamente");
        comandoPendiente = true;
    } 
    catch (const std::exception& e) {
        Serial.printf("❌ Excepción al enviar comando: %s\n", e.what());
        deviceConnected = false;
    }
    catch (...) {
        Serial.println("❌ Error desconocido al enviar comando");
        deviceConnected = false;
    }
}

void scanForDevices() {
    Serial.println("📡 INICIANDO ESCANEO BLE DETALLADO...");
    Serial.println("   ═══════════════════════════════════");
    Serial.printf("   - Servidor buscado: '%s'\n", targetDeviceName.c_str());
    Serial.printf("   - Duración: 4 segundos\n");
    Serial.printf("   - Potencia: Máxima\n");
    Serial.printf("   - Emparejamiento solo si RSSI >= %d dBm (equivalente a ~5 cm)\n", RSSI_MIN_APAREAMIENTO);
    Serial.println("   - Ajusta RSSI_MIN_APAREAMIENTO para acercar o alejar el rango BLE");
    Serial.println("   ═══════════════════════════════════");
    
    // Reiniciar flags
    doConnect = false;
    myDevice = nullptr;
    
    // Configurar escaneo
    BLEScan* pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true);
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);
    
    // Ejecutar escaneo
    BLEScanResults foundDevices = pBLEScan->start(4, false);
    
    // ⭐ MOSTRAR RESULTADOS DETALLADOS
    Serial.println("\n📊 RESULTADO DEL ESCANEO:");
    Serial.println("   ═══════════════════════════════════");
    Serial.printf("   - Total dispositivos encontrados: %d\n", foundDevices.getCount());
    
    if (foundDevices.getCount() == 0) {
        Serial.println("   ❌ NO se encontró NINGÚN dispositivo BLE");
        Serial.println("   💡 Verifica que:");
        Serial.println("      • El Bluetooth esté activado en dispositivos cercanos");
        Serial.println("      • Hayan dispositivos BLE en el área");
        Serial.println("      • La antena BLE del ESP32 funcione correctamente");
    } else if (!doConnect) {
        Serial.println("   ⚠️  Se encontraron dispositivos BLE, pero NO 'NUUP_Monitor'");
        Serial.println("   💡 Posibles causas:");
        Serial.println("      • El servidor NUUP_Monitor no está ejecutándose");
        Serial.println("      • El nombre del servidor es diferente");
        Serial.println("      • El servidor está fuera de rango");
    } else {
        Serial.println("   ✅ NUUP_Monitor ENCONTRADO - Listo para conectar");
    }
    Serial.println("   ═══════════════════════════════════\n");
    
    pBLEScan->clearResults();
}

void completarRegistro(String macServidor, String nombre, String alturaStr, String litrosStr) {
    Serial.println("\n💾 INICIANDO CIERRE DE REGISTRO...");

    // Este cierre de alta se dispara desde BLE (READY) usando la MAC que envía
    // el servidor y las variables globales de configuración vigentes. Luego
    // persiste todo en EEPROM y marca el flag de registrado para futuros arranques.
    // Guardar datos EN LAS VARIABLES
    memset(&dispositivo, 0, sizeof(dispositivo));
    strncpy(dispositivo.mac, macServidor.c_str(), sizeof(dispositivo.mac)-1);
    strncpy(dispositivo.nombre, nombre.c_str(), sizeof(dispositivo.nombre)-1);
    dispositivo.altura = alturaStr.toInt();
    dispositivo.litros = litrosStr.toInt();
    
    // ⭐⭐ ACTUALIZAR LAS VARIABLES GLOBALES TAMBIÉN
    nombreDispositivo = nombre;
    alturaDispositivo = dispositivo.altura;
    litrosDispositivo = dispositivo.litros;
    
    // Guardar en EEPROM
    guardarDatosEnEEPROM();
    registrado = true;
    EEPROM.write(EEPROM_ADDR_REGISTRADO, 1);
    bool commitSuccess = EEPROM.commit();
    
    Serial.printf("💿 EEPROM commit: %s\n", commitSuccess ? "ÉXITO" : "FALLO");
    
    Serial.println("🎉 REGISTRO COMPLETADO - Datos guardados:");
    imprimirDatosDispositivo();
    
    // ⭐⭐ DESCONECTAR BLE ANTES DE REINICIAR
    if (pClient != nullptr && pClient->isConnected()) {
        Serial.println("🔌 Desconectando BLE...");
        pClient->disconnect();
        delay(500);
    }
    
    // ⭐⭐ CONFIRMACIÓN VISUAL
    Serial.println("💡 LED APAGADO - Registro exitoso");
    digitalWrite(LED_PIN, LOW);
    
    // ⭐⭐ REINICIAR PARA APLICAR CAMBIOS
    Serial.println("🔄 Reiniciando en 3 segundos...");
    
    // Blink de confirmación
    for(int i = 0; i < 6; i++) {
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
        delay(PARPADEO_LED_RAPIDO_MS);
    }
    
    digitalWrite(LED_PIN, LOW); // Asegurar que queda apagado
    delay(3000);
    
    Serial.println("🚀 REINICIANDO...");
    ESP.restart();
}

void procesarComandoBLE(String comando) {
    Serial.println("\n=== INICIO PROCESAMIENTO BLE ===");
    Serial.print("📨 Comando RAW recibido: '");
    Serial.print(comando);
    Serial.println("'");

    // ⭐ TEMPORALMENTE DESACTIVAR WATCHDOG PARA OPERACIONES BLE
    esp_task_wdt_delete(NULL);

    // 1. PRIMER MENSAJE: Confirmación de registro con MAC del servidor
    if (comando.startsWith("OK_REG,")) {
        String macServidor = comando.substring(7);
        macServidor.trim();
        
        Serial.printf("✅ Confirmación de registro recibida - MAC Servidor: '%s'\n", macServidor.c_str());
        
        if (macServidor.length() == 17) {
            macRegistrada = macServidor;
            
            // ENCENDER LED
            digitalWrite(LED_PIN, HIGH);
            enProcesoRegistro = true;
            tiempoInicioRegistro = millis();
            
            Serial.println("💡 LED ENCENDIDO - Programando envío CONFIG...");
            
            // ⭐⭐ NO ENVIAR DIRECTAMENTE - PROGRAMAR PARA 
            
            pendienteEnvioConfig = true;
            tiempoProgramadoEnvio = millis() + DELAY_ENTRE_MENSAJES;
            
            Serial.printf("🔄 CONFIG programado para enviar en %d ms\n", DELAY_ENTRE_MENSAJES);
            
        } else {
            Serial.println("❌ MAC del servidor inválida");
        }
    }
    
    // 2. MENSAJE READY - REGISTRO COMPLETADO
    else if (comando == "READY") {
        Serial.println("\n🎉 READY RECIBIDO - CICLO COMPLETADO");
        
    enProcesoRegistro = false;
    esperandoDatosConfig = false;
    pendienteEnvioConfig = false;
        
    // ⭐⭐ EJECUTAR SECUENCIA LED PARA ALTA
    ejecutarSecuenciaLED("ALTA COMPLETADA");

    digitalWrite(LED_VERDE_PIN, HIGH);
    delay(DURACION_LED_CONFIRMACION_MS);
    digitalWrite(LED_VERDE_PIN, LOW);
    
    Serial.println("💾 Guardando datos en EEPROM...");
    completarRegistro(macRegistrada, nombreDispositivo, 
                      String(alturaDispositivo), String(litrosDispositivo));
}
    
    // 3. CONFIRMACIÓN DE BAJA EXITOSA
  else if (comando == "OK_BAJA") {
    Serial.println("\n✅ BAJA CONFIRMADA - Limpiando EEPROM...");

enProcesoRegistro = false;
    esperandoDatosConfig = false;
    pendienteEnvioConfig = false;
    bajaAutomaticaActivada = false;

    digitalWrite(LED_ROJO_PIN, HIGH);
    delay(DURACION_LED_CONFIRMACION_MS);
    digitalWrite(LED_ROJO_PIN, LOW);

    // Limpiar EEPROM y reiniciar
    limpiarEEPROMYReiniciar();
}
    
// ⭐⭐ NUEVO: MANEJAR "NO_EXISTE_MAC" - BAJA AUTOMÁTICA
    else if (comando == "ERROR:NO_EXISTE_MAC") {
        Serial.println("\n⚠️  EL DISPOSITIVO NO ESTÁ REGISTRADO EN EL SERVIDOR");
        Serial.println("💡 El servidor indica que la MAC no existe en su base de datos");
        Serial.println("🔄 Procediendo con baja automática local...");
        
        // ⭐⭐ LIMPIAR EEPROM Y REINICIAR DE TODAS FORMAS
        enProcesoRegistro = false;
        esperandoDatosConfig = false;
        pendienteEnvioConfig = false;
        bajaAutomaticaActivada = false;

        // Limpiar EEPROM y reiniciar
        limpiarEEPROMYReiniciar();
    }

    // 4. ERROR EN BAJA
    else if (comando.startsWith("ERROR:BAJA")) {
        Serial.println("\n❌ Error en baja: " + comando);
        
        enProcesoRegistro = false;
        esperandoDatosConfig = false;
        pendienteEnvioConfig = false;
        digitalWrite(LED_PIN, LOW);
        
        Serial.println("🔄 Reintentando baja en próximo escaneo...");
    }
    
    // 5. Mensajes de error generales
    else if (comando.startsWith("ERROR")) {
        Serial.print("❌ Error del servidor: ");
        Serial.println(comando);
        enProcesoRegistro = false;
        esperandoDatosConfig = false;
        pendienteEnvioConfig = false;
        digitalWrite(LED_PIN, LOW);
        
        Serial.println("🔄 Reiniciando estado para reintentar...");
        delay(2000);
    }
    
    // 6. Comando no reconocido
    else {
        Serial.println("❓ Comando no reconocido: " + comando);
    }
    
    comandoPendiente = false;
    
    // ⭐ RE-AGREGAR WATCHDOG ANTES DE SALIR
    esp_task_wdt_add(NULL);
    
    Serial.println("=== FIN PROCESAMIENTO BLE ===\n");
}

void debugEstadoBLE() {
    static unsigned long lastDebug = 0;
    if (millis() - lastDebug > 2000) {
        lastDebug = millis();
        
        Serial.println("\n🔍 DEBUG ESTADO BLE CLIENTE:");
        Serial.printf("   deviceConnected: %s\n", deviceConnected ? "SI" : "NO");
        Serial.printf("   doConnect: %s\n", doConnect ? "SI" : "NO");
        Serial.printf("   enProcesoRegistro: %s\n", enProcesoRegistro ? "SI" : "NO");
        Serial.printf("   esperandoDatosConfig: %s\n", esperandoDatosConfig ? "SI" : "NO");
        Serial.printf("   registrado: %s\n", registrado ? "SI" : "NO");
        
        if (pClient != nullptr) {
            Serial.printf("   pClient.isConnected: %s\n", pClient->isConnected() ? "SI" : "NO");
        }
        
        if (enProcesoRegistro) {
            unsigned long tiempoTranscurrido = millis() - tiempoInicioRegistro;
            Serial.printf("   ⏰ Tiempo proceso: %lu/%d seg\n", 
                        tiempoTranscurrido / 1000, TIMEOUT_REGISTRO_COMPLETO / 1000);
        }
        Serial.println();
    }
}

void verificarConexionYCercania() {
    if (deviceConnected && pClient != nullptr && pClient->isConnected()) {
        static unsigned long lastCheck = 0;
        if (millis() - lastCheck > 3000) {
            lastCheck = millis();
            int currentRSSI = pClient->getRssi();
            Serial.printf("📶 Monitorizando conexión - RSSI: %d dBm\n", currentRSSI);
        }
    }
}

void debugConexionBLE() {
    static unsigned long lastDebug = 0;
    if (millis() - lastDebug > 1000) {
        lastDebug = millis();
        
        Serial.println("\n📡 DEBUG BLE EN TIEMPO REAL:");
        Serial.printf("   deviceConnected: %s\n", deviceConnected ? "SI" : "NO");
        Serial.printf("   enProcesoRegistro: %s\n", enProcesoRegistro ? "SI" : "NO");
        Serial.printf("   esperandoDatosConfig: %s\n", esperandoDatosConfig ? "SI" : "NO");
        
        if (pClient != nullptr) {
            Serial.printf("   BLE Connected: %s\n", pClient->isConnected() ? "SI" : "NO");
            Serial.printf("   BLE RSSI: %d dBm\n", pClient->getRssi());
        } else {
            Serial.println("   BLE Client: NULL");
        }
        
        if (enProcesoRegistro) {
            unsigned long tiempo = millis() - tiempoInicioRegistro;
            Serial.printf("   ⏰ Tiempo registro: %lu/%d seg\n", tiempo/1000, TIMEOUT_REGISTRO_COMPLETO/1000);
        }
        Serial.println();
    }
}

// ============================================================================
// IMPLEMENTACIÓN DE FUNCIONES DEL SISTEMA
// ============================================================================

void parpadearLED(int pin, unsigned long intervalo, unsigned long duracion) {
    unsigned long inicio = millis();
    unsigned long ultimoCambio = inicio;
    bool estado = false;

    while (millis() - inicio < duracion) {
        if (millis() - ultimoCambio >= intervalo) {
            estado = !estado;
            digitalWrite(pin, estado);
            ultimoCambio = millis();
        }
        delay(10);
    }

    digitalWrite(pin, LOW);
}

void ejecutarSecuenciaLED(String tipoOperacion) {
    Serial.println("\n🎭 INICIANDO SECUENCIA LED - " + tipoOperacion);
    Serial.println("   Fase 1: 💡 LED FIJO (" + String(SECUENCIA_LED_FIJO/1000) + " segundos)");
    Serial.println("   Fase 2: ✨ LED PARPADEANDO (" + String(SECUENCIA_LED_PARPADEANTE/1000) + " segundos)");
    
    // ⭐⭐ FASE 1: LED ENCENDIDO FIJO
    Serial.println("\n💡 FASE 1 - LED ENCENDIDO");
    unsigned long inicioFase1 = millis();
    
    while (millis() - inicioFase1 < SECUENCIA_LED_FIJO) {
        digitalWrite(LED_VERDE_PIN, HIGH); // Siempre encendido
        
        // Mostrar progreso cada segundo
        static unsigned long ultimoDisplay = 0;
        if (millis() - ultimoDisplay > 1000) {
            ultimoDisplay = millis();
            unsigned long segundosTranscurridos = (millis() - inicioFase1) / 1000;
            unsigned long segundosRestantes = (SECUENCIA_LED_FIJO / 1000) - segundosTranscurridos;
            Serial.printf("   ⏰ %lu segundos restantes - LED FIJO\n", segundosRestantes);
        }
        delay(100);
    }
    
    // ⭐⭐ FASE 2: LED PARPADEANDO RÁPIDO
    Serial.println("✨ FASE 2 - LED PARPADEANDO");
    unsigned long inicioFase2 = millis();
    unsigned long ultimoCambioLED = inicioFase2;
    bool estadoLED = true;
    digitalWrite(LED_PIN, HIGH); // Empezar encendido
    
    while (millis() - inicioFase2 < SECUENCIA_LED_PARPADEANTE) {
        // Parpadeo rápido cada 200ms
        if (millis() - ultimoCambioLED >= SECUENCIA_INTERVALO_PARPADEO) {
            estadoLED = !estadoLED;
            digitalWrite(LED_VERDE_PIN, estadoLED);
            ultimoCambioLED = millis();
        }
        
        // Mostrar progreso cada segundo
        static unsigned long ultimoDisplay = 0;
        if (millis() - ultimoDisplay > 1000) {
            ultimoDisplay = millis();
            unsigned long segundosTranscurridos = (millis() - inicioFase2) / 1000;
            unsigned long segundosRestantes = (SECUENCIA_LED_PARPADEANTE / 1000) - segundosTranscurridos;
            String estado = estadoLED ? "ON" : "OFF";
            Serial.printf("   ⏰ %lu segundos restantes - LED %s\n", segundosRestantes, estado.c_str());
        }
        delay(50);
    }
    
    // ⭐⭐ FINAL: APAGAR LED
    digitalWrite(LED_VERDE_PIN, LOW);
    Serial.println("✅ SECUENCIA LED COMPLETADA - " + tipoOperacion);
}

void manejarLED() {
    // ⭐⭐ MODO BAJA: LED ROJO PARPADEANDO (configurable)
    if (bajaAutomaticaActivada) {
        if (millis() - ultimoCambioLedRojo >= PARPADEO_LED_RAPIDO_MS) {
            estadoLedRojo = !estadoLedRojo;
            digitalWrite(LED_ROJO_PIN, estadoLedRojo);
            ultimoCambioLedRojo = millis();
        }

        digitalWrite(LED_VERDE_PIN, LOW);
        return;
    }

    // ⭐⭐ MODO ESPERA DESPUÉS DE BAJA: LED ROJO PARPADEO LENTO
    if (esperaDespuesBaja) {
        if (millis() - ultimoCambioLedRojo >= PARPADEO_LED_LENTO_MS) {
            estadoLedRojo = !estadoLedRojo;
            digitalWrite(LED_ROJO_PIN, estadoLedRojo);
            ultimoCambioLedRojo = millis();
        }

        digitalWrite(LED_VERDE_PIN, LOW);
        return;
    }

    if (enProcesoRegistro && !bajaAutomaticaActivada) {
        static unsigned long ultimoBlinkRegistro = 0;
        static bool estadoRegistro = false;
        unsigned long transcurrido = millis() - tiempoInicioRegistro;

        if (transcurrido <= DURACION_PARPADEO_EMPAREJANDO_MS) {
            if (millis() - ultimoBlinkRegistro >= INTERVALO_PARPADEO_EMPAREJANDO_MS) {
                estadoRegistro = !estadoRegistro;
                digitalWrite(LED_VERDE_PIN, estadoRegistro);
                ultimoBlinkRegistro = millis();
            }
        } else {
            digitalWrite(LED_VERDE_PIN, LOW);
        }

        digitalWrite(LED_ROJO_PIN, LOW);
        return;
    }

    // ⭐⭐ WAKE POR IMPACTO: LED VERDE PARPADEANDO EN ESPERA O FIJO CON CLIENTE
    if (wakeByImpact) {
        static unsigned long ultimoBlinkImpacto = 0;
        static bool estadoImpacto = false;

        bool clienteWiFiActivo = modoConfiguracionActivo || WiFi.softAPgetStationNum() > 0;

        if (clienteWiFiActivo) {
            digitalWrite(LED_VERDE_PIN, HIGH); // Usuario en portal: LED fijo
        } else {
            if (millis() - ultimoBlinkImpacto >= PARPADEO_LED_LENTO_MS) {
                estadoImpacto = !estadoImpacto;
                ultimoBlinkImpacto = millis();
            }
            digitalWrite(LED_VERDE_PIN, estadoImpacto);
        }

        digitalWrite(LED_ROJO_PIN, LOW);
        return;
    }

    if (!registrado) {
        // NO REGISTRADO: LED ROJO ENCENDIDO FIJO (o parpadeo corto al despertar por impacto)
        if (wakeByImpact) {
            if (millis() - ultimoCambioLedRojo >= PARPADEO_WAKE_ROJO_MS) {
                estadoLedRojo = !estadoLedRojo;
                digitalWrite(LED_ROJO_PIN, estadoLedRojo);
                ultimoCambioLedRojo = millis();
            }
        } else {
            digitalWrite(LED_ROJO_PIN, HIGH);
        }
        digitalWrite(LED_VERDE_PIN, LOW);
        return;
    }

    // REGISTRADO Y SIN EVENTOS: AMBOS LEDs APAGADOS
    digitalWrite(LED_ROJO_PIN, LOW);
    digitalWrite(LED_VERDE_PIN, LOW);
}

void modoEsperaDespuesBaja() {
    static unsigned long ultimoCambioEspera = 0;
    static bool estadoLEDEspera = true;
    static unsigned long ultimoDisplay = 0;
    
    unsigned long tiempoEspera = millis() - tiempoFinBaja;
    unsigned long segundosRestantes = (TIEMPO_ESPERA_DESPUES_BAJA - tiempoEspera) / 1000;
    
    // ⭐⭐ LED PARPADEO LENTO DURANTE ESPERA (configurable)
    if (millis() - ultimoCambioEspera >= PARPADEO_LED_LENTO_MS) {
        estadoLEDEspera = !estadoLEDEspera;
        digitalWrite(LED_ROJO_PIN, estadoLEDEspera);
        ultimoCambioEspera = millis();
    }
    
    // ⭐⭐ MOSTRAR CONTADOR CADA SEGUNDO
    if (millis() - ultimoDisplay > 1000) {
        ultimoDisplay = millis();
        Serial.printf("⏳ Espera después de baja: %lu segundos restantes ", segundosRestantes);
        
        // Barra de progreso
        int progreso = map(tiempoEspera, 0, TIEMPO_ESPERA_DESPUES_BAJA, 0, 20);
        Serial.print("[");
        for (int i = 0; i < 20; i++) {
            if (i < progreso) Serial.print("=");
            else Serial.print(" ");
        }
        Serial.println("]");
    }
    
    // Verificar si terminó el periodo de espera
    if (tiempoEspera >= TIEMPO_ESPERA_DESPUES_BAJA) {
        esperaDespuesBaja = false;
        tiempoFinBaja = 0;
        digitalWrite(LED_ROJO_PIN, LOW);
        Serial.println("✅✅✅ PERIODO DE ESPERA FINALIZADO - LISTO PARA ALTA ✅✅✅");
    }
}

// ============================================================================
// IMPLEMENTACIÓN DE FUNCIONES IA
// ============================================================================

bool detectarNivelBajo(int litrosActuales) {
    // Asegúrate de que 'dispositivo' está declarado en tu código
    float porcentajeActual = (float)litrosActuales / dispositivo.litros * 100;
    
    if (porcentajeActual < 20) {
        estadisticas.medicionesConsecutivasBajas++;
    } else {
        estadisticas.medicionesConsecutivasBajas = 0;
    }
    
    if (estadisticas.medicionesConsecutivasBajas >= (UMBRAL_BAJO_NIVEL * 12)) {
        Serial.println("🔻 DETECTADO: Nivel bajo por " + String(UMBRAL_BAJO_NIVEL) + " horas consecutivas");
        return true;
    }
    
    return false;
}

void agregarAlHistorial(int litros) {
    estadisticas.historial24h[estadisticas.indiceHistorial].timestamp = millis();
    estadisticas.historial24h[estadisticas.indiceHistorial].litros = litros;
    
    estadisticas.indiceHistorial = (estadisticas.indiceHistorial + 1) % 288;
    if (estadisticas.totalMediciones < 288) {
        estadisticas.totalMediciones++;
    }
}

void analizarPatronesConsumo(int litrosActuales) {
    if (estadisticas.totalMediciones < 12) {
        // No hay suficientes datos aún
        analisisActual.notificacion = 0;
        analisisActual.mensajeNotificacion = "0";
        calcularPromediosBasicos();
        return;
    }
    
    // Reiniciar análisis
    analisisActual.notificacion = 0;
    analisisActual.mensajeNotificacion = "0";
    
    // Calcular promedios
    calcularPromediosBasicos();
    
    // 1. Detectar posible fuga
    if (detectarPosibleFuga()) {
        analisisActual.notificacion = 1;
        analisisActual.mensajeNotificacion = "Posible Fuga";
        return; // Prioridad máxima
    }
    
    // 2. Detectar consumo irregular
    if (detectarConsumoIrregular()) {
        analisisActual.notificacion = 2;
        analisisActual.mensajeNotificacion = "Consumo Irregular";
        return;
    }
    
    // 3. Detectar nivel bajo por mucho tiempo
    if (detectarNivelBajo(litrosActuales)) {
        analisisActual.notificacion = 3;
        analisisActual.mensajeNotificacion = "Nivel bajo demasiado tiempo";
        return;
    }
    
    // Estado normal
    analisisActual.notificacion = 0;
    analisisActual.mensajeNotificacion = "0";
}

void calcularPromediosBasicos() {
    float totalConsumo = 0;
    int countConsumo = 0;
    
    // Calcular consumo de las últimas 24 horas
    for (int i = 1; i < estadisticas.totalMediciones; i++) {
        int consumo = estadisticas.historial24h[i-1].litros - estadisticas.historial24h[i].litros;
        if (consumo > 0) {
            totalConsumo += consumo;
            countConsumo++;
        }
    }
    
    if (countConsumo > 0) {
        analisisActual.promedioDiario = totalConsumo;
        analisisActual.promedioSemanal = totalConsumo * 7;
        analisisActual.promedioMensual = totalConsumo * 30;
    } else {
        analisisActual.promedioDiario = 0;
        analisisActual.promedioSemanal = 0;
        analisisActual.promedioMensual = 0;
    }
}

bool detectarPosibleFuga() {
    int horasConConsumoConstante = 0;
    
    // Verificar consumo constante durante varias horas
    for (int i = 1; i < estadisticas.totalMediciones ; i++) {
        int consumo = estadisticas.historial24h[i-1].litros - estadisticas.historial24h[i].litros;
        
        // Si hay consumo constante (más de 1 litro) en mediciones consecutivas
        if (consumo > 1) {
            estadisticas.consumoConsecutivo++;
        } else {
            estadisticas.consumoConsecutivo = 0;
        }
        
        // Cada 12 mediciones = 1 hora (si se mide cada 5 min)
        if (i % 12 == 0) {
            if (estadisticas.consumoConsecutivo >= 10) { // 10 de 12 mediciones con consumo
                horasConConsumoConstante++;
            } else {
                horasConConsumoConstante = 0;
            }
        }
        
        // Si hay consumo constante por más de UMBRAL_FUGA horas, posible fuga
        if (horasConConsumoConstante >= UMBRAL_FUGA) {
            Serial.println("🚨 DETECTADO: Posible fuga - Consumo constante por " + String(horasConConsumoConstante) + " horas");
            return true;
        }
    }
    
    return false;
}

bool detectarConsumoIrregular() {
    if (estadisticas.totalMediciones < 24) return false;
    
    float consumoPorHora[24] = {0};  // Array estático, NO std::vector
    int horasConDatos = 0;
    
    for (int hora = 0; hora < min(24, estadisticas.totalMediciones / 12); hora++) {
        for (int minuto = 0; minuto < 12; minuto++) {
            int index = (estadisticas.indiceHistorial - (hora * 12 + minuto) + 288) % 288;
            int prevIndex = (index - 1 + 288) % 288;
            
            if (prevIndex >= 0 && prevIndex < 288) {
                int consumo = estadisticas.historial24h[prevIndex].litros - 
                             estadisticas.historial24h[index].litros;
                if (consumo > 0) {
                    consumoPorHora[hora] += consumo;
                }
            }
        }
        if (consumoPorHora[hora] > 0) horasConDatos++;
    }
    
    if (horasConDatos >= 3) {
        float promedio = 0;
        for (int i = 0; i < horasConDatos; i++) {
            promedio += consumoPorHora[i];
        }
        promedio /= horasConDatos;
        
        for (int i = 0; i < horasConDatos; i++) {
            if (consumoPorHora[i] > promedio * 3 || (consumoPorHora[i] < promedio * 0.3 && promedio > 10)) {
                Serial.println("⚠️ DETECTADO: Consumo irregular - Variación significativa detectada");
                return true;
            }
        }
    }
    
    return false;
}

String obtenerDatosIA() {
    String datosIA = String(analisisActual.notificacion) + "," +
                    analisisActual.mensajeNotificacion + "," +
                    String(analisisActual.promedioDiario, 1) + "," +
                    String(analisisActual.promedioSemanal, 1) + "," +
                    String(analisisActual.promedioMensual, 1);
    
    return datosIA;
}

// ============================================================================
// FUNCIONES PRINCIPALES SETUP Y LOOP
// ============================================================================

void setup() {
    // Configurar Watchdog Timer
    esp_task_wdt_init(30, true);
    esp_task_wdt_add(NULL);

    // ⭐ VERIFICAR CAUSA DE WAKEUP
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    
    Serial.begin(115200);
    Serial.println("\n🚀 ESP32 Iniciando cliente...");
    
    // Mostrar causa del wakeup
    switch(wakeup_reason) {
        case ESP_SLEEP_WAKEUP_EXT0:
            Serial.println("📅 Wakeup por BOTÓN");
            wakeByImpact = true;
            break;
        case ESP_SLEEP_WAKEUP_TIMER:
            Serial.println("⏰ Wakeup por TIMER - Ciclo normal");
            break;
        default:
            Serial.println("🔌 Wakeup por RESET/ALIMENTACIÓN");
            break;
    }

    if (wakeByImpact) {
        Serial.println("⚡ Wake por sensor de impacto - habilitando BLE/WiFi solo en este ciclo");
    }

    // Inicializar EEPROM

    EEPROM.begin(EEPROM_SIZE);
    if (LIMPIEZA_FABRICA_EN_SETUP) {
        Serial.println("🧹 LIMPIEZA DE FÁBRICA ACTIVADA (bandera inicial)");
        limpiarEEPROMYReiniciar();
    }
    Serial.println("💾 EEPROM inicializada");

    // Verificar estado de registro
    registrado = EEPROM.read(EEPROM_ADDR_REGISTRADO) == 1;
    Serial.printf("📋 Estado de registro: %s\n", registrado ? "REGISTRADO" : "NO REGISTRADO");

    // Cargar datos almacenados incluso si no está registrado para que las
    // ediciones web persistan entre reinicios; si la EEPROM está vacía se
    // restauran los valores de fábrica.
    leerDatosDeEEPROM();

    recalibrarPotenciaLoRa = wakeByImpact;
    if (recalibrarPotenciaLoRa) {
        potenciaLoRaActualDbm = LORA_POTENCIA_MIN_DBM;
        dispositivo.potenciaLoRaDbm = potenciaLoRaActualDbm;
    }

    // ⭐⭐ INICIALIZAR BLE INMEDIATAMENTE
    Serial.println("📱 INICIANDO BLE...");
    BLEDevice::init("NUUP_Controller");
    BLEDevice::setPower(ESP_PWR_LVL_P7, ESP_BLE_PWR_TYPE_DEFAULT);
    Serial.println("✅ BLE inicializado");

    // Inicializar pines
    pinMode(trigPin, OUTPUT);
    pinMode(echoPin, INPUT);
    pinMode(LED_PIN, OUTPUT);
    pinMode(LED_ROJO_PIN, OUTPUT);
    pinMode(SENSOR_IMPACTO_PIN, INPUT_PULLUP);
    digitalWrite(LED_PIN, LOW);
    digitalWrite(LED_ROJO_PIN, LOW);

    if (wakeByImpact) {
        inicioVigiliaImpacto = millis();
        if (!confirmarGolpesImpacto()) {
            Serial.println("⚠️  Golpes insuficientes o fuera de rango (1-3). Volviendo a dormir...");
            wakeByImpact = false;
            prepararParaDeepSleep();
            esp_deep_sleep_start();
        }
    }

    if (wakeByImpact && !registrado) {
        parpadearLED(LED_ROJO_PIN, PARPADEO_WAKE_ROJO_MS, DURACION_PARPADEO_WAKE_MS);
    }

    // Parpadeo inicial de ambos LEDs durante 3 segundos
    unsigned long inicioParpadeo = millis();
    bool estadoParpadeo = false;
    while (millis() - inicioParpadeo < 3000) {
        digitalWrite(LED_VERDE_PIN, estadoParpadeo);
        digitalWrite(LED_ROJO_PIN, estadoParpadeo);
        estadoParpadeo = !estadoParpadeo;
        delay(PARPADEO_LED_RAPIDO_MS);
    }
    digitalWrite(LED_VERDE_PIN, LOW);
    digitalWrite(LED_ROJO_PIN, LOW);
    
    // ⭐⭐ FORZAR PRIMER ESCANEO BLE INMEDIATO
    ultimoEscaneoBLE = 0; // Esto forzará el escaneo inmediatamente
    
    // Configurar LED
    if (registrado) {
        digitalWrite(LED_PIN, LOW);
        Serial.println("💡 LED APAGADO - Modo registrado");
    }
    
    // Obtener MAC
    macAddress = WiFi.macAddress();
    macAddress.replace("-", ":");
    Serial.print("📟 MAC: ");
    Serial.println(macAddress);

    if (registrado) {
        Serial.println("✅ Dispositivo registrado - Operación normal");
        imprimirDatosDispositivo();
    } else {
        Serial.println("🔍 Dispositivo NO registrado - Modo búsqueda activa");
    }

    // LED de inicio
    if (registrado) {
        for(int i = 0; i < 4; i++) {
            digitalWrite(LED_PIN, !digitalRead(LED_PIN));
            delay(100);
        }
        digitalWrite(LED_PIN, LOW);
    } else {
        digitalWrite(LED_ROJO_PIN, HIGH);
    }

    // Inicialización LoRa
    Serial.println("📡 INICIANDO LoRa...");
    iniciarLoRaConReintentos();

    // Configurar para medición
    ultimoEnvioDatos = -INTERVALO_ENVIO_FORZOSO;

    // ⭐⭐ INICIALIZAR WiFi AP Y SERVIDOR WEB
    Serial.println("\n🌐 INICIANDO SERVICIOS WiFi...");
    configurarWiFiAP();

    Serial.println("\n🎯 ESTRATEGIA OPERATIVA:");
    Serial.println("   ==========================");
    Serial.printf("   📱 BLE: Escaneo cada %d segundos\n", INTERVALO_ESCANEO_BAJA/1000);
    Serial.printf("   📊 Sensor: Medición cada %d segundos (cambios) / %d segundos (forzoso)\n", INTERVALO_ENVIO_CAMBIO/1000, INTERVALO_ENVIO_FORZOSO/1000);
    Serial.printf("   🌐 WiFi: Servidor web SIEMPRE ACTIVO\n");
    Serial.printf("   📍 IP: %s\n", WiFi.softAPIP().toString().c_str());
    Serial.printf("   😴 Sleep: %d segundos entre ciclos máximos\n", INTERVALO_ENVIO_FORZOSO/1000);
    Serial.println("   ==========================");
    
    Serial.println("✅ SETUP COMPLETADO - Primer escaneo BLE INMEDIATO");

    // ⭐⭐ DIAGNÓSTICO COMPLETO DEL ENTORNO BLE
    Serial.println("\n🔍 ===========================================");
    Serial.println("🎯 DIAGNÓSTICO BLE INICIAL");
    Serial.println("🔍 ===========================================");
    
    // Verificar estado del Bluetooth
    Serial.println("📱 ESTADO BLE:");
    Serial.printf("   - MAC Local: %s\n", macAddress.c_str());
    Serial.printf("   - Dispositivo registrado: %s\n", registrado ? "SI" : "NO");
    Serial.printf("   - Servidor buscado: '%s'\n", targetDeviceName.c_str());
    Serial.printf("   - UUID Servicio: %s\n", SERVICE_UUID);
    Serial.printf("   - UUID Característica: %s\n", CHARACTERISTIC_UUID);
    
    // Test inicial de BLE
    Serial.println("   - Estado BLE: INICIALIZADO ✅");
    
    // Forzar primer escaneo BLE inmediato
    ultimoEscaneoBLE = 0;
    
    Serial.println("🔍 ===========================================\n");

    // ⭐ INICIALIZAR SISTEMA DE IA
    Serial.println("🧠 INICIALIZANDO SISTEMA DE INTELIGENCIA ARTIFICIAL...");
    estadisticas.ultimoResetEstadisticas = millis();
    analisisActual.notificacion = 0;
    analisisActual.mensajeNotificacion = "0";
    analisisActual.promedioDiario = 0;
    analisisActual.promedioSemanal = 0;
    analisisActual.promedioMensual = 0;
    
    Serial.println("✅ IA inicializada - Sistema de notificaciones activo");
    Serial.println("   Notificaciones: 0=Normal, 1=Posible Fuga, 2=Consumo Irregular, 3=Nivel bajo");
}

void loop() {
    // ⭐⭐ PRIMERO VERIFICAR SI ESTAMOS EN ESPERA DESPUÉS DE BAJA
    if (esperaDespuesBaja) {
        unsigned long tiempoEspera = millis() - tiempoFinBaja;
        
        if (tiempoEspera < TIEMPO_ESPERA_DESPUES_BAJA) {
            // MANTENER LED ENCENDIDO Y MOSTRAR CONTADOR
            digitalWrite(LED_PIN, HIGH);
            
            static unsigned long ultimoDisplay = 0;
            if (millis() - ultimoDisplay > 1000) {
                ultimoDisplay = millis();
                unsigned long segundosRestantes = (TIEMPO_ESPERA_DESPUES_BAJA - tiempoEspera) / 1000;
                Serial.printf("⏳ Espera después de baja: %lu segundos restantes...\n", segundosRestantes);
            }
            
            delay(500);
            return; // ⭐⭐ NO HACER NADA MÁS DURANTE LA ESPERA
        } else {
            // ⭐⭐ FINALIZAR PERIODO DE ESPERA
            esperaDespuesBaja = false;
            tiempoFinBaja = 0;
            Serial.println("✅ Periodo de espera finalizado - Iniciando modo alta normal");
            digitalWrite(LED_PIN, LOW);
        }
    }

    // ⭐ ALIMENTAR WATCHDOG
    esp_task_wdt_reset();

    bool comunicacionesHabilitadas = wakeByImpact || !registrado || modoConfiguracionActivo;

    // ⭐⭐ PRIORIDAD 1: VERIFICAR CLIENTES WiFi
    if (comunicacionesHabilitadas) {
        verificarConexionCliente();
    }

    // ⭐⭐ PRIORIDAD 2: ATENDER SERVIDOR WEB (SOLO SI HAY COMUNICACIÓN HABILITADA)
    if (comunicacionesHabilitadas) {
        server.handleClient();
        dnsServer.processNextRequest();
    }

    // ⭐⭐ PRIORIDAD 3: MANEJAR LED
    manejarLED();

    // ⭐⭐ PRIORIDAD 4: VERIFICAR Y ENVIAR CONFIG PENDIENTE BLE
    if (comunicacionesHabilitadas && pendienteEnvioConfig && millis() >= tiempoProgramadoEnvio) {
        Serial.println("\n🎯 EJECUTANDO ENVÍO CONFIG PROGRAMADO...");

        if (deviceConnected && pClient != nullptr && pClient->isConnected()) {
            // ⭐⭐ USAR LAS VARIABLES EN LUGAR DE VALORES FIJOS
            String configCommand = "CONFIG," + nombreDispositivo + "," +
                                  String(alturaDispositivo) + "," +
                                  String(litrosDispositivo);

            Serial.println("📤 CONFIG enviado: " + configCommand);
            Serial.printf("✅ Usando variables: Nombre='%s', Altura=%d, Litros=%d\n",
                        nombreDispositivo.c_str(), alturaDispositivo, litrosDispositivo);

            sendCommand(configCommand);
            pendienteEnvioConfig = false;

            // Esperar respuesta
            unsigned long inicioEspera = millis();
            while (millis() - inicioEspera < 5000) {
                delay(100);
                server.handleClient(); // ⭐ MANTENER SERVIDOR WEB ACTIVO
                if (comandoPendiente) break;
            }
        } else {
            Serial.println("❌ No se puede enviar CONFIG - BLE desconectado");
            pendienteEnvioConfig = false;
            enProcesoRegistro = false;
        }
    }

    // ⭐⭐ PRIORIDAD 5: BLE PERIÓDICO
    unsigned long tiempoActual = millis();
    unsigned long tiempoDesdeEscaneoBLE = tiempoActual - ultimoEscaneoBLE;
    unsigned long intervaloEscaneo = registrado ? INTERVALO_ESCANEO_BAJA : INTERVALO_ESCANEO_ALTA;

    if (comunicacionesHabilitadas) {
        // Ejecutar BLE si es tiempo
        if (ultimoEscaneoBLE == 0 || tiempoDesdeEscaneoBLE >= intervaloEscaneo) {
            Serial.println("\n🔍 ===========================================");
            Serial.println("🎯 INICIANDO ESCANEO BLE");
            Serial.printf("   Modo: %s\n", registrado ? "SOLICITAR BAJA" : "SOLICITAR REGISTRO");
            Serial.printf("   Tiempo desde último escaneo: %d seg\n", tiempoDesdeEscaneoBLE / 1000);
            Serial.println("🔍 ===========================================");

            scanForDevices();

            if (doConnect) {
                Serial.println("\n✅ SERVICIO BLE ENCONTRADO - Conectando...");
                if (connectToServer()) {
                    if (!registrado) {
                        // MODO REGISTRO
                        String solicitudRegistro = "REG:" + macAddress;
                        sendCommand(solicitudRegistro);
                        Serial.println("📤 ENVIANDO SOLICITUD DE REGISTRO: " + solicitudRegistro);
                        enProcesoRegistro = true;
                        tiempoInicioRegistro = millis();
                        Serial.println("⏳ Esperando confirmación de registro...");
                    } else {
                        // MODO BAJA
                        String solicitudBaja = "BAJA:" + macAddress;
                        sendCommand(solicitudBaja);
                        Serial.println("📤 ENVIANDO SOLICITUD DE BAJA: " + solicitudBaja);
                        bajaAutomaticaActivada = true;
                        tiempoInicioBaja = millis();
                        enProcesoRegistro = true;
                        Serial.println("⏳ Esperando confirmación de baja...");
                    }
                } else {
                    Serial.println("❌ FALLO EN CONEXIÓN BLE");
                }
            } else {
                Serial.println("❌ SERVIDOR NUUP_Monitor NO ENCONTRADO");
            }

            ultimoEscaneoBLE = millis();
            Serial.printf("🔍 Próximo escaneo BLE en: %d segundos\n\n", intervaloEscaneo / 1000);

            // ⭐ SI SE ENCONTRÓ BLE, NO CONTINUAR INMEDIATAMENTE
            if (doConnect) {
                delay(100);
                return;
            }
        }
    }

    // ⭐⭐ PRIORIDAD 6: PROCESAR COMUNICACIÓN BLE ACTIVA
    if (comunicacionesHabilitadas && (enProcesoRegistro || bajaAutomaticaActivada || deviceConnected)) {
        static unsigned long ultimoUpdate = 0;
        if (millis() - ultimoUpdate > 2000) {
            ultimoUpdate = millis();

            if (bajaAutomaticaActivada) {
                unsigned long tiempoBaja = millis() - tiempoInicioBaja;
                Serial.printf("⏳ Procesando BAJA: %lu/%d segundos\n",
                            tiempoBaja / 1000, TIMEOUT_BAJA / 1000);

                if (tiempoBaja > TIMEOUT_BAJA) {
                    Serial.println("⏰ TIMEOUT BAJA - Cancelando proceso");
                    bajaAutomaticaActivada = false;
                    enProcesoRegistro = false;
                    if (pClient != nullptr && pClient->isConnected()) {
                        pClient->disconnect();
                    }
                    deviceConnected = false;
                    doConnect = false;
                }
            }

            if (enProcesoRegistro && !bajaAutomaticaActivada) {
                unsigned long tiempoRegistro = millis() - tiempoInicioRegistro;
                Serial.printf("⏳ Procesando REGISTRO: %lu/%d segundos\n",
                            tiempoRegistro / 1000, TIMEOUT_REGISTRO_COMPLETO / 1000);

                if (tiempoRegistro > TIMEOUT_REGISTRO_COMPLETO) {
                    Serial.println("⏰ TIMEOUT REGISTRO - Reiniciando...");
                    ESP.restart();
                }
            }
        }
        delay(100);
        return;
    }

    // ⭐ MOSTRAR ESTADO ACTUAL CADA 10 SEGUNDOS
    static unsigned long ultimoEstado = 0;
    if (millis() - ultimoEstado > 10000) {
        ultimoEstado = millis();
        unsigned long tiempoHastaProximoBLE = intervaloEscaneo - (millis() - ultimoEscaneoBLE);
        Serial.printf("\n📊 ESTADO: BLE=%s, Próximo BLE en: %d seg\n",
                    (deviceConnected || enProcesoRegistro || bajaAutomaticaActivada) ? "ACTIVO" : "INACTIVO",
                    tiempoHastaProximoBLE / 1000);
    }

    // ⭐⭐ PRIORIDAD 7: MEDICIÓN DE SENSOR (solo si está registrado y no hay BLE activo)
    if (registrado && !enProcesoRegistro && !bajaAutomaticaActivada && (!wakeByImpact || recalibrarPotenciaLoRa)) {
        unsigned long tiempoDesdeMedicion = millis() - ultimoEnvioDatos;

        if (ultimoEnvioDatos < 0 || tiempoDesdeMedicion >= intervaloEnvioActual) {
            Serial.println("\n📊 ===========================================");
            Serial.println("🎯 INICIANDO MEDICIÓN DE SENSOR");
            Serial.printf("   Tiempo desde última medición: %d seg\n", tiempoDesdeMedicion / 1000);
            Serial.println("📊 ===========================================");
            
            int distancia = obtenerDistanciaValida();
            bool confirmado = enviarDatos(distancia);
            if (!confirmado) {
                Serial.println("❌ Sin confirmación tras reintentos. Reiniciando para reanudar ciclo.");
                ESP.restart();
            }
            ultimoEnvioDatos = millis();

            Serial.printf("✅ Medición completada. Próxima en: %d segundos\n\n", intervaloEnvioActual / 1000);

            Serial.println("😴 Programando deep sleep hasta el próximo ciclo LoRa...");
            wakeByImpact = false;
            entrarDeepSleep();
        }
    }

    // ⭐⭐ PRIORIDAD 8: VERIFICAR SLEEP (SOLO SI ESTÁ REGISTRADO)
    if (registrado && !enProcesoRegistro && !bajaAutomaticaActivada && !modoConfiguracionActivo && !wakeByImpact) {
        unsigned long tiempoDesdeEnvio = millis() - ultimoEnvioDatos;
        unsigned long tiempoDesdeBLE = millis() - ultimoEscaneoBLE;
        unsigned long intervaloEscaneo = registrado ? INTERVALO_ESCANEO_BAJA : INTERVALO_ESCANEO_ALTA;
        
        // Solo dormir si han pasado al menos 5 segundos desde actividades
        if (tiempoDesdeEnvio >= 5000 && tiempoDesdeBLE >= 5000) {
            unsigned long tiempoHastaProximoBLE = intervaloEscaneo - tiempoDesdeBLE;
            
            if (tiempoHastaProximoBLE > 10000) {
                unsigned long sleepTime = intervaloEnvioActual - tiempoDesdeEnvio;
                
                if (sleepTime > 5000) {
                    Serial.println("\n😴 ENTRANDO EN DEEP SLEEP...");
                    prepararParaDeepSleep();
                    esp_deep_sleep_start();
                }
            }
        }
    } else if (!registrado) {
        // ⭐⭐ MODO SIN ALTA: NO HACER DEEP SLEEP, SOLO DELAY NORMAL
        static unsigned long ultimoEstado = 0;
        if (millis() - ultimoEstado > 10000) {
            ultimoEstado = millis();
            Serial.println("🔍 MODO BÚSQUEDA ACTIVA - Sin Deep Sleep");
        }
        delay(500); // Delay cooperativo normal
    } else if (wakeByImpact) {
        if (inicioVigiliaImpacto == 0) {
            inicioVigiliaImpacto = millis();
        }

        bool sesionAPActiva = modoConfiguracionActivo || WiFi.softAPgetStationNum() > 0;
        bool enlaceBLEActivo = deviceConnected || enProcesoRegistro || bajaAutomaticaActivada;

        if (sesionAPActiva || enlaceBLEActivo) {
            static bool avisoMantenerseDespierto = false;
            if (!avisoMantenerseDespierto) {
                Serial.println("⏳ Modo impacto activo - manteniendo AP/BLE despiertos");
                avisoMantenerseDespierto = true;
            }
            delay(50);
            return;
        }

        if (millis() - inicioVigiliaImpacto < IMPACTO_TIEMPO_VIGILIA_MS) {
            delay(50);
            return;
        }

        Serial.println("🔁 Vigilia por impacto finalizada - Reiniciando dispositivo");
        wakeByImpact = false;
        delay(250);
        ESP.restart();
    }
    
    // ⭐ DELAY OPTIMIZADO PARA COOPERATIVIDAD
    delay(50);
}

// ============================================================================
// FUNCIONES RESTANTES DEL SISTEMA
// ============================================================================

void onTxDone() {
    Serial.println("📤 Callback: Transmisión LoRa completada");
}

float measureDistance() {
    // Limpiar trigger
    digitalWrite(trigPin, LOW);
    delayMicroseconds(3);
    
    // Pulso de 10µs
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);
    
    // Medir eco
    unsigned long duration = pulseIn(echoPin, HIGH, 35000); // 35ms timeout
    
    if (duration == 0) {
        return -1.0; // Timeout
    }
    
    float distance = duration * 0.0343 / 2.0;
    
    // ⭐⭐ RANGO MÁS AMPLIO: aceptar hasta 400cm para depósitos grandes
    if (distance < 1.0 || distance > 400.0) {
        return -1.0;
    }
    
    return distance;
}

void establecerValoresDeFabrica() {
    memset(&dispositivo, 0, sizeof(dispositivo));
    strncpy(dispositivo.nombre, "Deposito estandar", sizeof(dispositivo.nombre)-1);
    dispositivo.altura = 160;
    dispositivo.litros = 1100;
    dispositivo.potenciaLoRaDbm = LORA_POTENCIA_DEFECTO_DBM;

    nombreDispositivo = dispositivo.nombre;
    alturaDispositivo = dispositivo.altura;
    litrosDispositivo = dispositivo.litros;
}

void inicializarDispositivo() {
    establecerValoresDeFabrica();
}

void guardarDatosEnEEPROM() {
    Serial.println("💾 Iniciando guardado en EEPROM...");
    
    EEPROM.put(EEPROM_ADDR_DATOS, dispositivo);
    Serial.println("📝 Datos escritos en buffer EEPROM");
    
    bool success = EEPROM.commit();
    if (success) {
        Serial.println("✅ Datos guardados correctamente en EEPROM");
        
        // Verificación: leer de vuelta para confirmar
        DispositivoData datosVerificados;
        EEPROM.get(EEPROM_ADDR_DATOS, datosVerificados);
        
        Serial.println("🔍 Verificación EEPROM:");
        Serial.printf("   MAC: '%s'\n", datosVerificados.mac);
        Serial.printf("   Nombre: '%s'\n", datosVerificados.nombre);
        Serial.printf("   Altura: %lu\n", datosVerificados.altura);
        Serial.printf("   Litros: %lu\n", datosVerificados.litros);
        Serial.printf("   Potencia LoRa: %u dBm\n", datosVerificados.potenciaLoRaDbm);
        
    } else {
        Serial.println("❌ Error al guardar en EEPROM - Commit falló");
    }
}

void leerDatosDeEEPROM() {
    EEPROM.get(EEPROM_ADDR_DATOS, dispositivo);
    size_t macLen = strlen(dispositivo.mac);

    // Detectar contenido vacío/no inicializado (todo en cero y strings vacíos)
    bool sinDatosUsuario = (macLen == 0) && (strlen(dispositivo.nombre) == 0) &&
                           (dispositivo.altura == 0 || dispositivo.litros == 0);

    if (macLen != 0 && macLen != 17) {
        Serial.println("Datos corruptos en EEPROM. Reinicializando a fábrica...");
        establecerValoresDeFabrica();
        guardarDatosEnEEPROM();
    } else if (sinDatosUsuario) {
        Serial.println("EEPROM sin datos previos. Cargando valores de fábrica...");
        establecerValoresDeFabrica();
        guardarDatosEnEEPROM();
    }

    // Sincronizar variables globales con los datos almacenados
    nombreDispositivo = dispositivo.nombre;
    alturaDispositivo = dispositivo.altura;
    litrosDispositivo = dispositivo.litros;

    if (dispositivo.potenciaLoRaDbm < LORA_POTENCIA_MIN_DBM || dispositivo.potenciaLoRaDbm > LORA_POTENCIA_MAX_DBM) {
        dispositivo.potenciaLoRaDbm = LORA_POTENCIA_DEFECTO_DBM;
    }
    potenciaLoRaActualDbm = dispositivo.potenciaLoRaDbm;
}

void imprimirDatosDispositivo() {
    Serial.println("\n--- Datos del Dispositivo ---");
    Serial.print("MAC: "); Serial.println(dispositivo.mac);
    Serial.print("Nombre: "); Serial.println(dispositivo.nombre);
    Serial.print("Altura: "); Serial.println(dispositivo.altura);
    Serial.print("Litros: "); Serial.println(dispositivo.litros);
    Serial.println("----------------------------\n");
}

void limpiarEEPROMYReiniciar() {
    Serial.println("\n🗑️  INICIANDO PROCESO DE BAJA...");

    // ⭐ PARPADEO RÁPIDO DURANTE EL PROCESO (LED ROJO)
    Serial.printf("💡 Parpadeo rápido (%d ms) durante %d segundos con LED rojo...\n",
                  PARPADEO_LED_RAPIDO_MS, DURACION_PARPADEO_PROCESO_BAJA_MS / 1000);
    unsigned long inicioParpadeo = millis();
    unsigned long ultimoCambio = millis();
    bool estadoRojo = false;

    while (millis() - inicioParpadeo < DURACION_PARPADEO_PROCESO_BAJA_MS) {
        if (millis() - ultimoCambio >= PARPADEO_LED_RAPIDO_MS) {
            estadoRojo = !estadoRojo;
            digitalWrite(LED_ROJO_PIN, estadoRojo);
            ultimoCambio = millis();
            Serial.print("💫 ");
        }
        delay(10);
    }

    digitalWrite(LED_ROJO_PIN, LOW);
    digitalWrite(LED_VERDE_PIN, LOW);
    Serial.println("\n");
    
    Serial.println("🧹 Limpiando EEPROM...");

    // Limpiar flag de registro
    EEPROM.write(EEPROM_ADDR_REGISTRADO, 0);

    // Restablecer datos del dispositivo a valores de fábrica (MAC nula)
    establecerValoresDeFabrica();
    EEPROM.put(EEPROM_ADDR_DATOS, dispositivo);

    bool success = EEPROM.commit();
    Serial.printf("💿 EEPROM limpiada: %s\n", success ? "ÉXITO" : "FALLO");

    registrado = false;
    macRegistrada = "";

    // ⭐ VERIFICACIÓN PROFUNDA DE BORRADO
    registrado = EEPROM.read(EEPROM_ADDR_REGISTRADO) == 1;
    DispositivoData verificacion;
    EEPROM.get(EEPROM_ADDR_DATOS, verificacion);

    bool valoresFabrica = (strncmp(verificacion.nombre, dispositivo.nombre, sizeof(dispositivo.nombre)) == 0) &&
                          verificacion.altura == dispositivo.altura &&
                          verificacion.litros == dispositivo.litros &&
                          strlen(verificacion.mac) == 0;

    Serial.printf("🔍 Verificación - Registrado: %s | Datos fábrica: %s\n",
                  registrado ? "SI" : "NO",
                  valoresFabrica ? "OK" : "FALTA LIMPIEZA");

    Serial.println("🔄 Reiniciando en 3 segundos tras parpadeo extendido...");

    Serial.printf("✨ Parpadeo final de %d segundo(s) (LED rojo)...\n", DURACION_PARPADEO_FINAL_BAJA_MS / 1000);
    parpadearLED(LED_ROJO_PIN, PARPADEO_LED_RAPIDO_MS, DURACION_PARPADEO_FINAL_BAJA_MS);

    delay(3000);
    Serial.println("🚀 REINICIANDO PARA MODO ALTA...");
    ESP.restart();
}

unsigned long calcularVentanaConfirmacionMs(int intento) {
    if (intento < 1) intento = 1;
    return TIEMPO_ESPERA_CONFIRMACION_INICIAL + (intento - 1) * INCREMENTO_ESPERA_CONFIRMACION;
}

bool esperarConfirmacionConfiguracion(uint8_t potenciaEsperada, int intentoActual) {
    unsigned long inicioEspera = millis();
    unsigned long ventana = calcularVentanaConfirmacionMs(intentoActual);

    while (millis() - inicioEspera < ventana) {
        int packetSize = LoRa.parsePacket();
        if (packetSize) {
            String respuesta = "";
            while (LoRa.available()) {
                respuesta += (char)LoRa.read();
            }
            respuesta.trim();

            Serial.printf("📨 Confirmación de configuración: %s\n", respuesta.c_str());

            if (!respuesta.startsWith("configuracion/")) {
                Serial.println("⏭️  No es confirmación de configuración, se ignora en este ciclo");
                continue;
            }

            int primera = respuesta.indexOf('/');
            int segunda = respuesta.indexOf('/', primera + 1);
            int tercera = respuesta.indexOf('/', segunda + 1);
            int coma = respuesta.indexOf(',', tercera + 1);

            if (primera == -1 || segunda == -1 || tercera == -1 || coma == -1) {
                Serial.println("⚠️  Formato de confirmación de configuración inválido");
                continue;
            }

            String mac = respuesta.substring(primera + 1, segunda);
            String etapa = respuesta.substring(segunda + 1, tercera);
            uint8_t potencia = respuesta.substring(coma + 1).toInt();

            if (mac != macAddress || etapa != "confirmacion") {
                Serial.println("⏭️  Confirmación de otra MAC o etapa distinta");
                continue;
            }

            if (potencia != potenciaEsperada) {
                Serial.printf("⚠️  Potencia confirmada %u dBm no coincide con esperada %u dBm\n", potencia, potenciaEsperada);
            }

            return true;
        }
    }

    return false;
}

bool esperarConfirmacionLoRa(int intentoActual) {
    unsigned long inicioEspera = millis();
    unsigned long ventana = calcularVentanaConfirmacionMs(intentoActual);

    while (millis() - inicioEspera < ventana) {
        int packetSize = LoRa.parsePacket();
        if (packetSize) {
            String respuesta = "";
            while (LoRa.available()) {
                respuesta += (char)LoRa.read();
            }
            respuesta.trim();
            Serial.printf("📨 Confirmación recibida: %s\n", respuesta.c_str());

            int first = respuesta.indexOf(',');
            int second = respuesta.indexOf(',', first + 1);
            int third = respuesta.indexOf(',', second + 1);
            int fourth = respuesta.indexOf(',', third + 1);

            if (first == -1 || second == -1 || third == -1 || fourth == -1) {
                Serial.println("⚠️  Confirmación inválida: se esperaba 'CONFIRMACION,MAC,nombre,altura,litros'");
                continue;
            }

            String tipo = respuesta.substring(0, first);
            String mac = respuesta.substring(first + 1, second);
            String nombreNuevo = respuesta.substring(second + 1, third);
            uint32_t alturaNueva = respuesta.substring(third + 1, fourth).toInt();
            uint32_t litrosNuevos = respuesta.substring(fourth + 1).toInt();

            if (tipo != "CONFIRMACION" || mac != macAddress) {
                Serial.printf("⏭️  Confirmación no esperada: se esperaba CONFIRMACION,%s,... y llegó %s,%s,...\n",
                              macAddress.c_str(), tipo.c_str(), mac.c_str());
                continue;
            }

            bool cambios = nombreNuevo != String(dispositivo.nombre) ||
                           alturaNueva != dispositivo.altura ||
                           litrosNuevos != dispositivo.litros;

            if (cambios) {
                Serial.println("✏️  Actualizando datos desde confirmación del monitor...");
                strlcpy(dispositivo.nombre, nombreNuevo.c_str(), sizeof(dispositivo.nombre));
                dispositivo.altura = alturaNueva;
                dispositivo.litros = litrosNuevos;
                guardarDatosEnEEPROM();
            }

            return true;
        }
    }

    return false;
}

bool enviarDatos(int distancia) {
    int litrosActuales = 0;
    
    if (distancia != 9999) {
        litrosActuales = calcularLitros(distancia, dispositivo.altura, dispositivo.litros);
        
        // ⭐ AGREGAR AL HISTORIAL PARA IA
        agregarAlHistorial(litrosActuales);
        
        // ⭐ EJECUTAR ANÁLISIS CADA 5 MINUTOS
        if (millis() - ultimoAnalisis >= INTERVALO_ANALISIS) {
            analizarPatronesConsumo(litrosActuales);
            ultimoAnalisis = millis();
        }
        
        // Mostrar interpretación
        Serial.printf("📊 Interpretación: %d cm → ", distancia);
        if (distancia >= dispositivo.altura) {
            Serial.printf("DEPÓSITO VACÍO (%d L)\n", litrosActuales);
        } else {
            int aguaCm = dispositivo.altura - distancia;
            Serial.printf("%d cm de agua → %d L\n", aguaCm, litrosActuales);
        }
    } else {
        litrosActuales = 9999;
        Serial.println("⚠️  Enviando valor de error (9999)");
    }
    
    // Leer sensores adicionales
    float voltage = (analogRead(ADC_PIN) / 4095.0 * 3.3 * 2.0);
    float temperatura = temperatureRead();
    
    // Redondear valores para envío
    int voltageInt = round(voltage);
    int temperaturaInt = round(temperatura);
    
    // ⭐ OBTENER DATOS DE IA
    String datosIA = obtenerDatosIA();
    
    // Formar mensaje LoRa MEJORADO con IA
    String mensaje = "001," + macAddress + "," +
                    String(litrosActuales) + "," + 
                    String(voltageInt) + "," + 
                    String(temperaturaInt) + "," +
                    String(dispositivo.altura) + "," + 
                    String(dispositivo.litros) + "," + 
                    String(dispositivo.nombre) + "," +
                    datosIA;
    
    // Mostrar desglose detallado CON IA
    Serial.println("\n🔍 DESGLOSE DE DATOS + IA:");
    Serial.printf("   ID: 002\n");
    Serial.printf("   MAC: %s\n", macAddress.c_str());
    Serial.printf("   Litros: %d L\n", litrosActuales);
    Serial.printf("   Voltaje: %.1f V → %d\n", voltage, voltageInt);
    Serial.printf("   Temperatura: %.1f °C → %d\n", temperatura, temperaturaInt);
    Serial.printf("   Altura total: %lu cm\n", dispositivo.altura);
    Serial.printf("   Capacidad total: %lu L\n", dispositivo.litros);
    Serial.printf("   Nombre: %s\n", dispositivo.nombre);
    Serial.printf("   🧠 IA - Notificación: %d\n", analisisActual.notificacion);
    Serial.printf("   🧠 IA - Mensaje: %s\n", analisisActual.mensajeNotificacion.c_str());
    Serial.printf("   🧠 IA - Promedio diario: %.1f L\n", analisisActual.promedioDiario);
    Serial.printf("   🧠 IA - Promedio semanal: %.1f L\n", analisisActual.promedioSemanal);
    Serial.printf("   🧠 IA - Promedio mensual: %.1f L\n", analisisActual.promedioMensual);
    
    bool cambios = mensaje != ultimoMensajeConfirmado;
    intervaloEnvioActual = cambios ? INTERVALO_ENVIO_CAMBIO : INTERVALO_ENVIO_FORZOSO;

    bool confirmado = false;
    uint8_t potenciaConfirmada = potenciaLoRaActualDbm;
    bool ajustarPotenciaTrasDespertar = wakeByImpact || recalibrarPotenciaLoRa;
    uint8_t potenciaInicio = recalibrarPotenciaLoRa ? LORA_POTENCIA_MIN_DBM : potenciaLoRaActualDbm;
    uint8_t potenciaFin = recalibrarPotenciaLoRa ? LORA_POTENCIA_MAX_DBM : potenciaLoRaActualDbm;

    for (uint8_t potencia = potenciaInicio; potencia <= potenciaFin; potencia++) {
        potenciaLoRaActualDbm = potencia;
        LoRa.setTxPower(potenciaLoRaActualDbm, PA_OUTPUT_PA_BOOST_PIN);
        Serial.printf("\n🔊 Potencia LoRa ajustada a %u dBm (barrido %s)\n",
                      potenciaLoRaActualDbm,
                      recalibrarPotenciaLoRa ? "dinámico" : "fijo");

        for (int intento = 1; intento <= REINTENTOS_CONFIRMACION; intento++) {
            unsigned long ventana = calcularVentanaConfirmacionMs(intento);
            Serial.printf("📤 INICIANDO TRANSMISIÓN LoRa (nivel %u dBm, intento %d/%d, espera %lu ms)...\n",
                          potenciaLoRaActualDbm, intento, REINTENTOS_CONFIRMACION, ventana);
            int beginResult = LoRa.beginPacket();

            if (beginResult) {
                LoRa.print(mensaje);
                int endResult = LoRa.endPacket();

                if (endResult) {
                    Serial.println("\n🎉 TRANSMISIÓN COMPLETADA");
                    Serial.print("📨 MENSAJE: ");
                    Serial.println(mensaje);
                    parpadearLED(LED_VERDE_PIN, PARPADEO_LORA_INTERVALO_MS, DURACION_PARPADEO_LORA_MS);
                    if (esperarConfirmacionLoRa(intento)) {
                        confirmado = true;
                        potenciaConfirmada = potenciaLoRaActualDbm;
                        break;
                    } else {
                        Serial.println("⌛ Sin confirmación, reintentando...");
                    }
                }
            }
            delay(250 + intento * 150);
        }

        if (confirmado || !recalibrarPotenciaLoRa) {
            break;
        }

        if (potencia == potenciaFin) {
            break;
        }
    }

    ultimoEnvioDatos = millis();
    if (confirmado) {
        ultimoMensajeConfirmado = mensaje;
        dispositivo.potenciaLoRaDbm = potenciaConfirmada;
        guardarDatosEnEEPROM();
        recalibrarPotenciaLoRa = false;
        if (ajustarPotenciaTrasDespertar) {
            intercambiarPotenciaConMonitor(potenciaConfirmada);
        }
    } else if (recalibrarPotenciaLoRa) {
        dispositivo.potenciaLoRaDbm = LORA_POTENCIA_DEFECTO_DBM;
        potenciaLoRaActualDbm = dispositivo.potenciaLoRaDbm;
        LoRa.setTxPower(potenciaLoRaActualDbm, PA_OUTPUT_PA_BOOST_PIN);
        guardarDatosEnEEPROM();
        Serial.printf("⚠️  Barrido completo sin confirmación. Potencia devuelta a %u dBm\n", potenciaLoRaActualDbm);
    }

    return confirmado;
}

bool intercambiarPotenciaConMonitor(uint8_t potenciaConfirmada) {
    bool confirmado = false;

    for (int intento = 1; intento <= REINTENTOS_CONFIG_POTENCIA; intento++) {
        String solicitud = "configuracion/" + macAddress + "/solicitud," + String(potenciaConfirmada);

        LoRa.setTxPower(potenciaLoRaActualDbm, PA_OUTPUT_PA_BOOST_PIN);
        unsigned long ventana = calcularVentanaConfirmacionMs(intento);
        Serial.printf("\n📡 Enviando ajuste de potencia (%s) usando %u dBm (intento %d/%d, espera %lu ms)\n",
                      solicitud.c_str(), potenciaLoRaActualDbm, intento, REINTENTOS_CONFIG_POTENCIA, ventana);

        if (LoRa.beginPacket()) {
            LoRa.print(solicitud);
            LoRa.endPacket();

            if (esperarConfirmacionConfiguracion(potenciaConfirmada, intento)) {
                Serial.println("✅ Confirmación de potencia recibida desde monitor01");
                confirmado = true;
                break;
            }
        }
        delay(200);
    }

    if (!confirmado) {
        Serial.println("⚠️  No se obtuvo confirmación de configuración tras los reintentos (continuando sin error crítico)");
    }

    return confirmado;
}

int calcularLitros(int distancia, uint32_t alturaTotal, uint32_t litrosTotal) {
    if (alturaTotal == 0 || litrosTotal == 0) return 0;
    
    // ⭐⭐ CORRECCIÓN: Si la distancia es mayor que la altura, el depósito está VACÍO
    if (distancia >= alturaTotal) {
        return 0;
    }
    
    // Si la distancia es menor que la altura, calcular litros
    int alturaLiquido = alturaTotal - distancia;
    if (alturaLiquido <= 0) return 0;
    
    // Calcular litros proporcionalmente
    int litros = (alturaLiquido * litrosTotal) / alturaTotal;
    
    Serial.printf("   📊 Cálculo: %d cm (dist) → %d cm (agua) → %d L\n", 
                distancia, alturaLiquido, litros);
    
    return litros;
}

int obtenerDistanciaValida() {
    int intentos = 0;
    float distancia = -1;
    
    Serial.println("🔍 Iniciando medición ultrasónica...");
    Serial.printf("   Rango aceptable: 2 - %d cm (Altura total: %d cm)\n", 
                dispositivo.altura * 2, dispositivo.altura);
    
    while (intentos < 5) { // Reducir intentos para mayor eficiencia
        esp_task_wdt_reset();
        
        distancia = measureDistance();
        
        // ⭐⭐ CORRECCIÓN: VALORES VÁLIDOS SON DESDE 2cm HASTA 2x LA ALTURA
        // - 2-160cm: Depósito con agua
        // - 160-320cm: Depósito vacío o casi vacío  
        if (distancia >= 2.0 && distancia <= (dispositivo.altura * 2)) {
            Serial.printf("✅ Medición válida: %.1f cm (intento %d)\n", distancia, intentos + 1);
            
            // ⭐ INTERPRETAR EL RESULTADO
            if (distancia <= dispositivo.altura) {
                Serial.printf("   💧 Depósito con agua: %.1f cm de líquido\n", dispositivo.altura - distancia);
            } else {
                Serial.printf("   🚱 Depósito vacío: medición por encima del tope\n", distancia);
            }
            
            return round(distancia);
        }
        
        if (distancia < 0) {
            Serial.printf("   Intento %d: Sin señal o timeout\n", intentos + 1);
        } else {
            Serial.printf("   Intento %d: Valor extremo (%.1f cm)\n", intentos + 1, distancia);
        }
        
        intentos++;
        delay(100);
    }
    
    // ⭐ FALLBACK: Si después de 5 intentos no hay medición válida
    Serial.println("⚠️  Usando valor por defecto (50cm)");
    return 50;
}

void resetearSensorUltrasonico() {
    Serial.println("Reseteando sensor ultrasónico...");
    pinMode(trigPin, INPUT);
    pinMode(echoPin, INPUT);
    delay(10);
    pinMode(trigPin, OUTPUT);
    pinMode(echoPin, INPUT);
    digitalWrite(trigPin, LOW);
}

void prepararParaDeepSleep() {
    // ⭐⭐ VERIFICAR SI ESTAMOS EN PERIODO DE ESPERA DESPUÉS DE BAJA
    if (esperaDespuesBaja) {
        Serial.println("⚠️  No se puede dormir - En periodo de espera después de baja");
        return;
    }

    Serial.println("🛌 Preparando para deep sleep...");

    unsigned long sleepTime = intervaloEnvioActual;

    if (registrado) {
        // Calcular tiempo de sleep exacto
        unsigned long tiempoDesdeUltimoEnvio = millis() - ultimoEnvioDatos;
        sleepTime = intervaloEnvioActual - tiempoDesdeUltimoEnvio;

        // Asegurar que el tiempo de sleep sea válido
        if (sleepTime < 1000) sleepTime = 1000;
        if (sleepTime > intervaloEnvioActual) sleepTime = intervaloEnvioActual;

        esp_sleep_enable_timer_wakeup(sleepTime * 1000);
        Serial.printf("   Sleep por TIMER: %lu ms (%lu seg)\n", sleepTime, sleepTime / 1000);
    } else {
        // Sin alta: solo despertar por impacto
        Serial.println("   Sleep sin alta - esperando impacto para despertar");
    }

    // ⭐ Wakeup adicional por sensor de impacto (nivel bajo)
    esp_sleep_enable_ext0_wakeup((gpio_num_t)SENSOR_IMPACTO_PIN, 0);

    // Apagar periféricos para ahorro de energía
    LoRa.sleep();
    WiFi.mode(WIFI_OFF);
    btStop();

    Serial.printf("✅ Configurado sleep (timer=%s, impacto=GPIO%d)\n",
                  registrado ? "SI" : "NO", SENSOR_IMPACTO_PIN);
}

void iniciarLoRaConReintentos() {
    Serial.println("📡 INICIANDO CONFIGURACIÓN LoRa...");
    
    SPI.begin(18, 19, 23, 5);
    LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
    
    int intentos = 0;
    while (!LoRa.begin(433E6) && intentos < 10) {
        Serial.printf("❌ Error al iniciar LoRa (Intento %d/10). Reintentando...\n", intentos + 1);
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
        intentos++;
        delay(1000);
    }
    
    if (intentos < 10) {
        Serial.println("✅ LoRa inicializado correctamente!");
        
        // Configurar parámetros LoRa
        LoRa.setTxPower(potenciaLoRaActualDbm, PA_OUTPUT_PA_BOOST_PIN);
        LoRa.setSpreadingFactor(12);
        LoRa.setSignalBandwidth(125E3);
        LoRa.setCodingRate4(8);
        
        // Configurar callback
        LoRa.onTxDone(onTxDone);
        
        // Mostrar configuración (valores estáticos ya que no hay funciones get)
        Serial.println("📊 CONFIGURACIÓN LoRa APLICADA:");
        Serial.println("   Frecuencia: 433.0 MHz");
        Serial.printf("   Potencia TX: %u dBm (ajustable)\n", potenciaLoRaActualDbm);
        Serial.println("   Spreading Factor: 12");
        Serial.println("   Ancho de banda: 125 kHz");
        Serial.println("   Coding Rate: 4/8");
        Serial.println("   Sync Word: 0x12");
        Serial.println("   Preamble Length: 8");
        
        digitalWrite(LED_PIN, LOW);
    } else {
        Serial.println("🚨 ERROR CRÍTICO: No se pudo inicializar LoRa después de 10 intentos");
        digitalWrite(LED_PIN, HIGH); // LED encendido indicando error
    }
}

void entrarDeepSleep() {
    prepararParaDeepSleep();
    esp_deep_sleep_start();
}

/**
 * 18650 (Iones de Litio): Ofrecen mayor capacidad. Una celda 18650 de 3.7V y 3400mAh  proporciona una autonomía excelente para proyectos que deben funcionar meses sin recarga.
Con batería 18650 3400mAh:

Autonomía = 3400mAh × (24h / 303mAh) ≈ 112 días (¡Casi 4 meses!)
 * 
Regulador LDO eficiente (como MCP1700 o HT7333)

Sin LEDs indicadores

Conexión directa 3.3V al ESP32


 */