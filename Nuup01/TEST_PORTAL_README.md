# 🧪 NUUP01 - Test Portal Cautivo

## 📋 Descripción

Versión **minimal** de NUUP01 para probar **solo el portal cautivo** sin necesidad de hardware conectado.

**Sin:**
- ❌ LoRa
- ❌ BLE
- ❌ Sensor ultrasónico
- ❌ Deep sleep
- ❌ Librerías externas

**Con:**
- ✅ WiFi AP (Access Point)
- ✅ Portal cautivo (auto-redirige)
- ✅ Servidor web con UI moderna
- ✅ EEPROM persistente
- ✅ LEDs de estado (opcionales)

---

## 🚀 Cómo Usar

### **⚠️ IMPORTANTE: Debes especificar el entorno**

Siempre usa `-e` para indicar qué versión compilar:

### **Opción 1: Compilar versión TEST (portal only)** ✅ Recomendado para probar

```bash
cd Nuup01

# Compilar
pio run -e test_ap

# Subir al ESP32
pio run -e test_ap -t upload

# Monitor serial
pio device monitor
```

### **Opción 2: Compilar versión COMPLETA (con hardware)**

```bash
cd Nuup01

# Compilar
pio run -e nodemcu-32s

# Subir al ESP32
pio run -e nodemcu-32s -t upload

# Monitor serial
pio device monitor
```

### **❌ ERROR COMÚN:**
Si olvidas `-e test_ap`, PlatformIO usará el entorno default y puede dar errores de compilación.

**SIEMPRE especifica el entorno:**
- `-e test_ap` → Portal only
- `-e nodemcu-32s` → Versión completa

---

## 📁 Archivos en el Proyecto

```
Nuup01/
├── src/
│   ├── main.cpp ✨                    # Versión optimizada COMPLETA (con hardware)
│   ├── main_test_ap.cpp ✨            # Versión TEST (solo portal AP)
│   └── main_backup_original.cpp       # Backup del código original
│
└── platformio.ini                     # Configuración con 2 entornos
```

---

## 🌐 Probar el Portal Cautivo

### 1️⃣ **Sube el código TEST al ESP32**
```bash
pio run -e test_ap -t upload
pio device monitor
```

### 2️⃣ **Conecta tu smartphone/PC al WiFi**
- **SSID:** `NUUP01_Test`
- **Password:** (sin contraseña)

### 3️⃣ **El portal se abre automáticamente**
- Android/iOS detectan el captive portal y lo abren automáticamente
- Si no abre, ve manualmente a: `http://192.168.4.1`

### 4️⃣ **Configura el dispositivo**
- Nombre del dispositivo
- Altura total (cm)
- Capacidad total (litros)

### 5️⃣ **Guarda y verifica**
- Los datos se guardan en EEPROM
- El ESP32 se reinicia automáticamente
- Reconécta al WiFi y verifica que los datos persisten

---

## 🎨 Features del Portal

### **Diseño Moderno:**
- ✅ Gradientes vibrantes
- ✅ Animaciones suaves
- ✅ Responsive (móvil/desktop)
- ✅ Iconos emoji
- ✅ Auto-redireccionamiento (captive portal)

### **Funciones:**
1. **Configuración de dispositivo**
   - Nombre (máx 20 caracteres)
   - Altura (10-500 cm)
   - Litros (10-50000 L)

2. **Reinicio ESP32**
   - Botón de reinicio seguro

3. **Reset de Fábrica**
   - Borra toda la EEPROM
   - Restaura valores por defecto
   - Requiere confirmación

---

## 🔧 Configuración Opcional

### **Cambiar SSID del AP:**
Edita en `main_test_ap.cpp`:
```cpp
const char* ssidAP = "NUUP01_Test";  // Cambia aquí
const char* passwordAP = "";         // Agrega password si quieres
```

### **Habilitar/Deshabilitar LEDs:**
Si **NO tienes LEDs** conectados, comenta estas líneas:
```cpp
// #define LED_VERDE_PIN 27
// #define LED_ROJO_PIN 26
```

Si **SÍ tienes LEDs**, déjalas como están:
- **Verde (GPIO 27):** Indica que el portal está activo
- **Rojo (GPIO 26):** No usado en versión test

---

## 📊 Salida Serial Esperada

```
╔═══════════════════════════════════════╗
║   NUUP01 - TEST PORTAL CAUTIVO      ║
║   Versión: Solo AP (Sin Hardware)    ║
╚═══════════════════════════════════════╝

✅ LEDs inicializados
📱 MAC: A1B2C3D4E5F6
⚠️ EEPROM vacía, usando valores de fábrica
💾 Datos guardados en EEPROM

✅ Modo AP iniciado:
   SSID: NUUP01_Test
   IP: 192.168.4.1
   Password: (sin contraseña)
✅ DNS Server iniciado (captive portal)
✅ Servidor Web iniciado

╔═══════════════════════════════════════╗
║         PORTAL LISTO                 ║
╠═══════════════════════════════════════╣
║  1. Conecta al WiFi: NUUP01_Test     ║
║  2. Abre navegador (auto-redirige)   ║
║  3. O ve a: http://192.168.4.1       ║
╚═══════════════════════════════════════╝
```

---

## ⚠️ Notas Importantes

### **Errores de Lint (IGNORAR)**
Los errores que ves en el editor son **normales** y se resuelven al compilar:
- `'Arduino.h' file not found` → OK (librería ESP32)
- `Unknown type name 'String'` → OK (Arduino String)
- `Use of undeclared identifier 'WiFi'` → OK (lib WiFi)

**El código compilará correctamente** con PlatformIO.

### **Memoria Flash Usada:**
- Versión TEST: ~350 KB (muy ligera)
- Versión COMPLETA: ~450 KB

### **Memoria RAM Libre:**
- ~200 KB disponibles después de inicio

---

## 🔄 Cambiar entre Versiones

### **Método 1: Especificar entorno al compilar**
```bash
# Versión TEST (portal only)
pio run -e test_ap -t upload

# Versión COMPLETA (con hardware)
pio run -e nodemcu-32s -t upload
```

### **Método 2: Cambiar default en platformio.ini**
Agrega al inicio del archivo:
```ini
[platformio]
default_envs = test_ap  ; o nodemcu-32s
```

---

## 🧪 Testing Checklist

- [ ] Compilar sin errores
- [ ] Subir al ESP32
- [ ] Ver salida serial correcta
- [ ] Conectar al WiFi `NUUP01_Test`
- [ ] Portal se abre automáticamente
- [ ] Cambiar nombre/altura/litros
- [ ] Guardar configuración
- [ ] ESP32 se reinicia
- [ ] Reconectar al WiFi
- [ ] Datos persisten (se muestran en el portal)
- [ ] Probar botón "Reiniciar"
- [ ] Probar botón "Reset de Fábrica"

---

## 💡 Próximos Pasos

Una vez que el portal funcione correctamente:

1. **Agregar hardware** (sensor, LoRa, BLE)
2. **Cambiar a versión completa:**
   ```bash
   pio run -e nodemcu-32s -t upload
   ```
3. **Testing completo** con todos los componentes

---

## 📞 Troubleshooting

### **El portal no abre automáticamente:**
- Ve manualmente a: `http://192.168.4.1`
- Algunos dispositivos bloquean captive portals en redes sin internet

### **No puedo conectar al WiFi:**
- Verifica que el SSID sea `NUUP01_Test`
- Verifica que no haya password configurado
- Reinicia el ESP32

### **Los datos no se guardan:**
- Verifica la salida serial para mensajes de error
- La EEPROM podría estar corrupta (usa Reset de Fábrica)

### **ESP32 se reinicia constantemente:**
- Verifica la alimentación (USB estable, 5V/500mA mínimo)
- Verifica que no haya errores en el monitor serial

---

**¡Listo para probar el portal cautivo!** 🚀
