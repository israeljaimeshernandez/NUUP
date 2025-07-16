#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>




//para asignar un numero de serial al dispositivo lector
const String serial_number = "ENTRADAPRINCIPALVILLAS"; 

//para el uso del segundo procesador
TaskHandle_t Task1;


//*****************************
//***   CREDENCIALES WIFFI  ***
//*****************************
const char* ssid     = "IJH";
const char* password = "Kfl-0878";


//*****************************
//***   CONFIGURACION MQTT  ***
//*****************************

const char *mqtt_server ="168.231.67.152";
const int  mqtt_port =1883;
const char *mqtt_user="nuup_web";
const char *mqtt_pass ="Kfl-0878";

WiFiClient espClient;
PubSubClient client(espClient);

long lastMsg = 0;
char msg[50];
bool send_access_query = false;

//*****************************
//*** DEFINICIONES WIGAND ***
//*****************************
#define MAX_BITS 100                  // Máximo número de bits 
#define WEIGAND_WAIT_TIME  3000       // Tiempo de espera para otro pulso Weigand  

// Pines ESP32 (ajusta según tu conexión)
#define DATA0_PIN 26                  // GPIO26 para DATA0
#define DATA1_PIN 25                  // GPIO25 para DATA1
#define LED_RED   2                   // LED en GPIO2

// Variables globales volátiles para interrupciones
volatile unsigned char databits[MAX_BITS]; // Almacena todos los bits de datos
volatile unsigned char bitCount = 0;      // Número de bits capturados
volatile unsigned char flagDone = 1;      // Bandera de captura completada
volatile unsigned int weigand_counter;    // Contador para espera Weigand

// Variables para el código decodificado
unsigned long facilityCode = 0;      // Código de instalación decodificado
unsigned long cardCode = 0;         // Código de tarjeta decodificado
int estaus_no_decodifica = 0;

// Prototipos de funciones
void IRAM_ATTR ISR_DATA0();
void IRAM_ATTR ISR_DATA1();
void printBits();
void processWeigandData();

//no se si se ocupan aqui
String rfid = "";
String user_name = "";

//*****************************
//*** DECLARACION FUNCIONES ***
//*****************************
// Funciones WIGAND
void IRAM_ATTR handleD0();
void IRAM_ATTR handleD1();
void printBinary(unsigned long data, byte count);
bool isCardValid(unsigned long facilityCode, unsigned long cardCode);
void processRFID();
//funciones wiffi
void setup_wiffi();
void callback(char* topic, byte* payload, unsigned int length);
void reconnect();
void access_screen(bool access);
void closing();
void opening();
void iddle();
void sending();



//*****************************
//***   SENSOR INT TEMP     ***
//*****************************

#ifdef __cplusplus
extern "C" {
	#endif

	uint8_t temprature_sens_read();

	#ifdef __cplusplus
}
#endif

uint8_t temprature_sens_read();

//*****************************
//***   TAREA OTRO NUCLEO   ***
//*****************************

void codeForTask1(void *parameter)
{

	for (;;)
	{

    	Serial.println("Segundo Procesador...");


		vTaskDelay(5000);
	}
}


//*********************************************************
//***  Rutinas de servicio de interrupción (ISRs)   ***
//*********************************************************
void IRAM_ATTR ISR_DATA0() {
  if(bitCount < MAX_BITS) {
    databits[bitCount] = 0;
    bitCount++;
  }
  flagDone = 0;
  weigand_counter = WEIGAND_WAIT_TIME;
}

void IRAM_ATTR ISR_DATA1() {
  if(bitCount < MAX_BITS) {
    databits[bitCount] = 1;
    bitCount++;
  }
  flagDone = 0;
  weigand_counter = WEIGAND_WAIT_TIME;
}

//****************
//***  SETUP   ***
//****************

void setup() {
  
  
  Serial.begin(9600);
  Serial.println("RFID Reader ESP32 - Versión Mejorada");


 //Configuraciones WIGAND
  // Configuración de interrupciones para ESP32
  pinMode(DATA0_PIN, INPUT_PULLUP);     // DATA0 con pullup interno
  pinMode(DATA1_PIN, INPUT_PULLUP);     // DATA1 con pullup interno
  pinMode(LED_RED, OUTPUT);             // Configura LED como salida
  attachInterrupt(digitalPinToInterrupt(DATA0_PIN), ISR_DATA0, FALLING);
  attachInterrupt(digitalPinToInterrupt(DATA1_PIN), ISR_DATA1, FALLING);
  weigand_counter = WEIGAND_WAIT_TIME;


 //Configuraciones SEGUNDO NUCLEO
	xTaskCreatePinnedToCore(
		codeForTask1, /* Task function. */
		"Task_1",	 /* name of task. */
		1000,		  /* Stack size of task */
		NULL,		  /* parameter of the task */
		1,			  /* priority of the task */
		&Task1,		  /* Task handle to keep track of created task */
		0);			  /* Core */

		setup_wiffi();
		client.setServer(mqtt_server, mqtt_port);
		client.setCallback(callback);



}

void loop() {
  // WEIGAND Espera hasta que no haya más pulsos de datos
  if (!flagDone) {
    if (--weigand_counter == 0) {
      flagDone = 1;
      processWeigandData(); // Procesar datos fuera de la ISR
    }
  }
  
  
if (!client.connected()) {
			reconnect();
		}

		client.loop();

		long now = millis();
		if (now - lastMsg > 2000){
			lastMsg = now;
			String to_send = String((temprature_sens_read() - 32) / 1.8);
			to_send.toCharArray(msg, 50);

			char topic[50];
			String topic_aux = serial_number + "/temp";
			topic_aux.toCharArray(topic,50);
	Serial.print("topico -->");
	Serial.println(topic);
  Serial.print("mensaje -->");
	Serial.println(msg);
			client.publish(topic, msg);
		}

		if (send_access_query == true){

			String to_send = rfid;
			rfid = "";

			sending();
			to_send.toCharArray(msg, 50);

			char topic[50];
			String topic_aux = serial_number + "/access_query";
			topic_aux.toCharArray(topic,50);

	Serial.print("topico -->");
	Serial.println(topic);
  Serial.print("mensaje -->");
	Serial.println(msg);

  client.publish(topic, msg);

			send_access_query = false;
		}







}



	//*****************************
	//***    funciones WIGAND   ***
	//*****************************
void processWeigandData() {
  if (bitCount > 0) {
    Serial.print("Leyendo: ");
    Serial.print(bitCount);
    Serial.println(" bits.");

    // Copia los datos volátiles a variables locales para procesamiento seguro
    unsigned char localBitCount = bitCount;
    unsigned char localDatabits[MAX_BITS];
    for(int i=0; i<localBitCount; i++) {
      localDatabits[i] = databits[i];
    }

    // Reinicia las variables de captura
    bitCount = 0;
    facilityCode = 0;
    cardCode = 0;

    if (localBitCount == 35) {
      // Formato HID Corporate 1000 de 35 bits
      for (int i=2; i<14; i++) {
        facilityCode <<= 1;
        facilityCode |= localDatabits[i];
      }
      for (int i=14; i<34; i++) {
        cardCode <<= 1;
        cardCode |= localDatabits[i];
      }
      Serial.println("Formato Weigand de 35 bits detectado");
      printBits();
    } 
    else if (localBitCount == 26) {
      // Formato estándar de 26 bits
      for (int i=1; i<9; i++) {
        facilityCode <<= 1;
        facilityCode |= localDatabits[i];
      }
      for (int i=9; i<25; i++) {
        cardCode <<= 1;
        cardCode |= localDatabits[i];
      }
      Serial.println("Formato Weigand de 26 bits detectado");
      printBits();
    } 
    else {
      estaus_no_decodifica = 1;
      Serial.println("Formato no reconocido - No se puede decodificar");
    }
  }
}

void printBits() {
  Serial.print("Código de Instalación (FC) = ");
  Serial.print(facilityCode);
  Serial.print(", Código de Tarjeta (CC) = ");
  Serial.println(cardCode);
  rfid=String(facilityCode) + String(cardCode);
  Serial.print("Codigo completo FC + CC: ");
  Serial.println(rfid);
  send_access_query = true;

  digitalWrite(LED_RED, LOW); // Apaga LED rojo
  
  if(facilityCode == 177 && cardCode == 404) {
    Serial.println("Autenticada Tarjeta de Israel Jaimes");
    digitalWrite(LED_RED, HIGH); // Enciende LED para acceso autorizado
    delay(1000);
  }
  else if(facilityCode == 177 && cardCode == 102) {
    Serial.println("Autenticada Tarjeta de Maria Jenny");
    digitalWrite(LED_RED, HIGH); // Enciende LED para acceso autorizado
    delay(1000);
  }
  else if(facilityCode == 177) {
    Serial.print("Acceso no autorizado.. FCC: ");
    Serial.print(facilityCode);
    Serial.print(" Código de Tarjeta: ");
    Serial.println(cardCode);
    // Parpadeo rápido para acceso denegado
    for(int i=0; i<5; i++) {
      digitalWrite(LED_RED, HIGH);
      delay(100);
      digitalWrite(LED_RED, LOW);
      delay(100);
    }
  }
}



	//*****************************
	//*** PANTALLAS ACCESO      ***
	//*****************************

	void access_screen(bool access) {

		if (access) {
			 Serial.println("Hola " + user_name);
			 Serial.println("");
			 Serial.println(" ACCESO PERMITIDO");

			delay(2000);
			opening();
		}else{

			Serial.println("ACCESO DENEGADO");
			delay(2000);
			iddle();
		}
	}

	void opening() {


		Serial.println("ABRIENDO...");
		delay(2000);
		iddle();

	}

	void closing() {

    Serial.println("CERRANDO...");
		delay(2000);
		iddle();

	}

	void sending() {

		Serial.println("Aguarde");
		delay(300);
		Serial.println(".");
		delay(300);
		Serial.println(".");
		delay(300);
		Serial.println(".");
		delay(300);

	}

	void iddle(){
	Serial.println("INGRESE TARJETA");
	}

	//*****************************
	//***  CONEXION WIFI MQTT   ***
	//*****************************
void setup_wiffi(){

delay(10);
Serial.println();
Serial1.print("Conectando a: ");
Serial.println(ssid);

WiFi.begin(ssid,password);

while(WiFi.status()!=WL_CONNECTED){
delay(500);
Serial.print(".");
 }
Serial.println("");
Serial.println("Conectado a la RED WiFFi VSCODE");
Serial.println("Direccion IP:");
Serial.println(WiFi.localIP());

}



	void callback(char* topic, byte* payload, unsigned int length){
		String incoming = "";
		Serial.print("Mensaje recibido desde -> ");
		Serial.print(topic);
		Serial.println("");
		for (int i = 0; i < length; i++) {
			incoming += (char)payload[i];
		}
		incoming.trim();
		Serial.println("Mensaje -> " + incoming);

		String str_topic(topic);

		if (str_topic == serial_number + "/open"){

			if ( incoming == "open") {
				digitalWrite(BUILTIN_LED, HIGH);
				opening();
			}

			if ( incoming == "close") {
				digitalWrite(BUILTIN_LED, LOW);
				closing();
			}

			if ( incoming == "granted") {
				digitalWrite(BUILTIN_LED, HIGH);
				access_screen(true);
			}

			if ( incoming == "refused") {
				digitalWrite(BUILTIN_LED, LOW);
				access_screen(false);
			}
		}

		if (str_topic == serial_number + "/user_name"){
			user_name = incoming;
		}



	}

	void reconnect() {

		while (!client.connected()) {
			Serial.print("Intentando conexión Mqtt...");
			// Creamos un cliente ID
			String clientId = "esp32_";
			clientId += String(random(0xffff), HEX);
			// Intentamos conectar
			if (client.connect(clientId.c_str(),mqtt_user,mqtt_pass)) {
				Serial.println("Conectado!");

				// Nos suscribimos a comandos
				char topic[50];
				String topic_aux = serial_number + "/command";
				topic_aux.toCharArray(topic,50);
				client.subscribe(topic);

				// Nos suscribimos a username
				char topic2[50];
				String topic_aux2 = serial_number + "/user_name";
				topic_aux2.toCharArray(topic2,50);
				client.subscribe(topic2);

			} else {
				Serial.print("falló :( con error -> ");
				Serial.print(client.state());
				Serial.println(" Intentamos de nuevo en 5 segundos");

				delay(5000);
			}
		}
	}

  
//https://www.udemy.com/course/iot-masterclass/learn/lecture/15250188#overview
//  https://www.pagemac.com/projects/rfid/arduino_wiegand  codigo wigand con hrfid
//https://www.luisllamas.es/que-es-esp-now-esp32/  diagramado esp