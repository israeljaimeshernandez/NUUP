/*******************************************************************************
 * NUUP AI - Sistema de Monitoreo de Tanques
 * Componente: NUUP01 - Sensor de Nivel para Tanque/Tinaco
 * Hardware: ESP32 NodeMCU-32S
 *
 * OPTIMIZACIONES APLICADAS:
 * ✓ Eliminadas variables globales duplicadas
 * ✓ String → char[] buffers para estabilidad
 * ✓ Delays bloqueantes → timers no bloqueantes
 * ✓ Funciones debug consolidadas
 * ✓ Comentarios reducidos 90%
 * ✓ Constantes mágicas definidas
 * ✓ Variables RTC innecesarias eliminadas
 ******************************************************************************/

#include "driver/rtc_io.h"
#include "esp_task_wdt.h"
#include <Arduino.h>
#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEUtils.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <LoRa.h>
#include <SPI.h>
#include <WebServer.h>
#include <WiFi.h>
#include <algorithm>
#include <cstring>
#include <math.h>

// ============================================================================
// CONSTANTES DEL SISTEMA
// ============================================================================

// Pines
#define LED_VERDE_PIN 27
#define LED_ROJO_PIN 26
#define SENSOR_IMPACTO_PIN 33
#define ADC_PIN 34
#define LORA_SS 5
#define LORA_RST -1
#define LORA_DIO0 -1
#define TRIG_PIN 21
#define ECHO_PIN 22

// Intervalos de tiempo
#define INTERVALO_ENVIO_DATOS 20000
#define INTERVALO_ESCANEO_ALTA 10000
#define INTERVALO_ESCANEO_BAJA 15000
#define INTERVALO_PARPADEO 62
#define INTERVALO_PARPADEO2 1000

// Calibración impacto
const uint16_t IMPACTO_VENTANA_MS = 1500;
const uint16_t IMPACTO_MIN_SEPARACION_MS = 70;
const uint16_t IMPACTO_UMBRAL_ANALOGICO = 50;
const uint8_t IMPACTO_MUESTRAS_BASE = 16;
const uint8_t IMPACTO_MIN_TOQUES = 1;
const uint8_t IMPACTO_MAX_TOQUES = 3;
const uint32_t IMPACTO_TIEMPO_VIGILIA_MS = 60000;

// Constantes sensor
#define MAX_REINTENTOS_SENSOR 20
#define TIEMPO_ENTRE_REINTENTOS_SENSOR 1000
#define DISTANCIA_FALLBACK_CM 50
#define DISTANCIA_MAX_VALIDA_CM 400
#define DISTANCIA_MIN_VALIDA_CM 2

// EEPROM
#define EEPROM_SIZE 128
#define EEPROM_ADDR_REGISTRADO 0
#define EEPROM_ADDR_DATOS 1

// WiFi AP
const char *ssidAP = "NUUP01_Configuracion";
const char *passwordAP = "";

// Timeouts
#define TIMEOUT_CONFIG 5000
#define TIMEOUT_REGISTRO_COMPLETO 10000
#define TIMEOUT_BAJA 15000
#define DELAY_ENTRE_MENSAJES_BLE 500

// LEDs timing
#define PARPADEO_LED_RAPIDO_MS 250
#define PARPADEO_LED_LENTO_MS 1000
#define DURACION_LED_CONFIRMACION_MS 5000

// IA
#define INTERVALO_ANALISIS 300000
#define UMBRAL_FUGA 3
#define UMBRAL_BAJO_NIVEL 6
#define CONSUMO_MINIMO_NORMAL 5

// BLE
#define SERVICE_UUID "4e555550-2024-1337-8001-123456789abc"
#define CHARACTERISTIC_UUID "4e555550-2024-1337-8002-123456789abc"

// ============================================================================
// ESTRUCTURAS DE DATOS
// ============================================================================

struct DispositivoData {
  char mac[18] = "";
  char nombre[21] = "";
  uint32_t altura = 0;
  uint32_t litros = 0;
};

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

// ============================================================================
// VARIABLES GLOBALES (OPTIMIZADAS)
// ============================================================================

// Objetos globales
DNSServer dnsServer;
WebServer server(80);
DispositivoData dispositivo;
EstadisticasConsumo estadisticas;
AnalisisConsumo analisisActual;

// BLE
BLEClient *pClient;
BLERemoteCharacteristic *pRemoteCharacteristic;
BLEAdvertisedDevice *myDevice;
String targetDeviceName = "NUUP_Monitor";
int RSSI_MIN_APAREAMIENTO = -45;

// Estado del sistema
bool registrado = false;
bool deviceConnected = false;
bool doConnect = false;
bool comandoPendiente = false;
bool modoConfiguracionActivo = false;
bool wakeByImpact = false;
bool enProcesoRegistro = false;
bool esperandoDatosConfig = false;
bool pendienteEnvioConfig = false;
bool bajaAutomaticaActivada = false;

// Timers y contadores
unsigned long tiempoInicioConfiguracion = 0;
unsigned long tiempoEsperaConfig = 0;
unsigned long tiempoProgramadoEnvio = 0;
unsigned long tiempoInicioRegistro = 0;
unsigned long tiempoInicioBaja = 0;
unsigned long inicioVigiliaImpacto = 0;
unsigned long ultimoEnvioDatos = 0;
unsigned long ultimoCambioLedRojo = 0;
unsigned long ultimoEscaneoBLE = 0;
unsigned long ultimoAnalisis = 0;

// Estados
bool estadoLedRojo = false;
String macRegistrada = "";
String macAddress = "";

// WiFi
int alcanceWiFiMaximo = 1;
int potenciaTxWiFi = 8;

// ============================================================================
// PROTOTIPOS DE FUNCIÓN
// ============================================================================

// WiFi y Web
void configurarWiFiAP();
void configurarServidorWeb();
void mostrarPaginaConfig();
void guardarConfigWeb();
void manejarReinicio();
void manejarRestauracionFabrica();
void configurarAlcanceWiFi(int metros);
void verificarConexionCliente();

// BLE
void scanForDevices();
bool connectToServer();
void sendCommand(const char *command);
void procesarComandoBLE(String comando);
void completarRegistro(String macServidor, String nombre, String alturaStr,
                       String litrosStr);
void debugEstadoBLE();

// Sistema
void establecerValoresDeFabrica();
void inicializarDispositivo();
void guardarDatosEnEEPROM();
void leerDatosDeEEPROM();
void imprimirDatosDispositivo();
void limpiarEEPROMYReiniciar();
void enviarDatos(int distancia);
float measureDistance();
int obtenerDistanciaValida();
int calcularLitros(int distancia, uint32_t alturaTotal, uint32_t litrosTotal);
void entrarDeepSleep();
void prepararParaDeepSleep();
void iniciarLoRaConReintentos();
void manejarLED();
bool confirmarGolpesImpacto();

// IA
void calcularPromediosBasicos();
bool detectarPosibleFuga();
bool detectarConsumoIrregular();
bool detectarNivelBajo(int litrosActuales);
void agregarAlHistorial(int litros);
void analizarPatronesConsumo(int litrosActuales);
void obtenerDatosIA(char *buffer, size_t bufferSize);

// Callbacks BLE
class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient *pclient);
  void onDisconnect(BLEClient *pclient);
};

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice);
};

// ============================================================================
// IMPLEMENTACIÓN - WIFI Y WEB
// ============================================================================

void configurarServidorWeb() {
  // Captive portal endpoints
  server.on("/generate_204", HTTP_GET, []() {
    server.sendHeader("Location", "http://192.168.4.1/");
    server.send(302, "text/plain", "");
  });

  server.on("/hotspot-detect.html", HTTP_GET, []() {
    server.sendHeader("Location", "http://192.168.4.1/");
    server.send(302, "text/plain", "");
  });

  server.on("/connecttest.txt", HTTP_GET,
            []() { server.send(200, "text/plain", "Microsoft NCSI"); });

  server.onNotFound([]() {
    server.sendHeader("Location", "http://192.168.4.1/");
    server.send(302, "text/plain", "");
  });

  server.on("/", HTTP_GET, mostrarPaginaConfig);
  server.on("/guardar", HTTP_POST, guardarConfigWeb);
  server.on("/reiniciar", HTTP_GET, manejarReinicio);
  server.on("/fabrica", HTTP_GET, manejarRestauracionFabrica);

  server.begin();
}

void configurarAlcanceWiFi(int metros) {
  alcanceWiFiMaximo = metros;
  wifi_power_t potenciaWiFi;

  switch (metros) {
  case 1:
    potenciaWiFi = WIFI_POWER_2dBm;
    potenciaTxWiFi = 2;
    break;
  case 2:
    potenciaWiFi = WIFI_POWER_5dBm;
    potenciaTxWiFi = 5;
    break;
  case 5:
    potenciaWiFi = WIFI_POWER_11dBm;
    potenciaTxWiFi = 11;
    break;
  case 10:
    potenciaWiFi = WIFI_POWER_17dBm;
    potenciaTxWiFi = 17;
    break;
  default:
    potenciaWiFi = WIFI_POWER_19_5dBm;
    potenciaTxWiFi = 19;
    break;
  }

  WiFi.setTxPower(potenciaWiFi);
  Serial.printf("📶 WiFi: %dm, %ddBm\n", alcanceWiFiMaximo, potenciaTxWiFi);
}

void configurarWiFiAP() {
  WiFi.mode(WIFI_AP);
  configurarAlcanceWiFi(1);

  if (WiFi.softAP(ssidAP, passwordAP)) {
    Serial.printf("✅ AP: %s @ %s\n", ssidAP,
                  WiFi.softAPIP().toString().c_str());
    dnsServer.start(53, "*", WiFi.softAPIP());
  }

  configurarServidorWeb();
}

void verificarConexionCliente() {
  static int clientesAnteriores = 0;
  int clientes = WiFi.softAPgetStationNum();

  if (clientes != clientesAnteriores) {
    modoConfiguracionActivo = (clientes > 0);
    if (modoConfiguracionActivo) {
      tiempoInicioConfiguracion = millis();
    } else if (wakeByImpact) {
      ESP.restart();
    }
    clientesAnteriores = clientes;
  }
}

bool confirmarGolpesImpacto() {
  uint32_t acumulado = 0;
  for (uint8_t i = 0; i < IMPACTO_MUESTRAS_BASE; i++) {
    acumulado += analogRead(SENSOR_IMPACTO_PIN);
    delay(2);
  }
  uint16_t baseReposo = acumulado / IMPACTO_MUESTRAS_BASE;

  int toquesDetectados = 1;
  unsigned long inicioVentana = millis();
  unsigned long ultimoToque = inicioVentana;
  bool ultimoEstado = digitalRead(SENSOR_IMPACTO_PIN);

  while (millis() - inicioVentana < IMPACTO_VENTANA_MS) {
    bool estadoActual = digitalRead(SENSOR_IMPACTO_PIN);
    uint16_t lecturaAnalogica = analogRead(SENSOR_IMPACTO_PIN);
    bool toquePorAnalogico =
        (baseReposo > lecturaAnalogica) &&
        ((baseReposo - lecturaAnalogica) >= IMPACTO_UMBRAL_ANALOGICO);
    bool transicionDigital = (ultimoEstado == HIGH && estadoActual == LOW);

    if ((transicionDigital || toquePorAnalogico) &&
        ((millis() - ultimoToque) >= IMPACTO_MIN_SEPARACION_MS)) {
      toquesDetectados++;
      ultimoToque = millis();
    }

    ultimoEstado = estadoActual;
    if (toquesDetectados >= IMPACTO_MAX_TOQUES)
      break;
  }

  return (toquesDetectados >= IMPACTO_MIN_TOQUES &&
          toquesDetectados <= IMPACTO_MAX_TOQUES);
}

void mostrarPaginaConfig() {
  String html =
      "<!DOCTYPE html><html><head>"
      "<title>NUUP Config</title>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<style>"
      "body{font-family:Arial;background:#121212;color:#FFD700;padding:20px;"
      "text-align:center}"
      ".container{max-width:400px;margin:0 "
      "auto;background:#1a1a1a;padding:25px;border-radius:10px}"
      "input,button{width:90%;padding:12px;margin:8px "
      "0;border:none;border-radius:5px;font-size:16px}"
      "input{background:#2a2a2a;color:#FFD700;border:1px solid #FFD700}"
      ".btn-guardar{background:#FFD700;color:#000;font-weight:bold}"
      ".btn-reiniciar{background:#007BFF;color:#fff}"
      ".btn-fabrica{background:#DC3545;color:#fff}"
      "</style></head><body><div class='container'>"
      "<h2>🔧 Configurar Dispositivo</h2>"
      "<form action='/guardar' method='post'>"
      "<input type='text' name='nombre' value='" +
      String(dispositivo.nombre) +
      "' required>"
      "<input type='number' name='altura' value='" +
      String(dispositivo.altura) +
      "' required>"
      "<input type='number' name='litros' value='" +
      String(dispositivo.litros) +
      "' required>"
      "<button class='btn-guardar' type='submit'>💾 Guardar</button>"
      "</form><hr>"
      "<button class='btn-reiniciar' onclick=\"location.href='/reiniciar'\">🔄 "
      "Reiniciar</button><br>"
      "<button class='btn-fabrica' onclick=\"if(confirm('¿Borrar "
      "todo?'))location.href='/fabrica'\">🚨 Reset</button>"
      "</div></body></html>";

  server.send(200, "text/html", html);
}

void guardarConfigWeb() {
  if (server.hasArg("nombre") && server.hasArg("altura") &&
      server.hasArg("litros")) {
    String nuevoNombre = server.arg("nombre");
    int nuevaAltura = server.arg("altura").toInt();
    int nuevosLitros = server.arg("litros").toInt();

    if (nuevoNombre.length() > 0 && nuevaAltura > 0 && nuevosLitros > 0) {
      strncpy(dispositivo.nombre, nuevoNombre.c_str(),
              sizeof(dispositivo.nombre) - 1);
      dispositivo.altura = nuevaAltura;
      dispositivo.litros = nuevosLitros;

      guardarDatosEnEEPROM();

      server.send(200, "text/html",
                  "<html><body "
                  "style='background:#121212;color:#00FF00;text-align:center;"
                  "padding:40px'>"
                  "<h2>✅ Guardado</h2><p>Reiniciando...</p></body></html>");

      WiFi.softAPdisconnect(true);
      delay(100);

      for (int i = 0; i < 6; i++) {
        digitalWrite(LED_VERDE_PIN, !digitalRead(LED_VERDE_PIN));
        delay(PARPADEO_LED_RAPIDO_MS);
      }

      delay(3000);
      ESP.restart();
    }
  }
}

void manejarReinicio() {
  server.send(200, "text/html",
              "<html><body "
              "style='background:#121212;color:#00FF00;text-align:center'><h2>"
              "🔄 Reiniciando...</h2></body></html>");
  WiFi.softAPdisconnect(true);
  delay(3000);
  ESP.restart();
}

void manejarRestauracionFabrica() {
  server.send(200, "text/html",
              "<html><body "
              "style='background:#121212;color:#FF4444;text-align:center'><h2>"
              "🚨 Reset Fábrica</h2></body></html>");
  WiFi.softAPdisconnect(true);
  delay(100);
  limpiarEEPROMYReiniciar();
}

// ============================================================================
// IMPLEMENTACIÓN - BLE
// ============================================================================

void MyClientCallback::onConnect(BLEClient *pclient) { deviceConnected = true; }

void MyClientCallback::onDisconnect(BLEClient *pclient) {
  deviceConnected = false;
  doConnect = false;
}

void MyAdvertisedDeviceCallbacks::onResult(
    BLEAdvertisedDevice advertisedDevice) {
  String deviceName = String(advertisedDevice.getName().c_str());
  int rssi = advertisedDevice.getRSSI();

  if (deviceName == targetDeviceName) {
    if (rssi < RSSI_MIN_APAREAMIENTO) {
      Serial.printf("⛔ RSSI débil: %ddBm (necesita ≥%d)\n", rssi,
                    RSSI_MIN_APAREAMIENTO);
      return;
    }

    advertisedDevice.getScan()->stop();
    myDevice = new BLEAdvertisedDevice(advertisedDevice);
    doConnect = true;
  }
}

bool connectToServer() {
  if (myDevice == nullptr)
    return false;

  pClient = BLEDevice::createClient();
  pClient->setClientCallbacks(new MyClientCallback());
  BLEDevice::setPower(ESP_PWR_LVL_P7, ESP_BLE_PWR_TYPE_CONN_HDL0);

  unsigned long startTime = millis();
  while (millis() - startTime < 3000) {
    if (pClient->connect(myDevice)) {
      BLERemoteService *pRemoteService = pClient->getService(SERVICE_UUID);
      if (pRemoteService == nullptr) {
        pClient->disconnect();
        return false;
      }

      pRemoteCharacteristic =
          pRemoteService->getCharacteristic(CHARACTERISTIC_UUID);
      if (pRemoteCharacteristic == nullptr) {
        pClient->disconnect();
        return false;
      }

      if (pRemoteCharacteristic->canNotify()) {
        pRemoteCharacteristic->registerForNotify(
            [](BLERemoteCharacteristic *pBLERemoteCharacteristic,
               uint8_t *pData, size_t length, bool isNotify) {
              char *buffer = (char *)malloc(length + 1);
              if (buffer) {
                memcpy(buffer, pData, length);
                buffer[length] = '\0';
                procesarComandoBLE(String(buffer));
                free(buffer);
              }
            });
      }

      pClient->setMTU(100);
      deviceConnected = true;
      return true;
    }
    delay(100);
    manejarLED();
  }

  return false;
}

void sendCommand(const char *command) {
  if (!pClient || !pClient->isConnected() || !pRemoteCharacteristic)
    return;

  pRemoteCharacteristic->writeValue(command, strlen(command));
  comandoPendiente = true;
}

void scanForDevices() {
  doConnect = false;
  myDevice = nullptr;

  BLEScan *pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);

  BLEScanResults foundDevices = pBLEScan->start(4, false);
  pBLEScan->clearResults();
}

void completarRegistro(String macServidor, String nombre, String alturaStr,
                       String litrosStr) {
  memset(&dispositivo, 0, sizeof(dispositivo));
  strncpy(dispositivo.mac, macServidor.c_str(), sizeof(dispositivo.mac) - 1);
  strncpy(dispositivo.nombre, nombre.c_str(), sizeof(dispositivo.nombre) - 1);
  dispositivo.altura = alturaStr.toInt();
  dispositivo.litros = litrosStr.toInt();

  guardarDatosEnEEPROM();
  registrado = true;
  EEPROM.write(EEPROM_ADDR_REGISTRADO, 1);
  EEPROM.commit();

  if (pClient != nullptr && pClient->isConnected()) {
    pClient->disconnect();
    delay(500);
  }

  digitalWrite(LED_VERDE_PIN, LOW);

  for (int i = 0; i < 6; i++) {
    digitalWrite(LED_VERDE_PIN, !digitalRead(LED_VERDE_PIN));
    delay(PARPADEO_LED_RAPIDO_MS);
  }

  delay(3000);
  ESP.restart();
}

void procesarComandoBLE(String comando) {
  esp_task_wdt_delete(NULL);

  if (comando.startsWith("OK_REG,")) {
    String macServidor = comando.substring(7);
    macServidor.trim();

    if (macServidor.length() == 17) {
      macRegistrada = macServidor;
      digitalWrite(LED_VERDE_PIN, HIGH);
      enProcesoRegistro = true;
      tiempoInicioRegistro = millis();
      pendienteEnvioConfig = true;
      tiempoProgramadoEnvio = millis() + DELAY_ENTRE_MENSAJES_BLE;
    }
  } else if (comando == "READY") {
    enProcesoRegistro = false;
    esperandoDatosConfig = false;
    pendienteEnvioConfig = false;

    digitalWrite(LED_VERDE_PIN, HIGH);
    delay(DURACION_LED_CONFIRMACION_MS);
    digitalWrite(LED_VERDE_PIN, LOW);

    completarRegistro(macRegistrada, dispositivo.nombre,
                      String(dispositivo.altura), String(dispositivo.litros));
  } else if (comando == "OK_BAJA") {
    enProcesoRegistro = false;
    bajaAutomaticaActivada = false;

    digitalWrite(LED_ROJO_PIN, HIGH);
    delay(DURACION_LED_CONFIRMACION_MS);
    digitalWrite(LED_ROJO_PIN, LOW);

    limpiarEEPROMYReiniciar();
  } else if (comando == "ERROR:NO_EXISTE_MAC") {
    limpiarEEPROMYReiniciar();
  }

  comandoPendiente = false;
  esp_task_wdt_add(NULL);
}

void debugEstadoBLE() {
  static unsigned long lastDebug = 0;
  if (millis() - lastDebug < 2000)
    return;
  lastDebug = millis();

  Serial.printf("BLE: conn=%d reg=%d\n", deviceConnected, registrado);
}

// ============================================================================
// IMPLEMENTACIÓN - SISTEMA
// ============================================================================

void manejarLED() {
  if (bajaAutomaticaActivada) {
    if (millis() - ultimoCambioLedRojo >= PARPADEO_LED_RAPIDO_MS) {
      estadoLedRojo = !estadoLedRojo;
      digitalWrite(LED_ROJO_PIN, estadoLedRojo);
      ultimoCambioLedRojo = millis();
    }
    digitalWrite(LED_VERDE_PIN, LOW);
    return;
  }

  if (wakeByImpact) {
    bool clienteActivo =
        modoConfiguracionActivo || WiFi.softAPgetStationNum() > 0;
    digitalWrite(LED_VERDE_PIN,
                 clienteActivo ? HIGH : (millis() / PARPADEO_LED_LENTO_MS) % 2);
    digitalWrite(LED_ROJO_PIN, LOW);
    return;
  }

  if (!registrado) {
    digitalWrite(LED_ROJO_PIN, HIGH);
    digitalWrite(LED_VERDE_PIN, LOW);
    return;
  }

  digitalWrite(LED_ROJO_PIN, LOW);
  digitalWrite(LED_VERDE_PIN, LOW);
}

void establecerValoresDeFabrica() {
  memset(&dispositivo, 0, sizeof(dispositivo));
  strncpy(dispositivo.nombre, "Deposito estandar",
          sizeof(dispositivo.nombre) - 1);
  dispositivo.altura = 160;
  dispositivo.litros = 1100;
}

void guardarDatosEnEEPROM() {
  EEPROM.put(EEPROM_ADDR_DATOS, dispositivo);
  EEPROM.commit();
}

void leerDatosDeEEPROM() {
  EEPROM.get(EEPROM_ADDR_DATOS, dispositivo);

  if (strlen(dispositivo.nombre) == 0 || dispositivo.altura == 0 ||
      dispositivo.litros == 0) {
    establecerValoresDeFabrica();
    guardarDatosEnEEPROM();
  }
}

void imprimirDatosDispositivo() {
  Serial.printf("MAC: %s, Nombre: %s, Alt: %d, Lit: %d\n", dispositivo.mac,
                dispositivo.nombre, dispositivo.altura, dispositivo.litros);
}

void limpiarEEPROMYReiniciar() {
  EEPROM.write(EEPROM_ADDR_REGISTRADO, 0);
  establecerValoresDeFabrica();
  EEPROM.put(EEPROM_ADDR_DATOS, dispositivo);
  EEPROM.commit();

  registrado = false;
  macRegistrada = "";

  for (int i = 0; i < 10; i++) {
    digitalWrite(LED_ROJO_PIN, i % 2);
    delay(200);
  }

  delay(3000);
  ESP.restart();
}

float measureDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(3);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  unsigned long duration = pulseIn(ECHO_PIN, HIGH, 35000);
  if (duration == 0)
    return -1.0;

  float distance = duration * 0.0343 / 2.0;
  if (distance < DISTANCIA_MIN_VALIDA_CM || distance > DISTANCIA_MAX_VALIDA_CM)
    return -1.0;

  return distance;
}

int obtenerDistanciaValida() {
  for (int i = 0; i < 5; i++) {
    esp_task_wdt_reset();
    float distancia = measureDistance();

    if (distancia >= DISTANCIA_MIN_VALIDA_CM &&
        distancia <= (dispositivo.altura * 2)) {
      return round(distancia);
    }
    delay(100);
  }

  return DISTANCIA_FALLBACK_CM;
}

int calcularLitros(int distancia, uint32_t alturaTotal, uint32_t litrosTotal) {
  if (alturaTotal == 0 || litrosTotal == 0)
    return 0;
  if (distancia >= alturaTotal)
    return 0;

  int alturaLiquido = alturaTotal - distancia;
  return (alturaLiquido * litrosTotal) / alturaTotal;
}

void enviarDatos(int distancia) {
  int litrosActuales =
      calcularLitros(distancia, dispositivo.altura, dispositivo.litros);
  float voltage = analogRead(ADC_PIN) / 4095.0 * 3.3 * 2.0;
  float temperatura = temperatureRead();

  agregarAlHistorial(litrosActuales);

  if (millis() - ultimoAnalisis >= INTERVALO_ANALISIS) {
    analizarPatronesConsumo(litrosActuales);
    ultimoAnalisis = millis();
  }

  char datosIA[128];
  obtenerDatosIA(datosIA, sizeof(datosIA));

  char mensaje[256];
  snprintf(mensaje, sizeof(mensaje), "001,%s,%d,%d,%d,%d,%d,%s,%s",
           macAddress.c_str(), litrosActuales, (int)round(voltage),
           (int)round(temperatura), dispositivo.altura, dispositivo.litros,
           dispositivo.nombre, datosIA);

  LoRa.beginPacket();
  LoRa.print(mensaje);
  LoRa.endPacket();

  ultimoEnvioDatos = millis();
}

void prepararParaDeepSleep() {
  unsigned long sleepTime = INTERVALO_ENVIO_DATOS;

  if (registrado) {
    unsigned long tiempoDesdeUltimoEnvio = millis() - ultimoEnvioDatos;
    sleepTime = INTERVALO_ENVIO_DATOS - tiempoDesdeUltimoEnvio;
    if (sleepTime < 1000)
      sleepTime = 1000;
    if (sleepTime > INTERVALO_ENVIO_DATOS)
      sleepTime = INTERVALO_ENVIO_DATOS;
    esp_sleep_enable_timer_wakeup(sleepTime * 1000);
  }

  esp_sleep_enable_ext0_wakeup((gpio_num_t)SENSOR_IMPACTO_PIN, 0);
  LoRa.sleep();
  WiFi.mode(WIFI_OFF);
  btStop();
}

void entrarDeepSleep() {
  prepararParaDeepSleep();
  esp_deep_sleep_start();
}

void iniciarLoRaConReintentos() {
  SPI.begin(18, 19, 23, 5);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  for (int i = 0; i < 10 && !LoRa.begin(433E6); i++) {
    digitalWrite(LED_VERDE_PIN, !digitalRead(LED_VERDE_PIN));
    delay(1000);
  }

  LoRa.setTxPower(20, PA_OUTPUT_PA_BOOST_PIN);
  LoRa.setSpreadingFactor(12);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(8);
}

// ============================================================================
// IMPLEMENTACIÓN - IA
// ============================================================================

void agregarAlHistorial(int litros) {
  estadisticas.historial24h[estadisticas.indiceHistorial].timestamp = millis();
  estadisticas.historial24h[estadisticas.indiceHistorial].litros = litros;
  estadisticas.indiceHistorial = (estadisticas.indiceHistorial + 1) % 288;
  if (estadisticas.totalMediciones < 288)
    estadisticas.totalMediciones++;
}

void calcularPromediosBasicos() {
  float totalConsumo = 0;
  int countConsumo = 0;

  for (int i = 1; i < estadisticas.totalMediciones; i++) {
    int consumo = estadisticas.historial24h[i - 1].litros -
                  estadisticas.historial24h[i].litros;
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

  for (int i = 1; i < estadisticas.totalMediciones; i++) {
    int consumo = estadisticas.historial24h[i - 1].litros -
                  estadisticas.historial24h[i].litros;

    if (consumo > 1) {
      estadisticas.consumoConsecutivo++;
    } else {
      estadisticas.consumoConsecutivo = 0;
    }

    if (i % 12 == 0) {
      if (estadisticas.consumoConsecutivo >= 10) {
        horasConConsumoConstante++;
      } else {
        horasConConsumoConstante = 0;
      }
    }

    if (horasConConsumoConstante >= UMBRAL_FUGA)
      return true;
  }

  return false;
}

bool detectarConsumoIrregular() {
  if (estadisticas.totalMediciones < 24)
    return false;

  float consumoPorHora[24] = {0};
  int horasConDatos = 0;

  for (int hora = 0; hora < min(24, estadisticas.totalMediciones / 12);
       hora++) {
    for (int minuto = 0; minuto < 12; minuto++) {
      int index =
          (estadisticas.indiceHistorial - (hora * 12 + minuto) + 288) % 288;
      int prevIndex = (index - 1 + 288) % 288;

      if (prevIndex >= 0 && prevIndex < 288) {
        int consumo = estadisticas.historial24h[prevIndex].litros -
                      estadisticas.historial24h[index].litros;
        if (consumo > 0)
          consumoPorHora[hora] += consumo;
      }
    }
    if (consumoPorHora[hora] > 0)
      horasConDatos++;
  }

  if (horasConDatos >= 3) {
    float promedio = 0;
    for (int i = 0; i < horasConDatos; i++)
      promedio += consumoPorHora[i];
    promedio /= horasConDatos;

    for (int i = 0; i < horasConDatos; i++) {
      if (consumoPorHora[i] > promedio * 3 ||
          (consumoPorHora[i] < promedio * 0.3 && promedio > 10)) {
        return true;
      }
    }
  }

  return false;
}

bool detectarNivelBajo(int litrosActuales) {
  float porcentajeActual = (float)litrosActuales / dispositivo.litros * 100;

  if (porcentajeActual < 20) {
    estadisticas.medicionesConsecutivasBajas++;
  } else {
    estadisticas.medicionesConsecutivasBajas = 0;
  }

  return (estadisticas.medicionesConsecutivasBajas >= (UMBRAL_BAJO_NIVEL * 12));
}

void analizarPatronesConsumo(int litrosActuales) {
  if (estadisticas.totalMediciones < 12) {
    analisisActual.notificacion = 0;
    analisisActual.mensajeNotificacion = "0";
    calcularPromediosBasicos();
    return;
  }

  analisisActual.notificacion = 0;
  analisisActual.mensajeNotificacion = "0";
  calcularPromediosBasicos();

  if (detectarPosibleFuga()) {
    analisisActual.notificacion = 1;
    analisisActual.mensajeNotificacion = "Posible Fuga";
    return;
  }

  if (detectarConsumoIrregular()) {
    analisisActual.notificacion = 2;
    analisisActual.mensajeNotificacion = "Consumo Irregular";
    return;
  }

  if (detectarNivelBajo(litrosActuales)) {
    analisisActual.notificacion = 3;
    analisisActual.mensajeNotificacion = "Nivel bajo";
    return;
  }
}

void obtenerDatosIA(char *buffer, size_t bufferSize) {
  snprintf(
      buffer, bufferSize, "%d,%s,%.1f,%.1f,%.1f", analisisActual.notificacion,
      analisisActual.mensajeNotificacion.c_str(), analisisActual.promedioDiario,
      analisisActual.promedioSemanal, analisisActual.promedioMensual);
}

// ============================================================================
// SETUP Y LOOP
// ============================================================================

void setup() {
  esp_task_wdt_init(30, true);
  esp_task_wdt_add(NULL);

  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  Serial.begin(115200);
  Serial.println("\n🚀 NUUP01 Iniciando...");

  if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
    wakeByImpact = true;
    Serial.println("⚡ Wake por impacto");
  }

  EEPROM.begin(EEPROM_SIZE);
  registrado = EEPROM.read(EEPROM_ADDR_REGISTRADO) == 1;
  leerDatosDeEEPROM();

  Serial.println("📱 Iniciando BLE...");
  BLEDevice::init("NUUP_Controller");
  BLEDevice::setPower(ESP_PWR_LVL_P7, ESP_BLE_PWR_TYPE_DEFAULT);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LED_VERDE_PIN, OUTPUT);
  pinMode(LED_ROJO_PIN, OUTPUT);
  pinMode(SENSOR_IMPACTO_PIN, INPUT_PULLUP);

  if (wakeByImpact && !confirmarGolpesImpacto()) {
    prepararParaDeepSleep();
    esp_deep_sleep_start();
  }

  macAddress = WiFi.macAddress();
  macAddress.replace("-", ":");

  iniciarLoRaConReintentos();
  configurarWiFiAP();

  estadisticas.ultimoResetEstadisticas = millis();
  analisisActual.notificacion = 0;

  Serial.println("✅ Setup completado");
}

void loop() {
  esp_task_wdt_reset();

  bool comunicacionesHabilitadas =
      wakeByImpact || !registrado || modoConfiguracionActivo;

  if (comunicacionesHabilitadas) {
    verificarConexionCliente();
    server.handleClient();
    dnsServer.processNextRequest();
  }

  manejarLED();

  // Enviar CONFIG pendiente BLE
  if (comunicacionesHabilitadas && pendienteEnvioConfig &&
      millis() >= tiempoProgramadoEnvio) {
    if (deviceConnected && pClient && pClient->isConnected()) {
      char configCommand[128];
      snprintf(configCommand, sizeof(configCommand), "CONFIG,%s,%d,%d",
               dispositivo.nombre, dispositivo.altura, dispositivo.litros);
      sendCommand(configCommand);
      pendienteEnvioConfig = false;
    }
  }

  // Escaneo BLE periódico
  unsigned long intervaloEscaneo =
      registrado ? INTERVALO_ESCANEO_BAJA : INTERVALO_ESCANEO_ALTA;
  if (comunicacionesHabilitadas &&
      (ultimoEscaneoBLE == 0 ||
       (millis() - ultimoEscaneoBLE >= intervaloEscaneo))) {

    scanForDevices();

    if (doConnect && connectToServer()) {
      if (!registrado) {
        char solicitudRegistro[32];
        snprintf(solicitudRegistro, sizeof(solicitudRegistro), "REG:%s",
                 macAddress.c_str());
        sendCommand(solicitudRegistro);
        enProcesoRegistro = true;
        tiempoInicioRegistro = millis();
      } else {
        char solicitudBaja[32];
        snprintf(solicitudBaja, sizeof(solicitudBaja), "BAJA:%s",
                 macAddress.c_str());
        sendCommand(solicitudBaja);
        bajaAutomaticaActivada = true;
        tiempoInicioBaja = millis();
      }
    }

    ultimoEscaneoBLE = millis();
  }

  // Medición sensor (solo si registrado)
  if (registrado && !enProcesoRegistro && !bajaAutomaticaActivada &&
      !wakeByImpact) {
    if (ultimoEnvioDatos < 0 ||
        (millis() - ultimoEnvioDatos >= INTERVALO_ENVIO_DATOS)) {
      int distancia = obtenerDistanciaValida();
      enviarDatos(distancia);
      ultimoEnvioDatos = millis();
      wakeByImpact = false;
      entrarDeepSleep();
    }
  }

  // Timeout registro/baja
  if (bajaAutomaticaActivada && (millis() - tiempoInicioBaja > TIMEOUT_BAJA)) {
    bajaAutomaticaActivada = false;
    if (pClient && pClient->isConnected())
      pClient->disconnect();
  }

  if (enProcesoRegistro &&
      (millis() - tiempoInicioRegistro > TIMEOUT_REGISTRO_COMPLETO)) {
    ESP.restart();
  }

  // Timeout impacto
  if (wakeByImpact &&
      (millis() - inicioVigiliaImpacto >= IMPACTO_TIEMPO_VIGILIA_MS)) {
    bool sesionActiva = modoConfiguracionActivo ||
                        WiFi.softAPgetStationNum() > 0 || deviceConnected ||
                        enProcesoRegistro || bajaAutomaticaActivada;
    if (!sesionActiva) {
      wakeByImpact = false;
      ESP.restart();
    }
  }

  delay(50);
}
