#define USER_SETUP_INFO "ILI9341V_ESP32"

#define ILI9341_DRIVER       // Usar controlador específico
#define TFT_WIDTH  240       // Ancho real de tu pantalla
#define TFT_HEIGHT 320       // Alto real (ajusta si es diferente)

// Pines ESP32 (según tu ficha técnica)
#define TFT_CS   15    // IO15 (Chip Select)
#define TFT_DC   2     // IO2 (Data/Command)
#define TFT_RST  EN    // Reset compartido (o usa -1 si no aplica)
#define TFT_MOSI 13    // IO13 (MOSI)
#define TFT_SCLK 14    // IO14 (SCK)
#define TFT_MISO 12    // IO12 (MISO, opcional si no se usa)

#define TFT_BL   21    // IO21 (Backlight)
#define TFT_BACKLIGHT_ON HIGH  // Nivel para encender backlight

// Optimización para ILI9341V
#define ILI9341_COLORINVERSION 0  // Desactiva inversión de color (puede variar)
#define SPI_FREQUENCY  27000000    // Frecuencia máxima para ILI9341V
#define SPI_READ_FREQUENCY 20000000 // Frecuencia para lecturas (si se usa MISO)

// Rotación inicial (prueba cambiarla si el problema persiste)
#define TFT_ROTATION 0  // 0, 1, 2 o 3