#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

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
unsigned long tiempoInicioEmparejamiento = 0;
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




// Variables para animación de WiFi
bool animandoWifi = false;
int frameWifi = 0;
unsigned long ultimoCambioWifi = 0;
const unsigned long INTERVALO_WIFI = 300; // ms entre frames


// DECLARACIONES DE FUNCIONES
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


// También agregar estas declaraciones al principio
void dibujarWifiAnimado(int centroX, int centroY, int frame);
void mostrarConexionWifi();
void iniciarAnimacionWifi();
void detenerAnimacionWifi();
void conectarWifi();


void setup() {
  Serial.begin(115200);
  
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("Fallo inicializacion OLED");
    while(true);
  }
  
  Serial.println("OLED inicializado correctamente");
  display.setTextColor(SSD1306_WHITE);

// probar el emparejamiento falta la funcion de finalizar emparejamoiento
//  emparejarNuevoDispositivo();

  
// Probar animación WiFi por 10 segundos
  conectarWifi();
  
  // Para detener después de 10 segundos (en producción lo controlarías con el estado real del WiFi)
  // detenerAnimacionWifi();


}


// ============================================================================
// DESARROLLO DE FUNCIONES (después del loop)
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


// Modificar el loop principal para manejar ambas animaciones
void loop() {
  if (emparejando) {
    // Animación de emparejamiento de dispositivo
    if (millis() - ultimoCambioAnimacion >= INTERVALO_ANIMACION) {
      frameAnimacion++;
      ultimoCambioAnimacion = millis();
    }
    mostrarEmparejamiento();
    
    if (millis() - tiempoInicioEmparejamiento > 10000) {
      detenerEmparejamiento();
    }
    
  } else if (animandoWifi) {
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
    
  } else {
    // Código normal de la pantalla
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
  tiempoInicioEmparejamiento = millis();
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