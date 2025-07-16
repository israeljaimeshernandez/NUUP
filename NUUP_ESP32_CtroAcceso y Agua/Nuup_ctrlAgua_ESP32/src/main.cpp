#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <LiquidCrystal_I2C.h>
#include <Preferences.h>
Preferences preferences;

#define Gpio_bomba_municipal   15
#define Gpio_bomba_solares     02
#define Gpio_bomba_cisterna1   04
#define Gpio_bomba_cisterna2   05
#define Gpio_suspension_total  18

//Esntradas del ESP32
#define Gpio_suspende_bomba_municipal  13
#define Gpio_suspende_bomba_solares    12
#define Gpio_suspende_bomba_cisterna1  14
#define Gpio_suspende_bomba_cisterna2  27

//Sensor ultrasonico viendolo desde atras en la bornera del sensor
// 5V-RxTrig-TxEcho-GND
#define echoPin 25  
//bornera que indica DAC2 esquina D2 hay 2 pines mas GND VCC
/* conecta el pin RX2 al pin Echo de JSN-SR0*/
#define trigPin  26
//bornera que indica DAC1 D1 hay 2 pines mas GND VCC
/* conecta el pin TX2 ESP32 al pin Trig de JSN-SR04       */   

//Variables heredadas de ARDUINO CLOUD
  float distancia_confirmada;  
  float nivelc1;
  float nivelc2;
  int altura_c1;
  int altura_c2;
  int capacidad_deposito;
  int distancia_cm;
  int duracion;
  bool error_sensor_c1;
  bool calendario_suspension_activo_cisterna1;
  bool calendario_suspension_activo_cisterna2;
  bool calendario_suspension_activo_municipal;
  bool calendario_suspension_activo_solares;
  bool estatus_bomba_cisterna1;
  bool estatus_bomba_cisterna2;
  bool estatus_bomba_municipal;
  bool estatus_bomba_solares;
  bool suspension_toma_cisterna1;
  bool suspension_toma_cisterna2;
  bool suspension_toma_municipal;
  bool suspension_toma_solares;
  bool suspension_total;
  bool triger_cisterna1_vacia;
  bool triger_cisterna2_vacia;
//perteecia a una funcion pero lo transfiero a boleano
  bool apagado_nocturno_bomba_cisterna1;
  bool apagado_nocturno_bomba_cisterna2;
  bool apagado_nocturno_bomba_municipal;
  bool apagado_nocturno_bomba_solares;

//<<<<<<<     Definciones sensores de LCD    >>>>>>
//<<<<<     .............................................
LiquidCrystal_I2C lcd(0x27,16,2);
String texto_estatus_niveles1="";
String texto_estatus_niveles2="";


String texto_estatus_bomba[4] = {"","","",""};
String texto_susp_estatus_bomba[4] = {"","","",""};

 //variable para actualzar el tiempo
unsigned long tiempo_actual =0;

//existe una suspension de calendario activa
bool suspension=0;

 //<<<<<<<     Definiciones MQTT  y WIFI  >>>>>>
//<<<<<     ............................................. 
const char* ssid ="IJH";
const char* pasword ="Kfl-0878";
const char *mqtt_server ="168.231.67.152";
const int  mqtt_port =1883;
const char *mqtt_user="nuup_web";
const char *mqtt_pass ="Kfl-0878";

WiFiClient espClient;
PubSubClient client(espClient);

long lastMsg = 0;
char msg[25];

//menses de dashboard para enviar a NODEJS 
int temp1 = 1;
int temp2 = 2;
int volts = 3;

//******************************************* */
// DECLARACION FUNCIONES  
//******************************************* */
void setup_wiffi();
void callback(char* topic, byte* playload, unsigned int lengt);
void reconnect();
void lcd_display();

//******************************************* */
// SETUP
//******************************************* */
void setup() {
  Serial.begin(9600);
  Serial.println("ESPVILLAS VSCODE");


//<<<<<<<     Definiciones LCD i2C  >>>>>>
//<<<<<     ................................ 
lcd.init();
lcd.backlight();
//lcd.noBacklight();
lcd.clear();
lcd.setCursor(0,0);
lcd.print("INICIA ESP32 ");
lcd.setCursor(0,1);
lcd.print("CISTERNAS VILLAS");
delay(1000);

//<<<<<<<     Definiciones GPIO  >>>>>>
//<<<<<     ................................ 

    //salida para el rele de desactivacion bomba Cisterna en el contactor interrumpiendo A1 A2
  pinMode(Gpio_suspende_bomba_municipal, OUTPUT);    // LED pin as output.
  digitalWrite(Gpio_suspende_bomba_municipal, HIGH); //inicia siempre apagado
  pinMode(Gpio_suspende_bomba_solares, OUTPUT);    // LED pin as output.
  digitalWrite(Gpio_suspende_bomba_solares, HIGH); //inicia siempre apagado  
  pinMode(Gpio_suspende_bomba_cisterna1, OUTPUT);    // LED pin as output.
  digitalWrite(Gpio_suspende_bomba_cisterna1, HIGH); //inicia siempre apagado 
  pinMode(Gpio_suspende_bomba_cisterna2, OUTPUT);    // LED pin as output.
  digitalWrite(Gpio_suspende_bomba_cisterna2, HIGH); //inicia siempre apagado


 pinMode(Gpio_bomba_municipal, INPUT);
 pinMode(Gpio_bomba_solares, INPUT);
 pinMode(Gpio_bomba_cisterna1, INPUT);
 pinMode(Gpio_bomba_cisterna2, INPUT);
 pinMode(Gpio_suspension_total, INPUT);
  
  //Sensor ultrasonico
pinMode(trigPin, OUTPUT);
pinMode(echoPin,INPUT);

//eeprom lee si hay dato guardado y no entra Wiffi
  // Iniciar la memoria (namespace: "config")
  preferences.begin("config", false);
 // Leer el valor guardado
  nivelc1=preferences.getFloat("nivelc1", 0);
  nivelc1=preferences.getFloat("nivelc2", 0);
  altura_c1=preferences.getInt("altura_c1", 0);
  altura_c2=preferences.getInt("altura_c2", 0);
  capacidad_deposito=preferences.getInt("capacidad_deposito", 0);
  distancia_confirmada=preferences.getFloat("distancia_confirmada", 0);
  estatus_bomba_cisterna1=preferences.getBool("estatus_bomba_cisterna1", 0);
  estatus_bomba_cisterna2=preferences.getBool("estatus_bomba_cisterna2", 0);
  estatus_bomba_municipal=preferences.getBool("estatus_bomba_municipal", 0);
  estatus_bomba_solares=preferences.getBool("estatus_bomba_solares", 0);
  suspension_toma_cisterna1=preferences.getBool("suspension_toma_cisterna1", 0);
  suspension_toma_cisterna2=preferences.getBool("suspension_toma_cisterna2", 0);
  suspension_toma_municipal=preferences.getBool("suspension_toma_municipal", 0);
  suspension_toma_solares=preferences.getBool("suspension_toma_solares", 0);
  suspension_total=preferences.getBool("suspension_total", 0);
 ////Serial.print("Valor leído: ");
 ////Serial.println(valorGuardado);
 // Cerrar la memoria
  preferences.end();

//inicializa temporal
//Variables heredadas de ARDUINO CLOUD deben ser programadas en dashboard
   distancia_confirmada=0;  
   nivelc1=0;
   nivelc2=0;
   altura_c1=250;
   altura_c2=0;
   capacidad_deposito=18000;
   distancia_cm=0;
   duracion=0;
   calendario_suspension_activo_cisterna1=false;
   calendario_suspension_activo_cisterna2=false;
   calendario_suspension_activo_municipal=false;
   calendario_suspension_activo_solares=false;
   error_sensor_c1=false;
   estatus_bomba_cisterna1=false;
   estatus_bomba_cisterna2=false;
   estatus_bomba_municipal=false;
   estatus_bomba_solares=false;
   suspension_toma_cisterna1=false;
   suspension_toma_cisterna2=false;
   suspension_toma_municipal=false;
   suspension_toma_solares=false;
   suspension_total=false;
   triger_cisterna1_vacia=false;
   triger_cisterna2_vacia=false;
//perteecia a unafuncion pero lo transfiero a boleano
   apagado_nocturno_bomba_cisterna1=false;
   apagado_nocturno_bomba_cisterna2=false;
   apagado_nocturno_bomba_municipal=false;
   apagado_nocturno_bomba_solares=false;
//finaliza inicializacion temporal

//<<<<<<<     Inicia WIFFI y MQTT  >>>>>>
//<<<<<     ................................ 
randomSeed(micros());   //variable para generar numeros aleatorios
setup_wiffi();  

client.setServer(mqtt_server,mqtt_port);
client.setCallback(callback);

}

void loop() {


/*
//Primero tengo que checar si es la primera vez como inicializa a cero las variables
//eeprom lee si hay dato guardado y no entra Wiffi
  // Iniciar la memoria (namespace: "config")
  preferences.begin("config", false);
  // Guardar un valor en memoria
  preferences.putFloat("nivelc1", nivelc1);
  preferences.putFloat("nivelc2", nivelc2);
  preferences.putInt("altura_c1", altura_c1);
  preferences.putInt("altura_c2", altura_c2);
  preferences.putInt("capacidad_deposito", capacidad_deposito);
  preferences.putFloat("distancia_confirmada",distancia_confirmada);
  preferences.putInt("estatus_bomba_cisterna1",estatus_bomba_cisterna1);
  preferences.putInt("estatus_bomba_cisterna2", estatus_bomba_cisterna2);
  preferences.putInt("estatus_bomba_municipal", estatus_bomba_municipal);
  preferences.putInt("estatus_bomba_solares", estatus_bomba_solares);
  preferences.putInt("suspension_toma_cisterna1", suspension_toma_cisterna1);
  preferences.putInt("suspension_toma_cisterna2",suspension_toma_cisterna2);
  preferences.putInt("suspension_toma_municipal", suspension_toma_municipal);
  preferences.putInt("suspension_toma_solares", suspension_toma_solares);
  preferences.putInt("suspension_total", suspension_total);
  ////Serial.println("Valor guardado: distancia_confirmada");
 // Leer el valor guardado
  ////Serial.print("Valor leído: ");
  ////Serial.println(valorGuardado);
 // Cerrar la memoria
  preferences.end();
*/

 if(!client.connected()){
 reconnect();
 }
//con este metodo estaria escuchando en todo momento si llego un mensaje
client.loop();

//CODIGOCLOUD
suspension_total =digitalRead(Gpio_suspension_total); 
     if(suspension_total){
         digitalWrite(Gpio_suspende_bomba_cisterna1, LOW);
         digitalWrite(Gpio_suspende_bomba_cisterna2, LOW);
         digitalWrite(Gpio_suspende_bomba_solares, LOW);
         digitalWrite(Gpio_suspende_bomba_municipal, LOW);
lcd.blink();
lcd.clear();
lcd.setCursor(0,0);
lcd.print("SUSPENSION TOTAL");
lcd.setCursor(0,1);
lcd.print("DE SISTEMA");
delay(500);       
  Serial.print("SUSPENSION TOTAL DEL SISTEMA");
     } else {  

  lcd.noBlink();
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH); // activa el disparador para generar pulso
  delayMicroseconds(10); // mantenga el disparador "ON" durante 10 ms para generar el pulso
  digitalWrite(trigPin, LOW); // Desactiva el disparador de pulso para detener el pulso

  // Si el pulso llega al receptor echoPin
  // se vuelve alto Entonces pulseIn() devuelve el
  // tiempo que tarda el pulso en llegar al receptor
  duracion = pulseIn(echoPin, HIGH);
  distancia_cm = duracion * 0.0344 / 2;

  Serial.print("Dist CM: ");
  Serial.print(distancia_cm);
  Serial.println("cm");
  Serial.print("Duracion: ");
  Serial.print(duracion);
  Serial.println(" T");
if(distancia_cm==608 or distancia_cm==609 or distancia_cm==611){
  Serial.print("ERR:");  Serial.println(distancia_cm);
  nivelc1 =-1;
}else{
temp1=distancia_cm;//mando al dashboard
temp2=duracion;//mando al dashboard
//cantidad de agua en el deposito si tenemos la altura del usuario y la capacidad del deposito a esa altura 
nivelc1 =(((altura_c1-distancia_cm)/ float(altura_c1)) *capacidad_deposito);
volts=int(nivelc1);//mando al dashboard
  Serial.print("Nivel: ");
  Serial.print(nivelc1);

}



       lcd.clear();
lcd.setCursor(0,0);
lcd.print("Dist1 CM:  ");lcd.print(String(distancia_cm));
lcd.setCursor(0,1);
lcd.print(" ULTRASONIDO 1");
       delay(300);//le baje el tiempo mucho ya que retarda dashboard
       //lcd.noBacklight();

estatus_bomba_cisterna1 =digitalRead(Gpio_bomba_cisterna1); //Es negado por que es PULL UP
  if (estatus_bomba_cisterna1){ //negado por que es siempre a 5V al cerrar con tierra activa 
   texto_estatus_bomba[0] = "Bomba C1 activa";
     Serial.print("Bomba C1 activa");
  }else {
texto_estatus_bomba[0] = "Bomba C1 espera";
     Serial.print("Bomba C1 ESPERA");
}  

 
estatus_bomba_cisterna2 =digitalRead(Gpio_bomba_cisterna2); //Es negado por que es PULL UP
  if (estatus_bomba_cisterna2){ //negado por que es siempre a 5V al cerrar con tierra activa 
   texto_estatus_bomba[1] = "Bomba C2 activa";
     Serial.print("Bomba C2 activa");
  }else {
texto_estatus_bomba[1] = "Bomba C2 espera";
     Serial.print("Bomba C2 espera");
}  
  
estatus_bomba_municipal =digitalRead(Gpio_bomba_municipal); //Es negado por que es PULL UP
  if (estatus_bomba_municipal){ //negado por que es siempre a 5V al cerrar con tierra activa 
   texto_estatus_bomba[2] = "Bomba Mun activa";
     Serial.print("Bomba Mun activa");   
  }else {
texto_estatus_bomba[2] = "Bomba Mun espera";
     Serial.print("Bomba Mun espera");   
}  
  
  estatus_bomba_solares =digitalRead(Gpio_bomba_solares); //Es negado por que es PULL UP
  if (estatus_bomba_solares){ //negado por que es siempre a 5V al cerrar con tierra activa 
   texto_estatus_bomba[3] = "Bomba Sol activa";
     Serial.print("Bomba Sol activa");   
  }else {
texto_estatus_bomba[3] = "Bomba Sol espera";
     Serial.print("Bomba Sol espera");   
        } 



  
//codigo para programacion por calendario del apagado de bomba
if(apagado_nocturno_bomba_cisterna1){
  calendario_suspension_activo_cisterna1=1;
 texto_susp_estatus_bomba[0] = "<C1 SUSP POR CALENDARIO>";
     Serial.println("<C1 SUSP POR CALENDARIO>");   
         digitalWrite(Gpio_suspende_bomba_cisterna1, LOW);
//   sustitucion delay
//Codigo de retardo en milisegundos
// tiempo_actual=millis();while(millis() < tiempo_actual+100){ }
        }else {
  calendario_suspension_activo_cisterna1=0;

//codigo para estatus de boton suspension lo realizo mientras no este en calendario programado
   if (suspension_toma_cisterna1){
 texto_susp_estatus_bomba[0] = "<C1 SUSP POR USUARIO SM>";
      Serial.println("<C1 SUSP POR USUARIO SM>");   
         digitalWrite(Gpio_suspende_bomba_cisterna1, LOW);
     //   sustitucion delay
//Codigo de retardo en milisegundos
// tiempo_actual=millis();while(millis() < tiempo_actual+50){ }
        }else {
     
if (triger_cisterna1_vacia){
 texto_susp_estatus_bomba[0] = "<AP BOMBA C1 VACIA 0%>";
     Serial.println("<AP BOMBA C1 VACIA 0%>");   
         digitalWrite(Gpio_suspende_bomba_cisterna1, LOW);
}
else{
  texto_susp_estatus_bomba[0] = "";
  //simpre caeriaaqui no deberia actualizar aqui el estatus 
  //  digitalWrite(Gpio_suspende_bomba_cisterna1,HIGH );     
}
        } 
}


//codigo para programacion por calendario del apagado de bomba
if(apagado_nocturno_bomba_cisterna2){
  calendario_suspension_activo_cisterna2=1;
 texto_susp_estatus_bomba[1] = "<C2 SUSP POR CALENDARIO>";
      Serial.println("<C2 SUSP POR CALENDARIO>");   
         digitalWrite(Gpio_suspende_bomba_cisterna2, LOW);
//   sustitucion delay
//Codigo de retardo en milisegundos
// tiempo_actual=millis();while(millis() < tiempo_actual+100){ }
        }else {
  calendario_suspension_activo_cisterna2=0;

//codigo para estatus de boton suspension lo realizo mientras no este en calendario programado
   if (suspension_toma_cisterna2){
 texto_susp_estatus_bomba[1] = "<C2 SUSP POR USUARIO SM>";
      Serial.println("<C2 SUSP POR USUARIO SM>");   
 digitalWrite(Gpio_suspende_bomba_cisterna2, LOW);
     //   sustitucion delay
//Codigo de retardo en milisegundos
// tiempo_actual=millis();while(millis() < tiempo_actual+50){ }
        }else {

if (triger_cisterna2_vacia){
 texto_susp_estatus_bomba[1] = "<AP BOMBA C2 VACIA>";
      Serial.println("<AP BOMBA C2 VACIA>");   
 digitalWrite(Gpio_suspende_bomba_cisterna2, LOW);
}
else{
     texto_susp_estatus_bomba[1] = "";
     //simpre caeriaaqui no deberia actualizar aqui el estatus 
     // digitalWrite(Gpio_suspende_bomba_cisterna2,HIGH );     
  }       
  } 
}

  

//codigo para programacion por calendario del apagado de bomba  
if(apagado_nocturno_bomba_municipal){
  calendario_suspension_activo_municipal=1;
 texto_susp_estatus_bomba[2] = "<MUN SUSP POR CALEND>";
      Serial.println("<MUN SUSP POR CALEND>");   
 digitalWrite(Gpio_suspende_bomba_municipal, LOW);
  //   sustitucion delay
//Codigo de retardo en milisegundos
// tiempo_actual=millis();while(millis() < tiempo_actual+100){ }

        }else {
  calendario_suspension_activo_municipal=0;

//codigo para estatus de boton suspension lo realizo mientras no este en calendario programado
   if (suspension_toma_municipal){
 texto_susp_estatus_bomba[2] = "<MUN SUSP USUARIO SM>";
      Serial.println("<MUN SUSP USUARIO SM>"); 
 digitalWrite(Gpio_suspende_bomba_municipal, LOW);
   //   sustitucion delay
//Codigo de retardo en milisegundos
// tiempo_actual=millis();while(millis() < tiempo_actual+50){ }

        }else {
     texto_susp_estatus_bomba[2] = "";
     //simpre caeriaaqui no deberia actualizar aqui el estatus 
    //   digitalWrite(Gpio_suspende_bomba_municipal,HIGH ); 
        } 
}



//codigo para programacion por calendario del apagado de bomba  
if(apagado_nocturno_bomba_solares){
  calendario_suspension_activo_solares=1;
 texto_susp_estatus_bomba[3] = "<SOL SUSP POR CALEND>";
      Serial.println("<SOL SUSP POR CALEND>"); 
 digitalWrite(Gpio_suspende_bomba_solares, LOW);
  //   sustitucion delay
//Codigo de retardo en milisegundos
// tiempo_actual=millis();while(millis() < tiempo_actual+100){ }

        }else {
  calendario_suspension_activo_solares=0;

//codigo para estatus de boton suspension lo realizo mientras no este en calendario programado
   if (suspension_toma_solares){
 texto_susp_estatus_bomba[3] = "<SOL SUSP USUARIO SM>";
         digitalWrite(Gpio_suspende_bomba_solares, LOW);
   //   sustitucion delay
//Codigo de retardo en milisegundos
// tiempo_actual=millis();while(millis() < tiempo_actual+50){ }

        }else {
         //simpre caeriaaqui no deberia actualizar aqui el estatus 
         //digitalWrite(Gpio_suspende_bomba_solares, HIGH);
        texto_susp_estatus_bomba[3] = "";       
 
        } 
}
  
 


//lcd_display();


       
            }    //else de boton suspension total del sistema     
//FINCODIGCLOUD

long now =millis();
if(now-lastMsg>500)
{
lastMsg=now;
/*temp1++;
temp2++;
volts++;
lo paso de los sensores como prueba ya no incremento*/
String to_send=String(temp1)+","+String(temp2)+","+String(volts);
to_send.toCharArray(msg,25);
Serial1.print("Publicamos mensaje --> ");
Serial1.println(msg);
client.publish("values",msg);
}

}



void setup_wiffi(){

delay(10);
Serial.println();
Serial1.print("Conectando a: ");
Serial.println(ssid);

WiFi.begin(ssid,pasword);

while(WiFi.status()!=WL_CONNECTED){
delay(500);
Serial.print(".");
 }
Serial.println("");
Serial.println("Conectado a la RED WiFFi VSCODE");
Serial.println("Direccion IP:");
Serial.println(WiFi.localIP());

}

void callback(char* topic, byte* playload, unsigned int lenght){
String incoming="";
Serial.println("Mensaje recibido desde --> ");
Serial.println(topic);
Serial.println("");

for (int i=0; i<lenght; i++){
  incoming+=(char)playload[i];
  incoming.trim();
  Serial.println("Mensaje -->"+incoming);
if(incoming=="on"){
  digitalWrite(Gpio_suspende_bomba_cisterna1, HIGH);
}else{
  digitalWrite(Gpio_suspende_bomba_cisterna1, LOW);
} 
}

}


void reconnect(){

  while(!client.connected()){
  Serial.println("Intententando reconectar a MQTT.. ");
String clientId="esp32_";
clientId+=String(random(0xffff),HEX);


if (client.connect(clientId.c_str(),mqtt_user,mqtt_pass)){
  Serial.println("Conectado a MQTT");
  
  client.subscribe( "led1");
  client.subscribe( "led2");

}

  else{
  Serial.println("fallo conexion con error -->");
  Serial.println(client.state());
  Serial.println("Intentamos de nuevo en 5 segundos..");
  delay(5000);  
}

  }



}


//Funcion de LCD

void lcd_display(){ 

   int tam;

//texto_estatus_bomba[0]=String(var_cisterna_flot1C1)+String(var_cisterna_flot2C1)+String(var_cisterna_flot3C1)+String(var_cisterna_flot4C1)+String(var_cisterna_flot5C1);
  
  // Cisterna 1  
  tam=texto_estatus_niveles1.length()+texto_susp_estatus_bomba[0].length();
    lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(texto_estatus_niveles1+" "+texto_susp_estatus_bomba[0]);
    lcd.setCursor(0,1);
  lcd.print(texto_estatus_bomba[0]);
//   sustitucion delay
//Codigo de retardo en milisegundos
  delay(1000);
//  tiempo_actual=millis();while(millis() < tiempo_actual+1000){ }

if(texto_susp_estatus_bomba[0]!=""){ //solo hace el scroll si hay suspension por que es muy lento en la actualizacion
  for (int positionCounter = 0; positionCounter < tam; positionCounter++) {
    // scroll one position left:
    lcd.scrollDisplayLeft();  // o su variante lcd.scrollDisplayRight();
 delay(250);
  
  }

   }



  
  
  // Cisterna 2  
tam=texto_estatus_niveles2.length()+texto_susp_estatus_bomba[1].length();
    lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(texto_estatus_niveles2+" "+texto_susp_estatus_bomba[1]);  
    lcd.setCursor(0,1);
  lcd.print(texto_estatus_bomba[1]);
//   sustitucion delay
//Codigo de retardo en milisegundos
//    delay(1000);
 // tiempo_actual=millis();while(millis() < tiempo_actual+1000){ }
delay(1000);
    
// tiempo_actual=millis();while(millis() < tiempo_actual+1000){ }
if(texto_susp_estatus_bomba[1]!=""){ //solo hace el scroll si hay suspension por que es muy lento en la actualizacion

  for (int positionCounter = 0; positionCounter < tam; positionCounter++) {
    // scroll one position left:
    lcd.scrollDisplayLeft();  // o su variante lcd.scrollDisplayRight();
 delay(250);

   }

}



  

  // BOMBA SOLARES Y MUNICIPAL  
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(texto_estatus_bomba[2]+" "+texto_susp_estatus_bomba[2]);
  lcd.setCursor(0,1);
   lcd.print(texto_estatus_bomba[3]+" "+texto_susp_estatus_bomba[3]);
 // tiempo_actual=millis();while(millis() < tiempo_actual+1000){ }
delay(1000);
  tam=16;
  if(texto_susp_estatus_bomba[2]!="" or texto_susp_estatus_bomba[3]!="") {
    tam=32;
                                                                       
 // tiempo_actual=millis();while(millis() < tiempo_actual+1000){ }
  for (int positionCounter = 0; positionCounter < tam; positionCounter++) {
    // scroll one position left:
    lcd.scrollDisplayLeft();  // o su variante lcd.scrollDisplayRight();
 delay(250);
  }
   }

  
//Para que no se quede basura 
lcd.clear();
lcd.setCursor(0,0);
lcd.print("ACTUALIZANDO... ");
lcd.setCursor(0,1);
lcd.print("INFORMACION IOT");


  
  }

