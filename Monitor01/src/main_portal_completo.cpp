/*******************************************************************************
 * MONITOR01 - PORTAL COMPLETO CON WHATSAPP
 *
 * Features:
 * - Portal cautivo moderno (diseño v2)
 * - Gestión de redes WiFi
 * - Notificaciones WhatsApp
 * - Nombre del hogar
 * - Reset de fábrica
 * - Dispositivos simulados (para testing)
 ******************************************************************************/

#include <Arduino.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

// ============================================================================
// ROOT CA CERTIFICATE (DigiCert Global Root CA for ngrok/HTTPS)
// ============================================================================
const char *root_ca =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDrzCCApegAwIBAgIQCDvgVpBCRrGhdWrJWZHHSjANBgkqhkiG9w0BAQUFADBh\n"
    "MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\n"
    "d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBD\n"
    "QTAeFw0wNjExMTAwMDAwMDBaFw0zMTExMTAwMDAwMDBaMGExCzAJBgNVBAYTAlVT\n"
    "MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j\n"
    "b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IENBMIIBIjANBgkqhkiG\n"
    "9w0BAQEFAAOCAQ8AMIIBCgKCAQEA4jvhEXLeqKTTo1eqUKKPC3eQyaKl7hLOllsB\n"
    "CSDMAZOnTjC3U/dDxGkAV53ijSLdhwZAAIEJzs4bg7/fzTtxRuLWZscFs3YnFo97\n"
    "nh6Vfe63SKMI2tavegw5BmV/Sl0fvBf4q77uKNd0f3p4mVmFaG5cIzJLv07A6Fpt\n"
    "43C/dxC//AH2hdmoRBBYMql1GNXRor5H4idq9Joz+EkIYIvUX7Q6hL+hqkpMfT7P\n"
    "T19sdl6gSzeRntwi5m3OFBqOasv+zbMUZBfHWymeMr/y7vrTC0LUq7dBMtoM1O/4\n"
    "gdW7jVg/tRvoSSiicNoxBN33shbyTApOB6jtSj1etX+jkMOvJwIDAQABo2MwYTAO\n"
    "BgNVHQ8BAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4EFgQUA95QNVbR\n"
    "TLtm8KPiGxvDl7I90VUwHwYDVR0jBBgwFoAUA95QNVbRTLtm8KPiGxvDl7I90VUw\n"
    "DQYJKoZIhvcNAQEFBQADggEBAMucN6pIExIK+t1EnE9SsPTfrgT1eXkIoyQY/Esr\n"
    "hMAtudXH/vTBH1jLuG2cenTnmCmrEbXjcKChzUyImZOMkXDiqw8cvpOp/2PV5Adg\n"
    "06O/nVsJ8dWO41P0jmP6P6fbtGbfYmbW0W5BjfIttep3Sp+dWOIrWcBAI+0tKIJF\n"
    "PnlUkiaY4IBIqDfv8NZ5YBberOgOzW6sRBc4L0na4UU+Krk2U886UAb3LujEV0ls\n"
    "YSEY1QSteDwsOoBrp+uvFRTp2InBuThs4pFsiv9kuXclVzDAGySj4dzp30d8tbQk\n"
    "CAUw7C29C79Fv1C5qfPrmAESrciIxpg0X40KPMbp1ZWVbd4=\n"
    "-----END CERTIFICATE-----\n";

// ============================================================================
// CONFIGURACIÓN
// ============================================================================

const char *ssidAP = "NUUP_Monitor";
const char *passwordAP = "";

// EEPROM Addresses
#define EEPROM_SIZE 2048
#define HOME_NAME_ADDR 0      // 64 bytes
#define PHONE_ADDR 100        // 16 bytes
#define WA_NOTIFY_ADDR 120    // 1 byte
#define SEND_WELCOME_ADDR 121 // 1 byte
#define WIFI_SSID_ADDR 150    // 64 bytes
#define WIFI_PASS_ADDR 220    // 64 bytes
#define CHATBOT_URL_ADDR 300  // 128 bytes

// Default chatbot URL
const char *DEFAULT_CHATBOT_URL =
    "https://nuup-chatbot.onrender.com/send-welcome";

// ============================================================================
// VARIABLES GLOBALES
// ============================================================================

DNSServer dnsServer;
WebServer server(80);
String macAddress = "";

// Config data
char homeName[64] = "Mi hogar";
char phoneNumber[16] = "";
bool whatsappNotify = false;
bool sendWelcome = false;
char wifiSSID[64] = "";
char wifiPass[64] = "";
char chatbotUrl[128];

// WiFi scan cache
String scannedNetworksCache = "";
unsigned long lastScan = 0;
const unsigned long SCAN_INTERVAL = 15000;

// ============================================================================
// FUNCIONES - EEPROM
// ============================================================================

void loadConfigFromEEPROM() {
  EEPROM.get(HOME_NAME_ADDR, homeName);
  EEPROM.get(PHONE_ADDR, phoneNumber);
  EEPROM.get(WIFI_SSID_ADDR, wifiSSID);
  EEPROM.get(WIFI_PASS_ADDR, wifiPass);
  EEPROM.get(CHATBOT_URL_ADDR, chatbotUrl);
  whatsappNotify = EEPROM.read(WA_NOTIFY_ADDR) == 1;
  sendWelcome = EEPROM.read(SEND_WELCOME_ADDR) == 1;

  // Set defaults if empty
  if (strlen(homeName) == 0 || homeName[0] == 0xFF) {
    strcpy(homeName, "Mi hogar");
  }
  if (strlen(chatbotUrl) == 0 || chatbotUrl[0] == 0xFF) {
    strcpy(chatbotUrl, DEFAULT_CHATBOT_URL);
  }

  Serial.println("\n✅ Configuración cargada:");
  Serial.printf("   Hogar: %s\n", homeName);
  Serial.printf("   Teléfono: %s\n", phoneNumber);
  Serial.printf("   WhatsApp: %s\n", whatsappNotify ? "ON" : "OFF");
  Serial.printf("   WiFi SSID: %s\n", wifiSSID);
}

void saveConfigToEEPROM() {
  EEPROM.put(HOME_NAME_ADDR, homeName);
  EEPROM.put(PHONE_ADDR, phoneNumber);
  EEPROM.put(WIFI_SSID_ADDR, wifiSSID);
  EEPROM.put(WIFI_PASS_ADDR, wifiPass);
  EEPROM.put(CHATBOT_URL_ADDR, chatbotUrl);
  EEPROM.write(WA_NOTIFY_ADDR, whatsappNotify ? 1 : 0);
  EEPROM.write(SEND_WELCOME_ADDR, sendWelcome ? 1 : 0);
  EEPROM.commit();

  Serial.println("💾 Configuración guardada en EEPROM");
}

void factoryReset() {
  Serial.println("🗑️ Ejecutando reset de fábrica...");
  for (int i = 0; i < EEPROM_SIZE; i++) {
    EEPROM.write(i, 0xFF);
  }
  EEPROM.commit();
  Serial.println("✅ EEPROM borrada. Reiniciando...");
}

// ============================================================================
// FUNCIONES - WHATSAPP
// ============================================================================

void sendWelcomeMessage() {
  if (strlen(phoneNumber) == 0) {
    Serial.println("⚠️ No hay número de teléfono configurado");
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠️ WiFi no conectado. No se puede enviar mensaje");
    return;
  }

  Serial.println("\n📱 Enviando mensaje de bienvenida por WhatsApp...");
  Serial.printf("   Teléfono: +52 1 %s\n", phoneNumber);
  Serial.printf("   URL: %s\n", chatbotUrl);

  bool useHTTPS = String(chatbotUrl).startsWith("https://");

  if (useHTTPS) {
    // HTTPS connection
    WiFiClientSecure *client = new WiFiClientSecure;
    if (client) {
      client->setInsecure(); // Skip cert verification

      HTTPClient https;
      if (https.begin(*client, chatbotUrl)) {
        https.addHeader("Content-Type", "application/json");
        https.setTimeout(45000); // 45s for Render free tier

        // JSON payload con el número completo
        String payload = "{\"phone\":\"521" + String(phoneNumber) + "\"}";
        Serial.printf("   Payload: %s\n", payload.c_str());

        int httpCode = https.POST(payload);

        if (httpCode > 0) {
          String response = https.getString();
          Serial.printf("   HTTP %d: %s\n", httpCode, response.c_str());

          if (httpCode == 200) {
            sendWelcome = false;
            EEPROM.write(SEND_WELCOME_ADDR, 0);
            EEPROM.commit();
            Serial.println("✅ Mensaje de bienvenida enviado");
          }
        } else {
          Serial.printf("❌ Error HTTP: %d\n", httpCode);
        }
        https.end();
      }
      delete client;
    }
  } else {
    // HTTP connection
    HTTPClient http;
    if (http.begin(chatbotUrl)) {
      http.addHeader("Content-Type", "application/json");
      http.setTimeout(10000);

      String payload = "{\"phone\":\"521" + String(phoneNumber) + "\"}";
      Serial.printf("   Payload: %s\n", payload.c_str());

      int httpCode = http.POST(payload);

      if (httpCode > 0) {
        String response = http.getString();
        Serial.printf("   HTTP %d: %s\n", httpCode, response.c_str());

        if (httpCode == 200) {
          sendWelcome = false;
          EEPROM.write(SEND_WELCOME_ADDR, 0);
          EEPROM.commit();
          Serial.println("✅ Mensaje de bienvenida enviado");
        }
      } else {
        Serial.printf("❌ Error HTTP: %d\n", httpCode);
      }
      http.end();
    }
  }
}

// ============================================================================
// FUNCIONES - WIFI
// ============================================================================

String escanearRedes() {
  if (millis() - lastScan < SCAN_INTERVAL &&
      scannedNetworksCache.length() > 0) {
    return scannedNetworksCache;
  }

  Serial.println("📶 Escaneando redes WiFi...");
  int n = WiFi.scanNetworks();

  // Filtrar SSIDs duplicados (solo mostrar una vez cada red)
  String ssidsAdded[20];
  int ssidCount = 0;
  String result = "[";
  bool first = true;

  for (int i = 0; i < n && ssidCount < 20; i++) {
    String currentSSID = WiFi.SSID(i);

    // Verificar si ya agregamos este SSID
    bool isDuplicate = false;
    for (int j = 0; j < ssidCount; j++) {
      if (ssidsAdded[j] == currentSSID) {
        isDuplicate = true;
        break;
      }
    }

    if (!isDuplicate && currentSSID.length() > 0) {
      if (!first)
        result += ",";
      first = false;

      result += "{";
      result += "\"ssid\":\"" + currentSSID + "\",";
      result +=
          "\"secure\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
      result += "}";

      ssidsAdded[ssidCount++] = currentSSID;
    }
  }
  result += "]";

  scannedNetworksCache = result;
  lastScan = millis();
  Serial.printf("   Encontradas: %d redes únicas\n", ssidCount);

  WiFi.scanDelete();
  return result;
}

bool conectarWiFi() {
  if (strlen(wifiSSID) == 0) {
    Serial.println("⚠️ No hay SSID configurado");
    return false;
  }

  Serial.printf("\n📡 Conectando a: %s\n", wifiSSID);
  WiFi.begin(wifiSSID, wifiPass);

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    delay(500);
    Serial.print(".");
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi conectado");
    Serial.printf("   IP: %s\n", WiFi.localIP().toString().c_str());

    // Si hay flag de enviar bienvenida, hacerlo ahora
    if (sendWelcome) {
      delay(1000); // Esperar a que la conexión se estabilice
      sendWelcomeMessage();
    }

    return true;
  } else {
    Serial.println("\n❌ Fallo al conectar WiFi");
    return false;
  }
}

// ============================================================================
// PORTAL WEB - HTML/CSS/JS EMBEBIDO
// ============================================================================

const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>NUUP Monitor</title>
    <style>
        :root {
            --primary: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            --success: #4CAF50;
            --warning: #FFC107;
            --danger: #F44336;
            --text: #2c3e50;
            --light: #7f8c8d;
            --bg: #f8f9fa;
        }
        * { margin:0; padding:0; box-sizing:border-box; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            background: var(--bg);
            color: var(--text);
        }
        .screen { display:none; min-height:100vh; flex-direction:column; }
        .screen.active { display:flex; }
        .header {
            background: var(--primary);
            color: white;
            padding: 20px;
            box-shadow: 0 4px 6px rgba(0,0,0,0.1);
        }
        .header-title { font-size: 24px; font-weight: 700; }
        .header-subtitle { font-size: 14px; opacity: 0.9; margin-top: 3px; }
        .content {
            flex: 1;
            padding: 20px;
            max-width: 600px;
            margin: 0 auto;
            width: 100%;
            padding-bottom: 100px;
        }
        .config-section {
            background: white;
            padding: 20px;
            border-radius: 16px;
            box-shadow: 0 4px 6px rgba(0,0,0,0.1);
            margin-bottom: 20px;
        }
        .section-title {
            font-size: 18px;
            font-weight: 700;
            margin-bottom: 15px;
        }
        .form-group { margin: 20px 0; }
        .form-label {
            display: block;
            font-size: 14px;
            font-weight: 600;
            margin-bottom: 8px;
        }
        .form-input {
            width: 100%;
            padding: 14px 15px;
            border: 2px solid #e0e0e0;
            border-radius: 8px;
            font-size: 16px;
        }
        .form-input:focus {
            outline: none;
            border-color: #667eea;
            box-shadow: 0 0 0 4px rgba(102, 126, 234, 0.1);
        }
        .form-hint { font-size: 12px; color: var(--light); margin-top: 5px; }
        .btn {
            width: 100%;
            padding: 16px;
            border: none;
            border-radius: 8px;
            font-size: 16px;
            font-weight: 600;
            cursor: pointer;
            transition: all 0.3s;
        }
        .btn-primary {
            background: var(--primary);
            color: white;
            box-shadow: 0 4px 6px rgba(0,0,0,0.1);
        }
        .btn-primary:hover { transform: translateY(-2px); }
        .btn-danger { background: var(--danger); color: white; }
        .toggle-group {
            background: #f8f9fa;
            padding: 16px;
            border-radius: 8px;
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin: 15px 0;
        }
        .toggle-switch { position: relative; width: 60px; height: 30px; }
        .toggle-switch input { opacity: 0; width: 0; height: 0; }
        .toggle-slider {
            position: absolute;
            cursor: pointer;
            top: 0;
            left: 0;
            right: 0;
            bottom: 0;
            background-color: #ccc;
            transition: 0.3s;
            border-radius: 30px;
        }
        .toggle-slider:before {
            position: absolute;
            content: "";
            height: 22px;
            width: 22px;
            left: 4px;
            bottom: 4px;
            background-color: white;
            transition: 0.3s;
            border-radius: 50%;
        }
        input:checked + .toggle-slider { background: linear-gradient(135deg, #667eea, #764ba2); }
        input:checked + .toggle-slider:before { transform: translateX(30px); }
        .wifi-list { display: grid; gap: 10px; margin: 15px 0; }
        .wifi-item {
            background: #f8f9fa;
            padding: 15px;
            border-radius: 8px;
            cursor: pointer;
            border: 2px solid transparent;
            transition: 0.3s;
        }
        .wifi-item:hover { border-color: #667eea; background: white; }
        .wifi-item.selected {
            border-color: #667eea;
            background: rgba(102, 126, 234, 0.05);
        }
        .bottom-nav {
            position: fixed;
            bottom: 0;
            left: 0;
            right: 0;
            background: white;
            box-shadow: 0 -2px 10px rgba(0,0,0,0.1);
            display: grid;
            grid-template-columns: repeat(3, 1fr);
            padding: 10px 0;
            z-index: 100;
        }
        .nav-item {
            display: flex;
            flex-direction: column;
            align-items: center;
            gap: 5px;
            cursor: pointer;
            padding: 10px;
            color: var(--light);
            border: none;
            background: none;
        }
        .nav-item.active { color: #667eea; }
        .nav-icon { font-size: 26px; }
        .nav-label { font-size: 12px; font-weight: 600; }
        .loader-overlay {
            display: none;
            position: fixed;
            top: 0;
            left: 0;
            right: 0;
            bottom: 0;
            background: rgba(0,0,0,0.7);
            z-index: 9999;
            align-items: center;
            justify-content: center;
            flex-direction: column;
        }
        .loader-overlay.active { display: flex; }
        .spinner {
            width: 60px;
            height: 60px;
            border: 6px solid rgba(255,255,255,0.3);
            border-top-color: white;
            border-radius: 50%;
            animation: spin 1s linear infinite;
        }
        @keyframes spin { to { transform: rotate(360deg); } }
        .loader-text { color: white; margin-top: 20px; font-size: 16px; }
        #phoneField { display: none; }
    </style>
</head>
<body>
    
    <!-- Loader -->
    <div class="loader-overlay" id="loader">
        <div class="spinner"></div>
        <div class="loader-text" id="loaderText">Cargando...</div>
    </div>
    
    <!-- Screen: Inicio -->
    <div class="screen active" id="screen-home">
        <div class="header">
            <div class="header-title">NUUP</div>
            <div class="header-subtitle" id="homeName">Mi hogar</div>
        </div>
        <div class="content">
            <div style="text-align:center;padding:60px 20px">
                <div style="font-size:80px">💧</div>
                <h2>Bienvenido a NUUP</h2>
                <p style="color:var(--light);margin:20px 0">
                    Monitorea tus dispositivos desde cualquier lugar
                </p>
                <p style="font-size:14px;color:var(--light);margin-top:30px">
                    Conecta tus sensores NUUP y configura WiFi/WhatsApp en la pestaña Configuración
                </p>
            </div>
        </div>
        <div class="bottom-nav">
            <button class="nav-item active" onclick="goTo('home')">
                <div class="nav-icon">🏠</div>
                <div class="nav-label">Inicio</div>
            </button>
            <button class="nav-item" onclick="goTo('alerts')">
                <div class="nav-icon">🔔</div>
                <div class="nav-label">Alertas</div>
            </button>
            <button class="nav-item" onclick="goTo('config')">
                <div class="nav-icon">⚙️</div>
                <div class="nav-label">Config</div>
            </button>
        </div>
    </div>
    
    <!-- Screen: Alertas -->
    <div class="screen" id="screen-alerts">
        <div class="header">
            <div class="header-title">Alertas</div>
            <div class="header-subtitle">Notificaciones recientes</div>
        </div>
        <div class="content">
            <div style="text-align:center;padding:60px 20px">
                <div style="font-size:80px;opacity:0.3">🔔</div>
                <h3>No hay alertas</h3>
                <p style="color:var(--light);margin-top:10px">
                    Cuando tus dispositivos necesiten atención, las verás aquí
                </p>
            </div>
        </div>
        <div class="bottom-nav">
            <button class="nav-item" onclick="goTo('home')">
                <div class="nav-icon">🏠</div>
                <div class="nav-label">Inicio</div>
            </button>
            <button class="nav-item active" onclick="goTo('alerts')">
                <div class="nav-icon">🔔</div>
                <div class="nav-label">Alertas</div>
            </button>
            <button class="nav-item" onclick="goTo('config')">
                <div class="nav-icon">⚙️</div>
                <div class="nav-label">Config</div>
            </button>
        </div>
    </div>
    
    <!-- Screen: Configuración -->
    <div class="screen" id="screen-config">
        <div class="header">
            <div class="header-title">Configuración</div>
            <div class="header-subtitle">WiFi y Notificaciones</div>
        </div>
        <div class="content">
            
            <!-- Nombre del Hogar -->
            <div class="config-section">
                <div class="section-title">🏠 Nombre del Hogar</div>
                <div class="form-group">
                    <input type="text" class="form-input" id="homeNameInput" placeholder="Mi hogar">
                </div>
                <button class="btn btn-primary" onclick="saveHomeName()">💾 Guardar</button>
            </div>
            
            <!-- WiFi -->
            <div class="config-section">
                <div class="section-title">📶 WiFi</div>
                <p style="font-size:13px;color:var(--light);margin-bottom:15px">
                    El monitor funciona sin WiFi, conéctalo para recibir notificaciones
                </p>
                <div class="wifi-list" id="wifiList"></div>
                <div class="form-group">
                    <label class="form-label">Contraseña WiFi</label>
                    <input type="password" class="form-input" id="wifiPass" placeholder="Contraseña">
                </div>
                <button class="btn btn-primary" onclick="connectWiFi()">📡 Conectar</button>
            </div>
            
            <!-- WhatsApp -->
            <div class="config-section">
                <div class="section-title">💬 Notificaciones WhatsApp</div>
                <p style="font-size:13px;color:var(--light);margin-bottom:15px">
                    Recibe alertas cuando tus dispositivos necesiten atención
                </p>
                <div class="toggle-group">
                    <span>Activar notificaciones</span>
                    <label class="toggle-switch">
                        <input type="checkbox" id="waToggle" onchange="toggleWA()">
                        <span class="toggle-slider"></span>
                    </label>
                </div>
                <div id="phoneField">
                    <div class="form-group">
                        <label class="form-label">Numero de Telefono</label>
                        <input type="tel" class="form-input" id="phone" placeholder="10 digitos" maxlength="10">
                        <div class="form-hint">Solo numeros, sin espacios</div>
                    </div>
                    <button class="btn btn-primary" onclick="saveWA()">💾 Guardar</button>
                </div>
            </div>
            
            <!-- Reset -->
            <div class="config-section">
                <div class="section-title">🗑️ Reset de Fábrica</div>
                <p style="font-size:13px;color:var(--light);margin-bottom:15px">
                    Borra toda la configuración. No se puede deshacer.
                </p>
                <button class="btn btn-danger" onclick="factoryReset()">⚠️ Restaurar</button>
            </div>
            
        </div>
        <div class="bottom-nav">
            <button class="nav-item" onclick="goTo('home')">
                <div class="nav-icon">🏠</div>
                <div class="nav-label">Inicio</div>
            </button>
            <button class="nav-item" onclick="goTo('alerts')">
                <div class="nav-icon">🔔</div>
                <div class="nav-label">Alertas</div>
            </button>
            <button class="nav-item active" onclick="goTo('config')">
                <div class="nav-icon">⚙️</div>
                <div class="nav-label">Config</div>
            </button>
        </div>
    </div>
    
    <script>
        let selectedSSID = '';
        
        function goTo(screen) {
            document.querySelectorAll('.screen').forEach(s => s.classList.remove('active'));
            document.getElementById('screen-' + screen).classList.add('active');
            document.querySelectorAll('.nav-item').forEach(n => n.classList.remove('active'));
            event.target.closest('.nav-item').classList.add('active');
            if (screen === 'config') loadData();
        }
        
        function showLoader(text) {
            document.getElementById('loader').classList.add('active');
            document.getElementById('loaderText').textContent = text;
        }
        
        function hideLoader() {
            document.getElementById('loader').classList.remove('active');
        }
        
        function loadData() {
            // Cargar nombre del hogar
            fetch('/get_config').then(r=>r.json()).then(data=>{
                document.getElementById('homeNameInput').value = data.home_name;
                document.getElementById('homeName').textContent = data.home_name;
                document.getElementById('phone').value = data.phone;
                document.getElementById('waToggle').checked = data.wa_notify;
                toggleWA();
            });
            
            // Escanear redes
            showLoader('Escaneando WiFi...');
            fetch('/scan_wifi').then(r=>r.json()).then(networks=>{
                hideLoader();
                let html = '';
                networks.forEach(net => {
                    html += `<div class="wifi-item" onclick="selectWiFi('${net.ssid}')">
                        <div>📶 ${net.ssid}</div>
                    </div>`;
                });
                document.getElementById('wifiList').innerHTML = html;
            }).catch(()=>hideLoader());
        }
        
        function selectWiFi(ssid) {
            selectedSSID = ssid;
            document.querySelectorAll('.wifi-item').forEach(w => w.classList.remove('selected'));
            event.target.closest('.wifi-item').classList.add('selected');
        }
        
        function toggleWA() {
            document.getElementById('phoneField').style.display = 
                document.getElementById('waToggle').checked ? 'block' : 'none';
        }
        
        function saveHomeName() {
            const name = document.getElementById('homeNameInput').value;
            showLoader('Guardando...');
            fetch('/save_home', {
                method: 'POST',
                headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                body: 'name=' + encodeURIComponent(name)
            }).then(()=>{
                hideLoader();
                document.getElementById('homeName').textContent = name;
                alert('✅ Nombre guardado');
            });
        }
        
        function connectWiFi() {
            if (!selectedSSID) return alert('Selecciona una red WiFi');
            const pass = document.getElementById('wifiPass').value;
            showLoader('Conectando WiFi...');
            fetch('/save_wifi', {
                method: 'POST',
                headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                body: `ssid=${encodeURIComponent(selectedSSID)}&pass=${encodeURIComponent(pass)}`
            }).then(r=>r.text()).then(msg=>{
                hideLoader();
                alert(msg);
            });
        }
        
        function saveWA() {
            const phone = document.getElementById('phone').value;
            const notify = document.getElementById('waToggle').checked;
            
            if (notify && phone.length !== 10) {
                return alert('❌ El número debe tener 10 dígitos');
            }
            
            showLoader('Guardando WhatsApp...');
            fetch('/save_whatsapp', {
                method: 'POST',
                headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                body: `phone=${phone}&notify=${notify?'1':'0'}`
            }).then(r=>r.text()).then(msg=>{
                hideLoader();
                alert(msg);
            });
        }
        
        function factoryReset() {
            if (!confirm('⚠️ ¿Borrar toda la configuración?')) return;
            if (!confirm('🚨 ¿Estás seguro? No se puede deshacer')) return;
            showLoader('Borrando...');
            fetch('/factory_reset', {method:'POST'}).then(()=>{
                alert('✅ Reset completado. Reiniciando...');
                setTimeout(()=>location.reload(), 1000);
            });
        }
        
        // Cargar datos al inicio
        loadData();
    </script>
</body>
</html>
)rawliteral";

// ============================================================================
// HANDLERS DEL SERVIDOR WEB
// ============================================================================

void handleRoot() { server.send_P(200, "text/html", HTML_PAGE); }

void handleGetConfig() {
  String json = "{";
  json += "\"home_name\":\"" + String(homeName) + "\",";
  json += "\"phone\":\"" + String(phoneNumber) + "\",";
  json += "\"wa_notify\":" + String(whatsappNotify ? "true" : "false");
  json += "}";
  server.send(200, "application/json", json);
}

void handleScanWiFi() {
  String json = escanearRedes();
  server.send(200, "application/json", json);
}

void handleSaveHome() {
  if (server.hasArg("name")) {
    String name = server.arg("name");
    strncpy(homeName, name.c_str(), sizeof(homeName) - 1);
    saveConfigToEEPROM();
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Missing name");
  }
}

void handleSaveWiFi() {
  if (server.hasArg("ssid") && server.hasArg("pass")) {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");

    strncpy(wifiSSID, ssid.c_str(), sizeof(wifiSSID) - 1);
    strncpy(wifiPass, pass.c_str(), sizeof(wifiPass) - 1);
    saveConfigToEEPROM();

    // Intentar conectar
    bool success = conectarWiFi();

    if (success) {
      server.send(200, "text/plain", "✅ WiFi conectado correctamente");
    } else {
      server.send(200, "text/plain",
                  "❌ Error al conectar. Verifica la contraseña");
    }
  } else {
    server.send(400, "text/plain", "Missing parameters");
  }
}

void handleSaveWhatsApp() {
  if (server.hasArg("phone") && server.hasArg("notify")) {
    String phone = server.arg("phone");
    bool notify = server.arg("notify") == "1";

    if (notify && phone.length() == 10) {
      strncpy(phoneNumber, phone.c_str(), sizeof(phoneNumber) - 1);
      whatsappNotify = true;
      sendWelcome = true; // Activar flag para enviar mensaje
      saveConfigToEEPROM();

      // Si ya hay WiFi, enviar ahora
      if (WiFi.status() == WL_CONNECTED) {
        sendWelcomeMessage();
      }

      server.send(200, "text/plain",
                  "✅ WhatsApp configurado\nRecibirás un mensaje de bienvenida "
                  "en: +52 1 " +
                      phone);
    } else if (!notify) {
      whatsappNotify = false;
      phoneNumber[0] = '\0';
      sendWelcome = false;
      saveConfigToEEPROM();
      server.send(200, "text/plain", "✅ Notificaciones WhatsApp desactivadas");
    } else {
      server.send(400, "text/plain",
                  "❌ Número inválido (debe tener 10 dígitos)");
    }
  }
}

void handleFactoryReset() {
  factoryReset();
  server.send(200, "text/plain", "OK");
  delay(1000);
  ESP.restart();
}

// ============================================================================
// SETUP
// ============================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║  MONITOR01 - PORTAL COMPLETO         ║");
  Serial.println("║  WiFi + WhatsApp + Portal Moderno    ║");
  Serial.println("╚════════════════════════════════════════╝\n");

  // EEPROM
  EEPROM.begin(EEPROM_SIZE);
  loadConfigFromEEPROM();

  // MAC
  macAddress = WiFi.macAddress();
  Serial.printf("📱 MAC: %s\n\n", macAddress.c_str());

  // WiFi AP
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(ssidAP, passwordAP);
  IPAddress IP = WiFi.softAPIP();

  Serial.println("✅ Modo AP iniciado:");
  Serial.printf("   SSID: %s\n", ssidAP);
  Serial.printf("   IP: %s\n\n", IP.toString().c_str());

  // DNS
  dnsServer.start(53, "*", IP);
  Serial.println("✅ DNS Server iniciado (captive portal)\n");

  // Web Server
  server.on("/", handleRoot);
  server.on("/get_config", handleGetConfig);
  server.on("/scan_wifi", handleScanWiFi);
  server.on("/save_home", HTTP_POST, handleSaveHome);
  server.on("/save_wifi", HTTP_POST, handleSaveWiFi);
  server.on("/save_whatsapp", HTTP_POST, handleSaveWhatsApp);
  server.on("/factory_reset", HTTP_POST, handleFactoryReset);

  // Captive portal redirects
  server.on("/generate_204", []() { server.send(204); });
  server.on("/hotspot-detect.html", []() {
    server.sendHeader("Location", "http://" + WiFi.softAPIP().toString());
    server.send(302);
  });
  server.onNotFound([]() {
    server.sendHeader("Location", "http://" + WiFi.softAPIP().toString());
    server.send(302);
  });

  server.begin();
  Serial.println("✅ Servidor Web iniciado\n");

  // Intentar conectar a WiFi guardado
  if (strlen(wifiSSID) > 0) {
    conectarWiFi();
  }

  Serial.println("╔════════════════════════════════════════╗");
  Serial.println("║         PORTAL LISTO                  ║");
  Serial.println("╠════════════════════════════════════════╣");
  Serial.printf("║  Conecta: %s\n", ssidAP);
  Serial.println("║  IP: " + IP.toString());
  Serial.println("╚════════════════════════════════════════╝\n");
}

// ============================================================================
// LOOP
// ============================================================================

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  delay(10);
}
