/*******************************************************************************
 * MONITOR01 - TEST PORTAL CAUTIVO COMPLETO
 *
 * Versión completa del portal para testing sin hardware
 * Incluye: WiFi, UserProfile, Dispositivos simulados, Bajas
 *
 * Sin: LoRa, BLE, MQTT real, OLED
 ******************************************************************************/

#include <Arduino.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <WebServer.h>
#include <WiFi.h>


// ============================================================================
// CONFIGURACIÓN
// ============================================================================

const char *ssidAP = "NUUP_Monitor_Test";
const char *passwordAP = "";

// EEPROM
#define EEPROM_SIZE 2048
#define USER_ID_ADDR 0
#define USER_EMAIL_ADDR 100
#define USER_NAME_ADDR 200
#define USER_PHONE_ADDR 300
#define USER_PASS_ADDR 400
#define USER_FLAG_ADDR 500
#define NETWORKS_ADDR 600
#define DEVICES_ADDR 1000

// Límites
#define MAX_NETWORKS 3
#define MAX_DEVICES 10
#define SSID_LEN 32
#define PASS_LEN 64

// ============================================================================
// ESTRUCTURAS
// ============================================================================

struct UserProfile {
  char id[32];
  char email[64];
  char nombre[64];
  char telefono[24];
  char password[32];
  bool registrado;
} user = {0};

struct WiFiNetwork {
  char ssid[SSID_LEN];
  char password[PASS_LEN];
  bool active;
};

struct MockDevice {
  char mac[18];
  char nombre[32];
  int litros;
  int porcentaje;
  float voltaje;
  bool activo;
};

// ============================================================================
// VARIABLES GLOBALES
// ============================================================================

DNSServer dnsServer;
WebServer server(80);
String macAddress = "";
WiFiNetwork savedNetworks[MAX_NETWORKS];
MockDevice mockDevices[MAX_DEVICES];
int deviceCount = 0;

// Cache de escaneo WiFi
String scannedNetworksCache = "";
unsigned long lastScan = 0;
const unsigned long SCAN_INTERVAL = 15000;

// ============================================================================
// FUNCIONES - USER PROFILE
// ============================================================================

void establecerValoresDeFabrica() {
  memset(&user, 0, sizeof(user));
  strncpy(user.id, "MONITOR_TEST_001", sizeof(user.id) - 1);
  strncpy(user.email, "monitor@nuup.com", sizeof(user.email) - 1);
  strncpy(user.nombre, "Monitor Principal", sizeof(user.nombre) - 1);
  strncpy(user.telefono, "5512345678", sizeof(user.telefono) - 1);
  strncpy(user.password, "nuup2024", sizeof(user.password) - 1);
  user.registrado = false;
}

void guardarUserEnEEPROM() {
  EEPROM.put(USER_ID_ADDR, user.id);
  EEPROM.put(USER_EMAIL_ADDR, user.email);
  EEPROM.put(USER_NAME_ADDR, user.nombre);
  EEPROM.put(USER_PHONE_ADDR, user.telefono);
  EEPROM.put(USER_PASS_ADDR, user.password);
  EEPROM.write(USER_FLAG_ADDR, user.registrado ? 1 : 0);
  EEPROM.commit();
}

void leerUserDeEEPROM() {
  EEPROM.get(USER_ID_ADDR, user.id);
  EEPROM.get(USER_EMAIL_ADDR, user.email);
  EEPROM.get(USER_NAME_ADDR, user.nombre);
  EEPROM.get(USER_PHONE_ADDR, user.telefono);
  EEPROM.get(USER_PASS_ADDR, user.password);
  user.registrado = EEPROM.read(USER_FLAG_ADDR) == 1;

  if (strlen(user.id) == 0) {
    establecerValoresDeFabrica();
    guardarUserEnEEPROM();
  }
}

// ============================================================================
// FUNCIONES - REDES WIFI
// ============================================================================

void inicializarRedes() {
  for (int i = 0; i < MAX_NETWORKS; i++) {
    savedNetworks[i].ssid[0] = '\0';
    savedNetworks[i].password[0] = '\0';
    savedNetworks[i].active = false;
  }
}

void guardarRedesEnEEPROM() {
  for (int i = 0; i < MAX_NETWORKS; i++) {
    int addr = NETWORKS_ADDR + (i * (SSID_LEN + PASS_LEN + 1));
    EEPROM.put(addr, savedNetworks[i]);
  }
  EEPROM.commit();
}

void leerRedesDeEEPROM() {
  for (int i = 0; i < MAX_NETWORKS; i++) {
    int addr = NETWORKS_ADDR + (i * (SSID_LEN + PASS_LEN + 1));
    EEPROM.get(addr, savedNetworks[i]);
  }
}

String escanearRedes() {
  if (millis() - lastScan < SCAN_INTERVAL &&
      scannedNetworksCache.length() > 0) {
    return scannedNetworksCache;
  }

  int n = WiFi.scanNetworks();
  String result = "[";

  for (int i = 0; i < n && i < 20; i++) {
    if (i > 0)
      result += ",";
    result += "{";
    result += "\"ssid\":\"" + WiFi.SSID(i) + "\",";
    result += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
    result += "\"enc\":" + String(WiFi.encryptionType(i));
    result += "}";
  }

  result += "]";
  scannedNetworksCache = result;
  lastScan = millis();

  WiFi.scanDelete();
  return result;
}

// ============================================================================
// FUNCIONES - DISPOSITIVOS SIMULADOS
// ============================================================================

void inicializarDispositivosSimulados() {
  deviceCount = 5; // 5 dispositivos de prueba

  // Dispositivo 1
  strncpy(mockDevices[0].mac, "AA:BB:CC:DD:EE:01", 17);
  strncpy(mockDevices[0].nombre, "Tinaco Principal", 31);
  mockDevices[0].litros = 850;
  mockDevices[0].porcentaje = 77;
  mockDevices[0].voltaje = 3.7;
  mockDevices[0].activo = true;

  // Dispositivo 2
  strncpy(mockDevices[1].mac, "AA:BB:CC:DD:EE:02", 17);
  strncpy(mockDevices[1].nombre, "Cisterna Norte", 31);
  mockDevices[1].litros = 1200;
  mockDevices[1].porcentaje = 85;
  mockDevices[1].voltaje = 3.8;
  mockDevices[1].activo = true;

  // Dispositivo 3
  strncpy(mockDevices[2].mac, "AA:BB:CC:DD:EE:03", 17);
  strncpy(mockDevices[2].nombre, "Tanque Azotea", 31);
  mockDevices[2].litros = 450;
  mockDevices[2].porcentaje = 42;
  mockDevices[2].voltaje = 3.5;
  mockDevices[2].activo = true;

  // Dispositivo 4
  strncpy(mockDevices[3].mac, "AA:BB:CC:DD:EE:04", 17);
  strncpy(mockDevices[3].nombre, "Tanque Jardín", 31);
  mockDevices[3].litros = 200;
  mockDevices[3].porcentaje = 25;
  mockDevices[3].voltaje = 3.3;
  mockDevices[3].activo = true;

  // Dispositivo 5
  strncpy(mockDevices[4].mac, "AA:BB:CC:DD:EE:05", 17);
  strncpy(mockDevices[4].nombre, "Cisterna Sur", 31);
  mockDevices[4].litros = 1800;
  mockDevices[4].porcentaje = 95;
  mockDevices[4].voltaje = 3.9;
  mockDevices[4].activo = true;
}

bool eliminarDispositivo(String mac) {
  for (int i = 0; i < deviceCount; i++) {
    if (String(mockDevices[i].mac) == mac) {
      // Mover todos los dispositivos siguientes una posición atrás
      for (int j = i; j < deviceCount - 1; j++) {
        mockDevices[j] = mockDevices[j + 1];
      }
      deviceCount--;
      Serial.println("✅ Dispositivo eliminado: " + mac);
      return true;
    }
  }
  return false;
}

// ============================================================================
// PORTAL WEB - HTML COMPLETO
// ============================================================================

void mostrarPaginaConfig() {
  // Escanear redes WiFi
  String redes = escanearRedes();

  // Construir lista de redes guardadas
  String redesGuardadas = "";
  for (int i = 0; i < MAX_NETWORKS; i++) {
    if (strlen(savedNetworks[i].ssid) > 0) {
      redesGuardadas += "<div class='network-item'>";
      redesGuardadas += "<span>📶 " + String(savedNetworks[i].ssid) + "</span>";
      redesGuardadas += "<button class='btn-small' onclick='editarRed(" +
                        String(i) + ")'>✏️</button>";
      redesGuardadas +=
          "<button class='btn-small btn-danger' onclick='borrarRed(" +
          String(i) + ")'>🗑️</button>";
      redesGuardadas += "</div>";
    }
  }

  if (redesGuardadas.length() == 0) {
    redesGuardadas =
        "<p style='color:#999;text-align:center'>No hay redes guardadas</p>";
  }

  // Construir lista de dispositivos
  String listaDispositivos = "";
  for (int i = 0; i < deviceCount; i++) {
    String statusColor = mockDevices[i].activo ? "#4CAF50" : "#999";
    String batteryIcon = mockDevices[i].voltaje > 3.6 ? "🔋" : "🪫";

    listaDispositivos += "<div class='device-item'>";
    listaDispositivos += "<div class='device-header'>";
    listaDispositivos += "<div>";
    listaDispositivos +=
        "<div class='device-name'>" + String(mockDevices[i].nombre) + "</div>";
    listaDispositivos +=
        "<div class='device-mac'>" + String(mockDevices[i].mac) + "</div>";
    listaDispositivos += "</div>";
    listaDispositivos +=
        "<div class='device-status' style='color:" + statusColor + "'>●</div>";
    listaDispositivos += "</div>";
    listaDispositivos += "<div class='device-stats'>";
    listaDispositivos += "<div>💧 " + String(mockDevices[i].litros) + "L (" +
                         String(mockDevices[i].porcentaje) + "%)</div>";
    listaDispositivos += "<div>" + batteryIcon + " " +
                         String(mockDevices[i].voltaje, 1) + "V</div>";
    listaDispositivos += "</div>";
    listaDispositivos +=
        "<button class='btn btn-danger' onclick='bajaDispositivo(\"" +
        String(mockDevices[i].mac) + "\")'>❌ Dar de Baja</button>";
    listaDispositivos += "</div>";
  }

  if (listaDispositivos.length() == 0) {
    listaDispositivos = "<p style='color:#999;text-align:center'>No hay "
                        "dispositivos registrados</p>";
  }

  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Monitor01 - Portal Completo</title>
    <style>
        * { margin:0; padding:0; box-sizing:border-box; }
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            padding: 20px;
        }
        .container {
            background: white;
            border-radius: 20px;
            box-shadow: 0 20px 60px rgba(0,0,0,0.3);
            max-width: 800px;
            margin: 0 auto;
            padding: 30px;
        }
        .header {
            text-align: center;
            margin-bottom: 30px;
        }
        .logo { font-size: 64px; margin-bottom: 10px; }
        h1 { color: #333; font-size: 28px; margin-bottom: 5px; }
        .subtitle { color: #666; font-size: 14px; }
        .info-box {
            background: #f0f4ff;
            border-left: 4px solid #667eea;
            padding: 15px;
            margin-bottom: 20px;
            border-radius: 5px;
        }
        .info-item {
            display: flex;
            justify-content: space-between;
            margin: 6px 0;
            font-size: 13px;
        }
        .section-title {
            color: #667eea;
            font-size: 18px;
            font-weight: 600;
            margin: 25px 0 15px 0;
            padding-bottom: 10px;
            border-bottom: 2px solid #e0e0e0;
        }
        .form-group {
            margin-bottom: 15px;
        }
        label {
            display: block;
            color: #444;
            font-weight: 600;
            margin-bottom: 6px;
            font-size: 14px;
        }
        input, select {
            width: 100%;
            padding: 10px 12px;
            border: 2px solid #e0e0e0;
            border-radius: 6px;
            font-size: 14px;
            transition: all 0.3s;
            background: #fafafa;
        }
        input:focus, select:focus {
            outline: none;
            border-color: #667eea;
            background: white;
            box-shadow: 0 0 0 3px rgba(102, 126, 234, 0.1);
        }
        .btn {
            width: 100%;
            padding: 12px;
            border: none;
            border-radius: 6px;
            font-size: 14px;
            font-weight: 600;
            cursor: pointer;
            transition: all 0.3s;
            margin-bottom: 8px;
        }
        .btn-primary {
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
        }
        .btn-primary:hover {
            transform: translateY(-2px);
            box-shadow: 0 8px 15px rgba(102, 126, 234, 0.3);
        }
        .btn-secondary {
            background: #f0f0f0;
            color: #666;
        }
        .btn-danger {
            background: #ff4757;
            color: white;
            font-size: 13px;
            padding: 8px;
        }
        .btn-small {
            padding: 6px 12px;
            font-size: 12px;
            border-radius: 4px;
            border: none;
            cursor: pointer;
            margin-left: 5px;
        }
        .network-item {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 12px;
            background: #f5f5f5;
            border-radius: 6px;
            margin-bottom: 8px;
        }
        .device-item {
            background: #f8f9fa;
            border-radius: 8px;
            padding: 15px;
            margin-bottom: 10px;
            border-left: 4px solid #667eea;
        }
        .device-header {
            display: flex;
            justify-content: space-between;
            align-items: flex-start;
            margin-bottom: 10px;
        }
        .device-name {
            font-weight: 600;
            font-size: 16px;
            color: #333;
        }
        .device-mac {
            font-size: 12px;
            color: #999;
            font-family: monospace;
        }
        .device-status {
            font-size: 24px;
        }
        .device-stats {
            display: flex;
            gap: 15px;
            font-size: 13px;
            color: #666;
            margin-bottom: 10px;
        }
        .checkbox-group {
            display: flex;
            align-items: center;
            gap: 10px;
            margin: 12px 0;
            padding: 10px;
            background: #fff3cd;
            border-radius: 6px;
        }
        .checkbox-group input { width: auto; height: 18px; }
        .checkbox-group label { margin: 0; color: #856404; cursor: pointer; }
        .grid { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
        @media (max-width: 600px) {
            .grid { grid-template-columns: 1fr; }
        }
        .note {
            background: #e7f3ff;
            border-left: 4px solid #2196F3;
            padding: 10px;
            margin: 12px 0;
            border-radius: 4px;
            font-size: 12px;
            color: #0c5460;
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <div class="logo">🏠</div>
            <h1>Monitor01 - Portal Completo</h1>
            <p class="subtitle">Gestión Central de Dispositivos NUUP</p>
        </div>
        
        <div class="info-box">
            <div class="info-item">
                <span><strong>MAC Monitor:</strong></span>
                <span style="font-family:monospace">)rawliteral" +
                macAddress + R"rawliteral(</span>
            </div>
            <div class="info-item">
                <span><strong>Red AP:</strong></span>
                <span>)rawliteral" +
                String(ssidAP) + R"rawliteral(</span>
            </div>
            <div class="info-item">
                <span><strong>Dispositivos:</strong></span>
                <span>)rawliteral" +
                String(deviceCount) + R"rawliteral( / 50</span>
            </div>
        </div>
        
        <!-- CONFIGURACIÓN DE USUARIO -->
        <div class="section-title">👤 Perfil de Usuario</div>
        <form action="/guardar_user" method="post">
            <div class="grid">
                <div class="form-group">
                    <label>🆔 User ID</label>
                    <input type="text" name="userid" value=")rawliteral" +
                String(user.id) + R"rawliteral(" required>
                </div>
                <div class="form-group">
                    <label>📧 Email</label>
                    <input type="email" name="email" value=")rawliteral" +
                String(user.email) + R"rawliteral(" required>
                </div>
            </div>
            <div class="grid">
                <div class="form-group">
                    <label>👨 Nombre</label>
                    <input type="text" name="nombre" value=")rawliteral" +
                String(user.nombre) + R"rawliteral(" required>
                </div>
                <div class="form-group">
                    <label>📞 Teléfono</label>
                    <input type="tel" name="telefono" value=")rawliteral" +
                String(user.telefono) + R"rawliteral(">
                </div>
            </div>
            <div class="form-group">
                <label>🔐 Password MQTT</label>
                <input type="password" name="password" value=")rawliteral" +
                String(user.password) + R"rawliteral(">
            </div>
            <div class="checkbox-group">
                <input type="checkbox" id="registrado" name="registrado" value="1" )rawliteral" +
                String(user.registrado ? "checked" : "") + R"rawliteral(>
                <label for="registrado">Ya estoy registrado en el sistema</label>
            </div>
            <button type="submit" class="btn btn-primary">💾 Guardar Perfil</button>
        </form>
        
        <!-- REDES WIFI -->
        <div class="section-title">📶 Redes WiFi Guardadas</div>
        <div id="redesGuardadas">)rawliteral" +
                redesGuardadas + R"rawliteral(</div>
        <form action="/guardar_red" method="post">
            <div class="form-group">
                <label>Seleccionar Red Cercana</label>
                <select id="redSelect" onchange="usarRed()">
                    <option value="">-- Escanear Redes --</option>
                </select>
            </div>
            <div class="grid">
                <div class="form-group">
                    <label>SSID</label>
                    <input type="text" id="ssid" name="ssid" required>
                </div>
                <div class="form-group">
                    <label>Password</label>
                    <input type="password" name="password" required>
                </div>
            </div>
            <button type="submit" class="btn btn-primary">➕ Guardar Red</button>
        </form>
        
        <!-- DISPOSITIVOS REGISTRADOS -->
        <div class="section-title">📡 Dispositivos NUUP01 Registrados</div>
        <div class="note">
            ℹ️ Estos son dispositivos de prueba simulados. En producción, se sincronizarán vía LoRa/MQTT.
        </div>
        <div id="listaDispositivos">)rawliteral" +
                listaDispositivos + R"rawliteral(</div>
        
        <!-- ACCIONES DEL SISTEMA -->
        <div class="section-title">🔧 Acciones del Sistema</div>
        <button onclick="location.href='/reiniciar'" class="btn btn-secondary">🔄 Reiniciar Monitor</button>
        <button onclick="if(confirm('¿Dar de baja este monitor?'))location.href='/baja_monitor'" class="btn btn-danger">⚠️ Dar de Baja Monitor</button>
        <button onclick="if(confirm('¿Borrar TODO?'))location.href='/fabrica'" class="btn btn-danger">🗑️ Reset de Fábrica</button>
    </div>
    
    <script>
        // Escanear redes WiFi
        fetch('/scan_wifi').then(r=>r.json()).then(data=>{
            let sel = document.getElementById('redSelect');
            data.forEach(net=>{
                let opt = document.createElement('option');
                opt.value = net.ssid;
                opt.text = net.ssid + ' (' + net.rssi + ' dBm)';
                sel.add(opt);
            });
        });
        
        function usarRed() {
            document.getElementById('ssid').value = document.getElementById('redSelect').value;
        }
        
        function editarRed(idx) {
            alert('Editar red ' + idx + ' (Demo)');
        }
        
        function borrarRed(idx) {
            if(confirm('¿Borrar esta red?')) {
                location.href='/borrar_red?idx='+idx;
            }
        }
        
        function bajaDispositivo(mac) {
            if(confirm('¿Dar de baja: ' + mac + '?')) {
                location.href='/baja_dispositivo?mac='+mac;
            }
        }
    </script>
</body>
</html>
)rawliteral";

  server.send(200, "text/html", html);
}

// ============================================================================
// HANDLERS
// ============================================================================

void handleGuardarUser() {
  if (server.hasArg("userid")) {
    strncpy(user.id, server.arg("userid").c_str(), sizeof(user.id) - 1);
    strncpy(user.email, server.arg("email").c_str(), sizeof(user.email) - 1);
    strncpy(user.nombre, server.arg("nombre").c_str(), sizeof(user.nombre) - 1);
    strncpy(user.telefono, server.arg("telefono").c_str(),
            sizeof(user.telefono) - 1);
    strncpy(user.password, server.arg("password").c_str(),
            sizeof(user.password) - 1);
    user.registrado = server.hasArg("registrado");

    guardarUserEnEEPROM();

    server.send(200, "text/html",
                "<html><body "
                "style='background:#667eea;color:white;text-align:center;"
                "padding:50px;font-family:sans-serif'>"
                "<h1>✅ Perfil Guardado</h1><p>Redirigiendo...</p>"
                "<script>setTimeout(()=>location.href='/',2000)</script></"
                "body></html>");
  }
}

void handleGuardarRed() {
  if (server.hasArg("ssid")) {
    for (int i = 0; i < MAX_NETWORKS; i++) {
      if (strlen(savedNetworks[i].ssid) == 0) {
        strncpy(savedNetworks[i].ssid, server.arg("ssid").c_str(),
                SSID_LEN - 1);
        strncpy(savedNetworks[i].password, server.arg("password").c_str(),
                PASS_LEN - 1);
        savedNetworks[i].active = true;
        guardarRedesEnEEPROM();
        break;
      }
    }

    server.send(200, "text/html",
                "<html><body "
                "style='background:#667eea;color:white;text-align:center;"
                "padding:50px;font-family:sans-serif'>"
                "<h1>✅ Red "
                "Guardada</h1><script>setTimeout(()=>location.href='/',2000)</"
                "script></body></html>");
  }
}

void handleBorrarRed() {
  if (server.hasArg("idx")) {
    int idx = server.arg("idx").toInt();
    if (idx >= 0 && idx < MAX_NETWORKS) {
      savedNetworks[idx].ssid[0] = '\0';
      savedNetworks[idx].password[0] = '\0';
      savedNetworks[idx].active = false;
      guardarRedesEnEEPROM();
    }

    server.send(
        200, "text/html",
        "<html><body "
        "style='background:#667eea;color:white;text-align:center;padding:50px'>"
        "<h1>🗑️ Red "
        "Eliminada</h1><script>setTimeout(()=>location.href='/',2000)</"
        "script></body></html>");
  }
}

void handleBajaDispositivo() {
  if (server.hasArg("mac")) {
    String mac = server.arg("mac");
    bool success = eliminarDispositivo(mac);

    server.send(
        200, "text/html",
        "<html><body "
        "style='background:#ff4757;color:white;text-align:center;padding:50px'>"
        "<h1>" +
            String(success ? "✅ Dispositivo Dado de Baja" : "❌ Error") +
            "</h1>"
            "<script>setTimeout(()=>location.href='/',2000)</script></body></"
            "html>");
  }
}

void handleScanWiFi() {
  String json = escanearRedes();
  server.send(200, "application/json", json);
}

void handleBajaMonitor() {
  server.send(
      200, "text/html",
      "<html><body "
      "style='background:#ff4757;color:white;text-align:center;padding:50px;"
      "font-family:sans-serif'>"
      "<h1>⚠️ Baja de Monitor</h1>"
      "<p>En producción, esto enviaría solicitud MQTT y limpiaría EEPROM</p>"
      "<p>Reiniciando en 5 segundos...</p>"
      "<script>setTimeout(()=>location.href='/fabrica',5000)</script></body></"
      "html>");
}

void handleReinicio() {
  server.send(
      200, "text/html",
      "<html><body "
      "style='background:#667eea;color:white;text-align:center;padding:50px'>"
      "<h1>🔄 Reiniciando...</h1></body></html>");
  delay(1000);
  ESP.restart();
}

void handleFactoryReset() {
  for (int i = 0; i < EEPROM_SIZE; i++)
    EEPROM.write(i, 0);
  EEPROM.commit();

  server.send(
      200, "text/html",
      "<html><body "
      "style='background:#ff4757;color:white;text-align:center;padding:50px'>"
      "<h1>🗑️ Reset de Fábrica Completado</h1></body></html>");

  delay(2000);
  ESP.restart();
}

// ============================================================================
// SETUP
// ============================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║ MONITOR01 - TEST PORTAL COMPLETO     ║");
  Serial.println("║ Versión: Full Features (Sin Hardware) ║");
  Serial.println("╚════════════════════════════════════════╝\n");

  macAddress = WiFi.macAddress();
  Serial.printf("📱 MAC: %s\n", macAddress.c_str());

  // EEPROM
  EEPROM.begin(EEPROM_SIZE);
  leerUserDeEEPROM();
  leerRedesDeEEPROM();

  Serial.printf("👤 User: %s (%s)\n", user.nombre, user.email);

  // Inicializar dispositivos simulados
  inicializarDispositivosSimulados();
  Serial.printf("📡 Dispositivos simulados: %d\n", deviceCount);

  // WiFi AP
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssidAP, passwordAP);
  IPAddress IP = WiFi.softAPIP();

  Serial.println("\n✅ Modo AP iniciado:");
  Serial.printf("   SSID: %s\n", ssidAP);
  Serial.printf("   IP: %s\n", IP.toString().c_str());

  // DNS para captive portal
  dnsServer.start(53, "*", IP);

  // Rutas del servidor
  server.on("/", HTTP_GET, mostrarPaginaConfig);
  server.on("/guardar_user", HTTP_POST, handleGuardarUser);
  server.on("/guardar_red", HTTP_POST, handleGuardarRed);
  server.on("/borrar_red", HTTP_GET, handleBorrarRed);
  server.on("/baja_dispositivo", HTTP_GET, handleBajaDispositivo);
  server.on("/scan_wifi", HTTP_GET, handleScanWiFi);
  server.on("/baja_monitor", HTTP_GET, handleBajaMonitor);
  server.on("/reiniciar", HTTP_GET, handleReinicio);
  server.on("/fabrica", HTTP_GET, handleFactoryReset);

  // Captive portal redirects
  server.on("/generate_204", HTTP_GET, []() {
    server.sendHeader("Location", "http://192.168.4.1/");
    server.send(302);
  });
  server.on("/hotspot-detect.html", HTTP_GET, []() {
    server.sendHeader("Location", "http://192.168.4.1/");
    server.send(302);
  });
  server.onNotFound([]() {
    server.sendHeader("Location", "http://192.168.4.1/");
    server.send(302);
  });

  server.begin();
  Serial.println("✅ Servidor Web iniciado\n");

  Serial.println("╔════════════════════════════════════════╗");
  Serial.println("║          PORTAL LISTO                 ║");
  Serial.println("╠════════════════════════════════════════╣");
  Serial.println("║  1. Conecta: " + String(ssidAP));
  Serial.println("║  2. Abre navegador (auto-redirige)    ║");
  Serial.println("║  3. O ve a: http://192.168.4.1        ║");
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
