//Pantalla
#include <LovyanGFX.hpp>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>
#include <LoRa.h>

// Pines LoRa
#define LORA_SS 27
//#define LORA_RST 35
//#define LORA_DIO0 35  //lo quiero cambiar sospecho que algo esta haciendo interferencia se queda pasmado el LORA despues de un tiempo
                    //voy a probar en el PCB de desarollo y vemos si se cambia

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


// Declaración de funciones
void iniciarLoRaConReintentos();

// ==================== DECLARACIÓN DE FUNCIONES PANTALLA====================
void hardwareReset();
void dibujarIconoWiFi(int x, int y);
void dibujarIconoSensor(int x, int y);
void dibujarBotones();
void verificarToques();
void calibrarSilencioso();
void calibrarBordes();
void procesarMensajeLoRa();

void setup() {

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
//clearEEPROM_WIFFI();  //solo para configuracion inicial
delay(1000);



// 6. Inicialización LoRa con manejo de errores mejorado
iniciarLoRaConReintentos();


Serial.println("Setup completado");
}


void loop() {
 
///dispositivos
  if (LoRa.parsePacket()) {
    String mensaje = LoRa.readString();
    mensaje.trim();
    Serial.println("Recibido mensaje sin BOTON S desde LORA  sensor:  " + mensaje);
delay(100);
  LoRa.beginPacket();
        LoRa.print("Enviando respuesta de monitor a sensor");
         Serial.println("Enviando respuesta de Monitor a sensor...");
        LoRa.endPacket();

  }

//prueba
  LoRa.beginPacket();
        LoRa.print("Enviando prueba");
         Serial.println("Enviando respuesta de Monitor a sensor...");
        LoRa.endPacket();
delay(1000);


// 7. Manejo del botón S para registro de dispositivo o emparejamiento
if (boton_s){
      delay(100);
    Serial.println("Modo recepción activado. Esperando solicitudes ");
        // Solo procesar mensajes (el OK_REG se envía desde procesarMensajeLoRa)
        procesarMensajeLoRa();
        delay(100); // Pequeña pausa para evitar saturación
      }
    



      //8.  Verificaciones de botones en TOUCH
      verificarToques(); //solo programado dos botones 
    static uint32_t lastPrint = 0;
    if(millis() - lastPrint > 1000) {
        Serial.printf("Estado: W=%d S=%d\n", boton_w, boton_s);
        lastPrint = millis();
    }
    
    delay(10);




  }


// Implementación de funciones


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


void procesarMensajeLoRa() {
  if (LoRa.parsePacket()) {
    String mensaje = LoRa.readString();
    mensaje.trim();
    Serial.println("Recibido mensaje LORA desde sensor:  " + mensaje);
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