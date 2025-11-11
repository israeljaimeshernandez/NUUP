#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Configuración LCD I2C
// Dirección I2C común: 0x27 o 0x3F
// Tamaño: 16 columnas x 2 filas
LiquidCrystal_I2C lcd(0x27, 16, 2);  // (dirección, columnas, filas)

// Pines I2C
#define I2C_SDA 21
#define I2C_SCL 22

// LED interno (si existe)
#define LED_BUILTIN 2

void setup() {
  // Inicializar serial
  Serial.begin(115200);
  
  // Inicializar LED
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  
  // Inicializar I2C con pines específicos
  Wire.begin(I2C_SDA, I2C_SCL);
  
  // Inicializar LCD
  lcd.init();
  lcd.backlight();  // Encender retroiluminación
  
  // Mensaje inicial
  lcd.setCursor(0, 0);
  lcd.print("ESP32 + LCD I2C");
  lcd.setCursor(0, 1);
  lcd.print("Inicializado OK!");
  
  Serial.println("✅ Sistema inicializado");
  Serial.println("📟 LCD I2C - 16x2");
  Serial.print("🔍 Buscando LCD en I2C...");
  
  delay(2000);
  
  // Limpiar pantalla después de mensaje inicial
  lcd.clear();
}

void loop() {
  static unsigned long lastUpdate = 0;
  static int counter = 0;
  
  // Actualizar cada 2 segundos
  if (millis() - lastUpdate >= 2000) {
    lastUpdate = millis();
    
    // Alternar LED
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    
    // Mostrar información en LCD
    lcd.setCursor(0, 0);
    lcd.print("Contador: ");
    lcd.print(counter);
    
    lcd.setCursor(0, 1);
    lcd.print("Millis: ");
    lcd.print(millis() / 1000);
    lcd.print("s  ");
    
    // Mostrar en serial también
    Serial.print("🔢 Contador: ");
    Serial.println(counter);
    
    counter++;
  }
}