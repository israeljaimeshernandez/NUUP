#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <WiFi.h>
#include <EEPROM.h>
#include <math.h> 
#include "driver/rtc_io.h" // Necesario para control de pines RTC
#include <algorithm>

// --- Configuración ---
#define INTERVALO_ENVIO_DATOS 30000
#define INTERVALO_PARPADEO 62 //configurando 62ms
#define INTERVALO_PARPADEO2 1000 //Dispositivo no configurado 1 seg
#define TIEMPO_BAJA_FORZADA 10000
#define  INTERVALO_ENVIO_SOLICITUD 1000

// --- Pines ---
#define BOTON_PIN 33  
#define LED_PIN 27
#define ADC_PIN 34
// --- LoRa ---
#define LORA_SS 5
#define LORA_RST 14   //no definido
#define LORA_DIO0 2  //no defnido
// --- Configuración Deep Sleep ---
#define BOTON_WAKEUP_PIN GPIO_NUM_33 // Usar el mismo pin que tu botón
// Variables conservadas durante el sleep
RTC_DATA_ATTR unsigned long ultimoEnvio = 0;
RTC_DATA_ATTR bool botonPresionadoSleep = false;
//variables del sensor
const int trigPin = 21;  // D21 (GPIO21)
const int echoPin = 22;  // D22 (GPIO22)
// --- Configuración de reintentos sensor---
#define MAX_REINTENTOS 20       // Número máximo de reintentos
#define TIEMPO_ENTRE_REINTENTOS 1000  // 1 segundo entre reintentos
#define TIEMPO_RESET_SENSOR 1000 // Tiempo para resetear el sensor (ms)
int reintentosRestantes = MAX_REINTENTOS;
unsigned long ultimoReintento = 0;
// --- EEPROM ---
#define EEPROM_SIZE 128
#define EEPROM_ADDR_REGISTRADO 0
#define EEPROM_ADDR_DATOS 1
// Estructura de datos
struct DispositivoData {
  char mac[18] = "";          // Inicializado vacío
  char nombre[21] = "";       // Inicializado vacío
  uint32_t altura = 0;
  uint32_t litros = 0;
};
// Variables globales
DispositivoData dispositivo;
bool registrado = false;
bool botonPresionado = false;
unsigned long tiempoInicioPresion = 0;
unsigned long ultimoEnvioDatos = 0;
unsigned long ultimoEnvioSolicitud = 0;
unsigned long ultimoCambioLED = 0;
bool estadoLED = false;
unsigned long ultimoCambioLED2 = 0;
bool estadoLED2 = false;

String macAddress;
int counter = 0;
// Prototipos de funciones
void inicializarDispositivo();
void guardarDatosEnEEPROM();
void leerDatosDeEEPROM();
void imprimirDatosDispositivo();
void limpiarEEPROMYReiniciar();
void enviarDatos(int distancia); 
void enviarSolicitudBaja();
void enviarSolicitudRegistro();
void procesarBaja();
void confirmarRegistro(String datos);
void manejarBoton();
float measureDistance();
int  obtenerDistanciaValida();
int calcularLitros(int distancia, uint32_t alturaTotal, uint32_t litrosTotal);
void resetearSensorUltrasonico();
void entrarDeepSleep();
void prepararParaDeepSleep();
void iniciarLoRaConReintentos();

void setup() {
Serial.begin(115200);
//pines de sensor
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(BOTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  rtc_gpio_hold_dis(BOTON_WAKEUP_PIN); // Asegurar que el pin no está bloqueado
  
   // Inicializar EEPROM
  EEPROM.begin(EEPROM_SIZE);
  registrado = EEPROM.read(EEPROM_ADDR_REGISTRADO) == 1;

  // Obtener MAC
  macAddress = WiFi.macAddress();
  macAddress.replace("-", ":");
  Serial.print("MAC: ");
  Serial.println(macAddress);

// Inicialización LoRa con manejo de errores
iniciarLoRaConReintentos();

  if (registrado) {
    leerDatosDeEEPROM();
    Serial.println("Dispositivo registrado. Operación normal.");
    imprimirDatosDispositivo();
  } else {
    Serial.println("Dispositivo NO registrado. Presione botón para registro.");
    inicializarDispositivo();
  }

    Serial.println("Termina SETUP...");
//      limpiarEEPROMYReiniciar();

  digitalWrite(LED_PIN, LOW);
delay(2000);
    digitalWrite(LED_PIN, HIGH);
delay(2000);
  digitalWrite(LED_PIN, LOW);
delay(2000);
    digitalWrite(LED_PIN, HIGH);
delay(2000);
  digitalWrite(LED_PIN, LOW);
delay(2000);
    digitalWrite(LED_PIN, HIGH);
delay(2000);
  digitalWrite(LED_PIN, LOW);
delay(2000);
    digitalWrite(LED_PIN, HIGH);
delay(2000);


}

void loop() {


 manejarBoton();

  if (registrado) {
  digitalWrite(LED_PIN, HIGH);
    // Medir distancia con manejo de errores
  int distancia = obtenerDistanciaValida();
    if (distancia < 0) { // Si hay error
      Serial.println("Error en sensor ultrasónico, enviando valor 9999");
      distancia = 9999; // Valor especial para indicar error
    }
    // Enviar datos (incluyendo el valor de error si aplica)
    enviarDatos(distancia);
        // Entrar en deep sleep
    Serial.println("No esta el boton pesionado manda a dormir...LOOP ....");
     prepararParaDeepSleep();
     esp_deep_sleep_start();
  }

 else{
  if (millis() - ultimoCambioLED2 >= INTERVALO_PARPADEO2) {
      estadoLED2 = !estadoLED2;
      digitalWrite(LED_PIN, estadoLED2);
      ultimoCambioLED2 = millis();
    }
  }
  
}

float measureDistance() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  long duration = pulseIn(echoPin, HIGH, 30000); // Timeout de 30ms
  return (duration <= 0) ? -1 : duration * 0.034 / 2;
}

// Función para inicializar estructura de dispositivo
void inicializarDispositivo() {
  memset(&dispositivo, 0, sizeof(dispositivo));
  strncpy(dispositivo.mac, macAddress.c_str(), sizeof(dispositivo.mac)-1);
  dispositivo.mac[sizeof(dispositivo.mac)-1] = '\0';
  strncpy(dispositivo.nombre, "Nuevo Dispositivo", sizeof(dispositivo.nombre)-1);
  dispositivo.altura = 0;
  dispositivo.litros = 0;
}

void guardarDatosEnEEPROM() {
  EEPROM.put(EEPROM_ADDR_DATOS, dispositivo);
  if (EEPROM.commit()) {
    Serial.println("Datos guardados correctamente en EEPROM");
  } else {
    Serial.println("Error al guardar en EEPROM");
  }
}

void leerDatosDeEEPROM() {
  EEPROM.get(EEPROM_ADDR_DATOS, dispositivo);
  
  // Verificar integridad de los datos
  if (strlen(dispositivo.mac) != 17) {
    Serial.println("Datos corruptos en EEPROM. Reinicializando...");
    inicializarDispositivo();
    guardarDatosEnEEPROM();
  }
}

void imprimirDatosDispositivo() {
  Serial.println("\n--- Datos del Dispositivo ---");
  Serial.print("MAC: ");
  Serial.println(dispositivo.mac);
  Serial.print("Nombre: ");
  Serial.println(dispositivo.nombre);
  Serial.print("Altura: ");
  Serial.println(dispositivo.altura);
  Serial.print("Litros: ");
  Serial.println(dispositivo.litros);
  Serial.println("----------------------------\n");
}

void limpiarEEPROMYReiniciar() {
  for (int i = 0; i < EEPROM_SIZE; i++) {
    EEPROM.write(i, 0xFF);
  }
  EEPROM.commit();
  Serial.println("EEPROM limpiada. Reiniciando...");
  ESP.restart();
}

void enviarSolicitudBaja() {
  String mensaje = "BAJA02:" + macAddress; //debo solitiar la baja de mi mac
  LoRa.beginPacket();
  LoRa.print(mensaje);
  LoRa.endPacket();
  Serial.print("[Baja] ");
  Serial.println(mensaje);
}

void enviarSolicitudRegistro() {
  String mensaje = "REG02:" + macAddress;
  LoRa.beginPacket();
  LoRa.print(mensaje);
  LoRa.endPacket();
  Serial.print("Solicitud de Registro desde sensor: ");
  Serial.println(mensaje);
}

void procesarBaja() {
  Serial.println("Baja confirmada. Reiniciando...");
  limpiarEEPROMYReiniciar();
  delay(1000);  //si no me espero continua con baja luego alta consecutivamente por que es el mismo boton
}

void confirmarRegistro(String datos) {
  int pos1 = datos.indexOf(',');
  int pos2 = datos.indexOf(',', pos1+1);
  int pos3 = datos.indexOf(',', pos2+1);
  int pos4 = datos.indexOf(',', pos3+1);
  
  if (pos1 != -1 && pos2 != -1 && pos3 != -1 && pos4 != -1) {
    String macRecibida = datos.substring(pos1+1, pos2);
    String nombreRecibido = datos.substring(pos2+1, pos3);
    String alturaStr = datos.substring(pos3+1, pos4);
    String litrosStr = datos.substring(pos4+1);
    
    if (macRecibida.length() == 17) {
      memset(&dispositivo, 0, sizeof(dispositivo));
      
      strncpy(dispositivo.mac, macRecibida.c_str(), sizeof(dispositivo.mac)-1);
      dispositivo.mac[sizeof(dispositivo.mac)-1] = '\0';
      
      strncpy(dispositivo.nombre, nombreRecibido.c_str(), sizeof(dispositivo.nombre)-1);
      dispositivo.nombre[sizeof(dispositivo.nombre)-1] = '\0';
      
      dispositivo.altura = alturaStr.toInt();
      dispositivo.litros = litrosStr.toInt();
      
      guardarDatosEnEEPROM();
      registrado = true;
      EEPROM.write(EEPROM_ADDR_REGISTRADO, 1);
      EEPROM.commit();
      
      Serial.println("¡Registro confirmado correctamente!");
      imprimirDatosDispositivo();
      delay(1000);  //si no me espero continua con baja luego alta consecutivamente por que es el mismo boton

    } else {
      Serial.println("Error: Longitud de MAC inválida");
    }
  } else {
    Serial.println("Error: Formato de registro inválido");
  }
}

void manejarBoton() {
  tiempoInicioPresion = millis();
  ultimoCambioLED = millis();

  while(!digitalRead(BOTON_PIN)){

 
if (millis() - ultimoEnvioSolicitud >= INTERVALO_ENVIO_SOLICITUD) {
    if (registrado) {
      enviarSolicitudBaja();
    } else {
      enviarSolicitudRegistro();
    }
    ultimoEnvioSolicitud = millis();
  }

  if (registrado && millis() - tiempoInicioPresion >= TIEMPO_BAJA_FORZADA) {
    Serial.println("Baja forzada por tiempo (BAJA FORZADA) espera 5 seg y Deep Sleep..");
      digitalWrite(LED_PIN, LOW);
      delay(5000);

    limpiarEEPROMYReiniciar();
    prepararParaDeepSleep();
    esp_deep_sleep_start();
  }

 if (LoRa.parsePacket()) {
    String respuesta = LoRa.readString();
    respuesta.trim();
    Serial.print("[Recibiendo mensaje de respuesta....] ");
    Serial.println(respuesta);

    if (respuesta == "OK_BAJA" ) {
      procesarBaja();
    Serial.println("Recibe confirmacion de Baja manda a dormir espero 5 segundos...");
         digitalWrite(LED_PIN, LOW);
      delay(5000);

  prepararParaDeepSleep();
    esp_deep_sleep_start();

    } else if (respuesta.startsWith("OK_REG") && !registrado) {
      confirmarRegistro(respuesta);
         digitalWrite(LED_PIN, HIGH);
      delay(5000);
      Serial.println("Recibe confirmacion de ALTA manda a dormir espero 5 seg...");
    }
  }


    if (millis() - ultimoCambioLED >= INTERVALO_PARPADEO) {
      estadoLED = !estadoLED;
      digitalWrite(LED_PIN, estadoLED);
      ultimoCambioLED = millis();
    }
 
 }
}


/**
 * Calcula el volumen en litros basado en la distancia medida y los parámetros del tanque
 * @param distance Distancia medida por el sensor ultrasónico (cm)
 * @param alturaTotal Altura total del tanque (cm)
 * @param litrosTotal Capacidad total del tanque (litros)
 * @return Volumen en litros (float)
 */

// --- Función modificada para cálculo con enteros ---
int calcularLitros(int distancia, uint32_t alturaTotal, uint32_t litrosTotal) {
  if (alturaTotal == 0 || litrosTotal == 0 || distancia < 0) {
    return 0; // Valores inválidos
  }
  // Calcular altura del líquido (entero)
  int alturaLiquido = alturaTotal - distancia;
  
  // Validar rangos
  if (alturaLiquido <= 0) return 0;
  if (alturaLiquido >= alturaTotal) return litrosTotal;
  
  // Calcular litros (todo en enteros para evitar floats)
  return (alturaLiquido * litrosTotal) / alturaTotal;
}

/**
 * Obtiene la distancia válida del sensor ultrasónico
 * @return Distancia en cm (float), o -1 si hay error
 */

// --- Función para obtener distancia con manejo de errores ---
int obtenerDistanciaValida() {
  int intentos = 0;
  float distancia = -1;
  
  while (intentos < MAX_REINTENTOS) {
    // Medir distancia
    digitalWrite(trigPin, LOW);
    delayMicroseconds(2);
    distancia = measureDistance();
    
    // Verificar si el valor es válido
    if (distancia >= 0 && distancia <= dispositivo.altura * 1.5) {
      return round(distancia); // Devolver valor válido
    }
    
    // Si no es válido
    Serial.print("Medición inválida (");
    Serial.print(distancia);
    Serial.println(" cm), reintentando...");
    
    intentos++;
    if (intentos % 5 == 0) { // Cada 5 intentos fallidos
      resetearSensorUltrasonico();
    }
    
    delay(TIEMPO_ENTRE_REINTENTOS);
  }
  
  // Si llegamos aquí es que agotamos los reintentos
  Serial.println("No se pudo obtener medición válida después de " + String(MAX_REINTENTOS) + " intentos");
  return -1; // Indicador de error
}


void enviarDatos(int distancia) {
  int litrosActuales = 0;
  
  // Solo calcular litros si la distancia es válida (no es 9999)
  if (distancia != 9999) {
    litrosActuales = calcularLitros(distancia, dispositivo.altura, dispositivo.litros);
    
    Serial.print("Distancia medida: ");
    Serial.print(distancia);
    Serial.print(" cm, Litros calculados: ");
    Serial.print(litrosActuales);
    Serial.println(" L");
Serial.print("Altura:");
    Serial.println(dispositivo.altura);
    Serial.print("Litros:");
    Serial.println(dispositivo.litros);
    Serial.print("Nombre:");
    Serial.println(dispositivo.nombre);
  } else {
    litrosActuales = 9999; // Propagamos el valor de error
    Serial.println("Enviando valor de error (9999)");
  }

 // Construir mensaje maximo 10 datos
 String mensaje = "002,"+
                  macAddress + "," + 
                  String(litrosActuales) + "," + 
String(round((analogRead(12.052) / 4095.0 * 3.3 * 2.0 ))) + "," +                   
                  //String(round((analogRead(ADC_PIN) / 4095.0 * 3.3 * 2.0 ))) + "," + 
                  String(round(temperatureRead() ))+","+
                  String(dispositivo.altura)+ "," + 
                  String(dispositivo.litros)+ "," + 
                  String(dispositivo.nombre)+ ","                                     
                  ;
  
  // Enviar por LoRa
  LoRa.beginPacket();
  LoRa.print(mensaje);
  LoRa.endPacket();

  Serial.print("[Datos] ");
  Serial.println(mensaje);
}

// --- Función para resetear el sensor ---
void resetearSensorUltrasonico() {
  Serial.println("Reseteando sensor ultrasónico...");
  
  // Cambiar pines a entrada para resetear
  pinMode(trigPin, INPUT);
  pinMode(echoPin, INPUT);
  delay(10); // Pequeña pausa
  
  // Volver a configurar pines
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  digitalWrite(trigPin, LOW);
  
  Serial.println("Sensor reseteado");
}


void entrarDeepSleep() {
  Serial.println("Preparando para deep sleep...");
  Serial.flush();
  
  // Configurar wakeup sources
  esp_sleep_enable_ext0_wakeup(BOTON_WAKEUP_PIN, LOW);
  
  // Calcular tiempo restante para próximo envío
  unsigned long tiempoTranscurrido = millis() - ultimoEnvio;
  if (tiempoTranscurrido < INTERVALO_ENVIO_DATOS) {
    unsigned long sleepTime = INTERVALO_ENVIO_DATOS - tiempoTranscurrido;
    esp_sleep_enable_timer_wakeup(sleepTime * 1000);
    Serial.print("Durmiendo por ");
    Serial.print(sleepTime / 1000);
    Serial.println(" segundos");
  } else {
    // Si ya pasó el tiempo, dormir mínimo 1 segundo
    esp_sleep_enable_timer_wakeup(1 * 1000000);
    Serial.println("Durmiendo 1 segundo (intervalo ya pasado)");
  }
  
  // Configurar pines para bajo consumo
  LoRa.sleep();
  WiFi.mode(WIFI_OFF);
  btStop();
  
  // Entrar en deep sleep
  esp_deep_sleep_start();
}


// Nueva función para preparar el deep sleep
void prepararParaDeepSleep() {
  Serial.println("Preparando para deep sleep...");
  Serial.flush();
  
  // Configurar wakeup por botón
  esp_sleep_enable_ext0_wakeup(BOTON_WAKEUP_PIN, LOW);
  
  // Configurar wakeup por timer
  unsigned long tiempoTranscurrido = millis() - ultimoEnvioDatos;
  unsigned long sleepTime = INTERVALO_ENVIO_DATOS - std::min<unsigned long>(tiempoTranscurrido, INTERVALO_ENVIO_DATOS);
  esp_sleep_enable_timer_wakeup(sleepTime * 1000);
  
  // Poner periféricos en modo bajo consumo
  LoRa.sleep();
  WiFi.mode(WIFI_OFF);
  btStop();
  
  Serial.print("Durmiendo por ");
  Serial.print(sleepTime / 1000);
  Serial.println(" segundos");
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

    estadoLED = !estadoLED;
    digitalWrite(LED_PIN, estadoLED);
    intentos++;
    Serial.println("Intento #" + String(intentos));
  }

  digitalWrite(LED_PIN, LOW);
  Serial.println("LoRa listo después de " + String(intentos) + " intento(s)!");
}