/*******************************************************************************
 * NUUP01 - TEST PORTAL CAUTIVO (SIN HARDWARE)
 *
 * Versión minimal para probar SOLO el portal cautivo AP
 * Sin: LoRa, BLE, Sensor, Deep Sleep
 *
 * Compatible con ESP32 sin hardware conectado
 ******************************************************************************/

#include <Arduino.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <WebServer.h>
#include <WiFi.h>


// ============================================================================
// CONFIGURACIÓN
// ============================================================================

// WiFi AP
const char *ssidAP = "NUUP01_Test";
const char *passwordAP = ""; // Sin contraseña para fácil acceso

// Pines LED (opcionales - comentar si no tienes LEDs)
#define LED_VERDE_PIN 27
#define LED_ROJO_PIN 26

// EEPROM
#define EEPROM_SIZE 128
#define EEPROM_ADDR_DATOS 1

// ============================================================================
// ESTRUCTURAS
// ============================================================================

struct DispositivoData {
  char mac[18] = "";
  char nombre[21] = "Tinaco Test";
  uint32_t altura = 180;
  uint32_t litros = 1100;
};

// ============================================================================
// VARIABLES GLOBALES
// ============================================================================

DNSServer dnsServer;
WebServer server(80);
DispositivoData dispositivo;
String macAddress = "";
bool configurando = true;

// ============================================================================
// FUNCIONES AUXILIARES
// ============================================================================

void establecerValoresDeFabrica() {
  memset(&dispositivo, 0, sizeof(dispositivo));
  strncpy(dispositivo.nombre, "Tinaco Test", sizeof(dispositivo.nombre) - 1);
  dispositivo.altura = 180;
  dispositivo.litros = 1100;
}

void guardarDatosEnEEPROM() {
  EEPROM.put(EEPROM_ADDR_DATOS, dispositivo);
  EEPROM.commit();
  Serial.println("💾 Datos guardados en EEPROM");
}

void leerDatosDeEEPROM() {
  EEPROM.get(EEPROM_ADDR_DATOS, dispositivo);

  if (strlen(dispositivo.nombre) == 0 || dispositivo.altura == 0 ||
      dispositivo.litros == 0) {
    Serial.println("⚠️ EEPROM vacía, usando valores de fábrica");
    establecerValoresDeFabrica();
    guardarDatosEnEEPROM();
  } else {
    Serial.println("✅ Datos cargados de EEPROM:");
    Serial.printf("   Nombre: %s\n", dispositivo.nombre);
    Serial.printf("   Altura: %d cm\n", dispositivo.altura);
    Serial.printf("   Litros: %d L\n", dispositivo.litros);
  }
}

void limpiarEEPROM() {
  for (int i = 0; i < EEPROM_SIZE; i++) {
    EEPROM.write(i, 0);
  }
  EEPROM.commit();
  Serial.println("🗑️ EEPROM limpiada");
}

// ============================================================================
// PORTAL WEB
// ============================================================================

void mostrarPaginaConfig() {
  String html =
      R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>NUUP01 - Configuración</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }
        
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            display: flex;
            justify-content: center;
            align-items: center;
            padding: 20px;
        }
        
        .container {
            background: white;
            border-radius: 20px;
            box-shadow: 0 20px 60px rgba(0,0,0,0.3);
            max-width: 450px;
            width: 100%;
            padding: 40px 30px;
            animation: slideIn 0.5s ease-out;
        }
        
        @keyframes slideIn {
            from {
                opacity: 0;
                transform: translateY(-30px);
            }
            to {
                opacity: 1;
                transform: translateY(0);
            }
        }
        
        .header {
            text-align: center;
            margin-bottom: 30px;
        }
        
        .logo {
            font-size: 48px;
            margin-bottom: 10px;
        }
        
        h1 {
            color: #333;
            font-size: 24px;
            margin-bottom: 5px;
        }
        
        .subtitle {
            color: #666;
            font-size: 14px;
        }
        
        .info-box {
            background: #f0f4ff;
            border-left: 4px solid #667eea;
            padding: 15px;
            margin-bottom: 25px;
            border-radius: 5px;
        }
        
        .info-item {
            display: flex;
            justify-content: space-between;
            margin: 8px 0;
            font-size: 14px;
        }
        
        .info-label {
            color: #666;
            font-weight: 600;
        }
        
        .info-value {
            color: #333;
            font-family: monospace;
        }
        
        .form-group {
            margin-bottom: 20px;
        }
        
        label {
            display: block;
            color: #444;
            font-weight: 600;
            margin-bottom: 8px;
            font-size: 14px;
        }
        
        input {
            width: 100%;
            padding: 12px 15px;
            border: 2px solid #e0e0e0;
            border-radius: 8px;
            font-size: 16px;
            transition: all 0.3s;
            background: #fafafa;
        }
        
        input:focus {
            outline: none;
            border-color: #667eea;
            background: white;
            box-shadow: 0 0 0 3px rgba(102, 126, 234, 0.1);
        }
        
        .btn {
            width: 100%;
            padding: 14px;
            border: none;
            border-radius: 8px;
            font-size: 16px;
            font-weight: 600;
            cursor: pointer;
            transition: all 0.3s;
            margin-bottom: 10px;
        }
        
        .btn-primary {
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
        }
        
        .btn-primary:hover {
            transform: translateY(-2px);
            box-shadow: 0 10px 20px rgba(102, 126, 234, 0.3);
        }
        
        .btn-primary:active {
            transform: translateY(0);
        }
        
        .btn-secondary {
            background: #f0f0f0;
            color: #666;
        }
        
        .btn-secondary:hover {
            background: #e0e0e0;
        }
        
        .btn-danger {
            background: #ff4757;
            color: white;
            font-size: 14px;
            padding: 10px;
        }
        
        .btn-danger:hover {
            background: #ee5a6f;
        }
        
        .divider {
            margin: 20px 0;
            text-align: center;
            color: #999;
            font-size: 12px;
        }
        
        .success {
            background: #d4edda;
            border-color: #c3e6cb;
            color: #155724;
            padding: 15px;
            border-radius: 8px;
            margin-bottom: 20px;
            text-align: center;
            animation: fadeIn 0.5s;
        }
        
        @keyframes fadeIn {
            from { opacity: 0; }
            to { opacity: 1; }
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <div class="logo">💧</div>
            <h1>NUUP01 Test</h1>
            <p class="subtitle">Portal Cautivo de Configuración</p>
        </div>
        
        <div class="info-box">
            <div class="info-item">
                <span class="info-label">MAC Address:</span>
                <span class="info-value">)rawliteral" +
      macAddress + R"rawliteral(</span>
            </div>
            <div class="info-item">
                <span class="info-label">Red WiFi:</span>
                <span class="info-value">)rawliteral" +
      String(ssidAP) + R"rawliteral(</span>
            </div>
        </div>
        
        <form action="/guardar" method="post">
            <div class="form-group">
                <label>📝 Nombre del Dispositivo</label>
                <input type="text" name="nombre" value=")rawliteral" +
      String(dispositivo.nombre) +
      R"rawliteral(" required maxlength="20" placeholder="Ej: Tinaco Principal">
            </div>
            
            <div class="form-group">
                <label>📏 Altura Total (cm)</label>
                <input type="number" name="altura" value=")rawliteral" +
      String(dispositivo.altura) +
      R"rawliteral(" required min="10" max="500" placeholder="180">
            </div>
            
            <div class="form-group">
                <label>🪣 Capacidad Total (litros)</label>
                <input type="number" name="litros" value=")rawliteral" +
      String(dispositivo.litros) +
      R"rawliteral(" required min="10" max="50000" placeholder="1100">
            </div>
            
            <button type="submit" class="btn btn-primary">💾 Guardar Configuración</button>
        </form>
        
        <div class="divider">━━━━━━━━━━━━━━━━━━━━━━</div>
        
        <button onclick="location.href='/reiniciar'" class="btn btn-secondary">🔄 Reiniciar ESP32</button>
        <button onclick="if(confirm('¿Borrar toda la configuración?'))location.href='/fabrica'" class="btn btn-danger">🗑️ Reset de Fábrica</button>
    </div>
</body>
</html>
)rawliteral";

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

      String html = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Guardado Exitoso</title>
    <style>
        body {
            font-family: 'Segoe UI', sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            display: flex;
            justify-content: center;
            align-items: center;
            color: white;
        }
        .container {
            text-align: center;
            background: rgba(255,255,255,0.1);
            backdrop-filter: blur(10px);
            padding: 50px;
            border-radius: 20px;
            box-shadow: 0 20px 60px rgba(0,0,0,0.3);
        }
        .icon {
            font-size: 80px;
            margin-bottom: 20px;
            animation: bounce 1s infinite;
        }
        @keyframes bounce {
            0%, 100% { transform: translateY(0); }
            50% { transform: translateY(-20px); }
        }
        h1 {
            font-size: 32px;
            margin-bottom: 15px;
        }
        p {
            font-size: 18px;
            opacity: 0.9;
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="icon">✅</div>
        <h1>¡Configuración Guardada!</h1>
        <p>ESP32 se reiniciará en 3 segundos...</p>
    </div>
    <script>
        setTimeout(function() {
            window.location.href = '/reiniciar';
        }, 3000);
    </script>
</body>
</html>
)rawliteral";

      server.send(200, "text/html", html);

#ifdef LED_VERDE_PIN
      // Parpadeo de confirmación
      for (int i = 0; i < 6; i++) {
        digitalWrite(LED_VERDE_PIN, !digitalRead(LED_VERDE_PIN));
        delay(250);
      }
#endif

      Serial.println("\n✅ Configuración guardada:");
      Serial.printf("   Nombre: %s\n", dispositivo.nombre);
      Serial.printf("   Altura: %d cm\n", dispositivo.altura);
      Serial.printf("   Litros: %d L\n", dispositivo.litros);

      delay(3000);
      ESP.restart();
    }
  }
}

void manejarReinicio() {
  server.send(200, "text/html",
              "<html><body "
              "style='background:#667eea;color:white;text-align:center;padding:"
              "50px;font-family:sans-serif'>"
              "<h1>🔄 Reiniciando...</h1></body></html>");
  delay(1000);
  ESP.restart();
}

void manejarRestauracionFabrica() {
  server.send(200, "text/html",
              "<html><body "
              "style='background:#ff4757;color:white;text-align:center;padding:"
              "50px;font-family:sans-serif'>"
              "<h1>🗑️ Reset de Fábrica</h1><p>Borrando "
              "configuración...</p></body></html>");
  delay(1000);

  limpiarEEPROM();
  establecerValoresDeFabrica();
  guardarDatosEnEEPROM();

  delay(2000);
  ESP.restart();
}

// ============================================================================
// SETUP
// ============================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n");
  Serial.println("╔═══════════════════════════════════════╗");
  Serial.println("║   NUUP01 - TEST PORTAL CAUTIVO      ║");
  Serial.println("║   Versión: Solo AP (Sin Hardware)    ║");
  Serial.println("╚═══════════════════════════════════════╝");
  Serial.println();

// Inicializar LEDs (opcional)
#ifdef LED_VERDE_PIN
  pinMode(LED_VERDE_PIN, OUTPUT);
  pinMode(LED_ROJO_PIN, OUTPUT);
  digitalWrite(LED_VERDE_PIN, LOW);
  digitalWrite(LED_ROJO_PIN, LOW);
  Serial.println("✅ LEDs inicializados");
#else
  Serial.println("ℹ️  LEDs no configurados (no hay pines definidos)");
#endif

  // Obtener MAC
  macAddress = WiFi.macAddress();
  macAddress.replace(":", "");
  Serial.printf("📱 MAC: %s\n", macAddress.c_str());

  // Inicializar EEPROM
  EEPROM.begin(EEPROM_SIZE);
  leerDatosDeEEPROM();

  // Configurar WiFi AP
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssidAP, passwordAP);

  IPAddress IP = WiFi.softAPIP();
  Serial.println("\n✅ Modo AP iniciado:");
  Serial.printf("   SSID: %s\n", ssidAP);
  Serial.printf("   IP: %s\n", IP.toString().c_str());
  Serial.println("   Password: (sin contraseña)");

  // Configurar DNS para captive portal
  dnsServer.start(53, "*", IP);
  Serial.println("✅ DNS Server iniciado (captive portal)");

  // Configurar rutas del servidor web
  server.on("/", HTTP_GET, mostrarPaginaConfig);
  server.on("/guardar", HTTP_POST, guardarConfigWeb);
  server.on("/reiniciar", HTTP_GET, manejarReinicio);
  server.on("/fabrica", HTTP_GET, manejarRestauracionFabrica);

  // Rutas para captive portal (Android/iOS detection)
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

  // Redirigir cualquier otra ruta al portal
  server.onNotFound([]() {
    server.sendHeader("Location", "http://192.168.4.1/");
    server.send(302, "text/plain", "");
  });

  server.begin();
  Serial.println("✅ Servidor Web iniciado");

  Serial.println("\n╔═══════════════════════════════════════╗");
  Serial.println("║         PORTAL LISTO                 ║");
  Serial.println("╠═══════════════════════════════════════╣");
  Serial.println("║  1. Conecta al WiFi: " + String(ssidAP));
  Serial.println("║  2. Abre navegador (auto-redirige)   ║");
  Serial.println("║  3. O ve a: http://192.168.4.1       ║");
  Serial.println("╚═══════════════════════════════════════╝\n");

#ifdef LED_VERDE_PIN
  digitalWrite(LED_VERDE_PIN, HIGH); // LED verde fijo = listo
#endif
}

// ============================================================================
// LOOP
// ============================================================================

void loop() {
  // Procesar DNS y servidor web
  dnsServer.processNextRequest();
  server.handleClient();

// LED de actividad (parpadeo cada 2 segundos)
#ifdef LED_VERDE_PIN
  static unsigned long ultimoParpadeo = 0;
  if (millis() - ultimoParpadeo > 2000) {
    digitalWrite(LED_VERDE_PIN, !digitalRead(LED_VERDE_PIN));
    ultimoParpadeo = millis();
  }
#endif

  delay(10); // Pequeño delay para estabilidad
}
