#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>


// --- Pines ---
#define BOTON_PIN 33  
#define LED_PIN 27
#define ADC_PIN 34
// --- LoRa ---
#define LORA_SS 5
#define LORA_RST -1   //14 no definido aunque si lo tengo conectado
#define LORA_DIO0 -1  //2  no defnido aunque si lo tengo conectado

void iniciarLoRaConReintentos();

void setup() {
Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);

// Inicialización LoRa con manejo de errores
iniciarLoRaConReintentos();

    Serial.println("Termina SETUP...");

}

void loop() {

  
String mensaje = "PRUEBA SENSOR OK"; //debo solitiar la baja de mi mac
  LoRa.beginPacket();
  LoRa.print(mensaje);
  LoRa.endPacket();
  Serial.println("Enviando prueba del sensor a monitor.. ");
  digitalWrite(LED_PIN, HIGH);
delay(500);
 
/*
   if (LoRa.parsePacket()) {
         digitalWrite(LED_PIN, LOW);
    String respuesta = LoRa.readString();
    respuesta.trim();
   Serial.println("Recibido mensaje desde LORA  Monitor:  " + respuesta);
   delay(200);
    digitalWrite(LED_PIN, HIGH);
     delay(200);
  }*/
 
}


void iniciarLoRaConReintentos() {
  int intentos = 0;
  bool estadoLED = false;

  // Iniciar SPI manualmente
  SPI.begin(18, 19, 23, 5);  // SCK, MISO, MOSI, SS
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  while (!LoRa.begin(433E6)) {
    Serial.println("Error al iniciar LoRa. Reintentando...");

    // Verifica manualmente SPI
    Serial.println("SPI test: " + String(SPI.transfer(0x42), HEX));

    // Reset LoRa
    pinMode(LORA_RST, OUTPUT);
    digitalWrite(LORA_RST, LOW);
    delay(164);
    digitalWrite(LORA_RST, HIGH);
    delay(164);


    intentos++;
    Serial.println("Intento #" + String(intentos));
  }

  digitalWrite(LED_PIN, HIGH);
  Serial.println("LoRa listo después de " + String(intentos) + " intento(s)!");
}