#include <WiFi.h>               // Libreria Wifi
#include <HTTPClient.h>         // Libreria solicitudes HTTP
#include <DHT.h>                // Libreria DHT22
#include <MQ135.h>              // Libreria MQ135
#include <HardwareSerial.h>     // Libreria comunicacion Serial por puertos UART
#include <Wire.h>               // Libreria de manejo I2C
#include <LiquidCrystal_I2C.h>  // Libreria LCD_I2C

// Objeto Serial para SIM800L
HardwareSerial sim800(1);     // Usamos UART1 del ESP32
// Objeto Serial para RS485 del sensor NPK
HardwareSerial RS485Serial(2); // Usamos UART2 del ESP32
// Se establece el tamaño del lcd
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Comandos para leer valores de NPK
const byte nitro[]  = {0x01, 0x03, 0x00, 0x1E, 0x00, 0x01, 0xE4, 0x0C};
const byte phosp[]  = {0x01, 0x03, 0x00, 0x1F, 0x00, 0x01, 0xB5, 0xCC};
const byte potas[]  = {0x01, 0x03, 0x00, 0x20, 0x00, 0x01, 0x85, 0xC0};

byte nitroValue;
byte phospValue;
byte potassValue;

// Buffer de recepción
byte responseBuffer[7];

// Pines de los sensores
#define PULSADOR1 5               // Pulsador integrado en la placa en el pin5
#define DHTPIN 4                  // DHT22 en el pin 4
#define DHTTYPE DHT22             
#define AM2305B_PIN 27            // AM2305B en el pin 27
#define AM2305B_TYPE DHT22        // AM2305B se usa como DHT22 (misma compatibilidad) 
#define RELAY_DHT_RESET_PIN 25    // Rele de reinicio del DHT22 en el pin 25
#define RELAY_VALVE_PIN 26        // Rele de activacion de la electrovalvula en el pin 26
#define DE_RE_PIN 15              // DE y RE conectados al mismo pin 15
#define RX_PIN 17                 // RO del RS485 en el pin 17
#define TX_PIN 16                 // DI del RS485 en el pin 16
#define MQ135_PIN 14              // Datos del MQ135 en el pin 35
#define UV_PIN 33                 // Datos del sensor UV en el pin 33
#define WATER_LEVEL_PIN 13        // Sensor de nivel en el pin 13
#define FLOW_SENSOR_PIN 32        // Sensor de flujo en el pin 32
#define SOIL_SENSOR1_PIN 36       // Sensor de suelo en el pin 36
#define SOIL_SENSOR2_PIN 39       // Sensor de suelo en el pin 39
#define SOIL_SENSOR3_PIN 34       // Sensor de suelo en el pin 34
#define SOIL_SENSOR4_PIN 35       // Sensor de suelo en el pin 35
#define rxPin 19                  // Conectar el pin 19 de ESP32 al RX del SIM800L
#define txPin 18                  // Conectar el pin 18 de ESP32 al TX del SIM800L

MQ135 mq135_sensor(MQ135_PIN);

// Configuración WiFi
const char* ssid = "ZTE-cf9ffcs";   // Reemplaza con el nombre de tu red WiFi
const char* password = "ec8a4ccf";  // Reemplaza con tu contraseña WiFi

// URL del servidor PHP
const char* serverURL = "https://ecocic.econotec.app/receive_data.php";

// Configuración del sensor DHT22 y AM2305B
DHT dht(DHTPIN, DHTTYPE);
DHT am2305b(AM2305B_PIN, AM2305B_TYPE);

// Variables globales
volatile int flowPulseCount = 0;  // Para el sensor de flujo
float calibrationFactor = 4.5;    // Factor de calibración para el flujómetro
float temperature, humidity;
float humy1 = 0;
int soil_humidity1 = 0;
int soil_humidity2 = 0;
int soil_humidity3 = 0;
int soil_humidity4 = 0;
int water_level = 0;
int flow_rate = 0;
int mq135_value = 0;
float co2_concentration = 0;    // Concentración de CO2 calculada a partir del MQ135
float co_concentration = 0;     // Concentración de CO calculada a partir del MQ135
int cicloContador = 0;          // Contador de ciclos
const int ciclosParaEnviar = 5; // Mandar mensaje despues de [especificado] ciclos

// Configuraciones para el SIM800L EVB encargado de los mensajes SMS
String mensaje;
#define numero_telefonico "59167016581"       // Número de teléfono
const String serie = "123453434";             // Número de serie del dispositivo
const String ubicacion = "12.3456,-76.5432";  // Ubicación 

// Funciones auxiliares
void IRAM_ATTR flowPulseCounter();                                            // Cuenta los pulsos del flujómetro
void reinicioDHT22();                                                         // Activa el relé del DHT22 para reiniciarlo por interrupcion
float calculateAverageSoilHumidity(float h1, float h2, float h3, float h4);   // Calcula la humedad promedio de los sensores de suelo
byte getSensorValue(const byte* command, const char* name);                   // Obtiene informacion del NPK

void setup() {
  // Configurar pines
  pinMode(WATER_LEVEL_PIN, INPUT);        // Configurar el pin del sensor de nivel de agua como entrada
  pinMode(FLOW_SENSOR_PIN, INPUT);        // Configurar el pin del sensor de flujo como entrada
  pinMode(SOIL_SENSOR1_PIN, INPUT);       // Configurar el pin del sensor de suelo 1 como entrada
  pinMode(SOIL_SENSOR2_PIN, INPUT);       // Configurar el pin del sensor de suelo 2 como entrada
  pinMode(SOIL_SENSOR3_PIN, INPUT);       // Configurar el pin del sensor de suelo 3 como entrada
  pinMode(SOIL_SENSOR4_PIN, INPUT);       // Configurar el pin del sensor de suelo 4 como entrada
  pinMode(RELAY_DHT_RESET_PIN, OUTPUT);   // Configurar el pin del relé como salida
  pinMode(RELAY_VALVE_PIN, OUTPUT);       // Configurar el pin del relé como salida
  pinMode(PULSADOR1, INPUT);              // Configurar el pin del pulsador como entrada
  pinMode(DE_RE_PIN, OUTPUT);             // Configurar el pin DE_RE del RS485 como salida

  // Inicializa los sensores de humedad y temperatura DHT22 y AM2305B
  dht.begin();
  am2305b.begin();

  // Se establece la velocidad de comunicación serial en 115200
  Serial.begin(115200);

  // Inicializa el lcd
  lcd.clear();
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.print("INIAF-CalibraciónSensor ");

  // Configura interrupción para el sensor de flujo y el reinicio del DHT22
  attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), flowPulseCounter, RISING);
  attachInterrupt(digitalPinToInterrupt(PULSADOR1), reinicioDHT22, FALLING);
  
  // Inicializa la comunicacion serial del SIM800L EVB y del RS485 del sensor NPK
  sim800.begin(9600, SERIAL_8N1, rxPin, txPin);         // UART1 para el SIM800L
  RS485Serial.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);  // UART2 para el RS485

  digitalWrite(DE_RE_PIN, LOW);               // Modo recepción inicial del sensor NPK
  digitalWrite(RELAY_DHT_RESET_PIN, HIGH);    // Mantiene apagado el rele de reinicio del DHT22
  digitalWrite(RELAY_VALVE_PIN, HIGH);        // Mantiene apagado el rele de la electrovalvula

  /*
  // Conexión WiFi
  WiFi.begin(ssid, password);
  Serial.print("Conectando a WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\nConectado a WiFi.");
  */

  delay(12000);   //Retardo para asegurar que el SIM800L se conecte a la red de telefonia
}

void loop() {
  unsigned long startTime = millis(); // Guarda el tiempo de inicio

  // Apagar el rele de reinicio del DHT22
  digitalWrite(RELAY_DHT_RESET_PIN, HIGH);
  // Leer datos del sensor DHT22
  float temperature_dht22 = dht.readTemperature();
  float humidity_dht22 = dht.readHumidity();
  // Leer datos del AM2305B
  float humidityAM2305B = am2305b.readHumidity();
  float temperatureAM2305B = am2305b.readTemperature();

  // Validar los datos del DHT22
  if (isnan(temperature_dht22) || isnan(humidity_dht22)) {
    Serial.println("Error leyendo del sensor DHT22");
    return;
  }

  // Leer sensores de humedad del suelo
  soil_humidity1 = analogRead(SOIL_SENSOR1_PIN);
  soil_humidity2 = analogRead(SOIL_SENSOR2_PIN);
  soil_humidity3 = analogRead(SOIL_SENSOR3_PIN);
  soil_humidity4 = analogRead(SOIL_SENSOR4_PIN);

  // Leer otros sensores (como MQ135, UV, nivel de agua, y flujo)
  humidity = dht.readHumidity();
  temperature = dht.readTemperature();
  float rzero = mq135_sensor.getRZero();
  float correctedRZero = mq135_sensor.getCorrectedRZero(temperature, humidity);
  float resistance = mq135_sensor.getResistance();
  float ppm = mq135_sensor.getPPM();
  float correctedPPM = mq135_sensor.getCorrectedPPM(temperature, humidity);

  // Leer el estado del nivel de agua
  water_level = digitalRead(WATER_LEVEL_PIN);
  if (digitalRead(WATER_LEVEL_PIN) == LOW) {
    Serial.println("Nivel del agua bajo");
  } else {
    Serial.println("Nivel del agua adecuado");
  }

  // Guardar el valor de contado de pulsos del flujometro
  float flowRate = (flowPulseCount / calibrationFactor);
  Serial.print("Caudal (L/min): ");
  Serial.println(flowRate); //  entre 10
  flowPulseCount = 0;

  // Estimaciones de calidad del aire y concentraciones de CO y CO2
  co2_concentration = map(mq135_value, 0, 4095, 0, 5000); // Ajusta según el rango deseado
  co_concentration = map(mq135_value, 0, 4095, 0, 2000);  // Ajusta según el rango deseado

  // Calcular la humedad promedio del suelo
  float avg_soil_humidity = calculateAverageSoilHumidity(soil_humidity1, soil_humidity2, soil_humidity3, soil_humidity4);

  // Activar o desactivar el relé según la humedad promedio
  if (avg_soil_humidity > 500) { // 2800
    digitalWrite(RELAY_VALVE_PIN, LOW); // Encender relé
  } else {
    digitalWrite(RELAY_VALVE_PIN, HIGH); // Apagar relé
  }

  // Determinar el estado del relé de la electrovalvula
  String relayState = (avg_soil_humidity > 500) ? "encendido" : "apagado";

  // Enviar solicitudes y leer valores del NPK
  nitroValue = getSensorValue(nitro, "Nitrogeno");
  delay(250);
  phospValue = getSensorValue(phosp, "Fosforo");
  delay(250);
  potassValue = getSensorValue(potas, "Potasio");
  delay(250);

  // Mostrar resultados
  Serial.printf("Soil Nitrogeno: %d mg/kg\n", nitroValue);
  Serial.printf("Soil Fosforo: %d mg/kg\n", phospValue);
  Serial.printf("Soil Potasio: %d mg/kg\n\n", potassValue);

  delay(151);
    
  // Mostrar todos los datos en el monitor serie
  Serial.println("Temperatura DHT22: " + String(temperature_dht22));
  Serial.println("Humedad DHT22: " + String(humidity_dht22));
  Serial.println("Temperatura AM2305B: " + String(temperatureAM2305B));
  Serial.println("Humedad AM2305B: " + String(humidityAM2305B));
  Serial.println("Humedad Suelo 1: " + String(soil_humidity1));
  Serial.println("Humedad Suelo 2: " + String(soil_humidity2));
  Serial.println("Humedad Suelo 3: " + String(soil_humidity3));
  Serial.println("Humedad Suelo 4: " + String(soil_humidity4));
  Serial.println("Promedio Suelo: " + String(avg_soil_humidity));
  if (digitalRead(WATER_LEVEL_PIN) == LOW) {
    Serial.println("Nivel del agua bajo");
  } else {
    Serial.println("Nivel del agua adecuado");
  }
  Serial.println("Flujo (L/min):: " + String(flowRate));
  Serial.println("MQ135 : " + String(mq135_value));
  Serial.println("MQ135 (Calidad del aire): " + String(correctedPPM / 100000));
  Serial.println("Concentración de CO2: " + String(co2_concentration) + " ppm");
  Serial.println("Concentración de CO: " + String(co_concentration) + " ppm");
  Serial.println("Estado del Relé: " + relayState);

  // Serialización de datos para el envío
  String url = String(serverURL) +
                "?serie=" + serie +
               "&temperatura_dht22=" + String(temperature_dht22) +
               "&humedad_dht22=" + String(humidity_dht22) +
               "&temperatura_am2305b=" + String(temperatureAM2305B) +
               "&humedad_am2305b=" + String(humidityAM2305B) +
               "&humedad_suelo1=" + String(soil_humidity1) +
               "&humedad_suelo2=" + String(soil_humidity2) +
               "&humedad_suelo3=" + String(soil_humidity3) +
               "&humedad_suelo4=" + String(soil_humidity4) +
               "&nivel_agua=" + String(water_level) +
               "&flujo=" + String(flowRate) +
               "&mq135=" + String(mq135_value) +
               "&co2=" + String(correctedPPM / 100000) +
               "&co=" + String(correctedPPM / 100000) +
               "&estado_rele=" + relayState +
               "&ubicacion=" + ubicacion;

  /*
  if (WiFi.status() == WL_CONNECTED){
    // Realizar la solicitud HTTP
    HTTPClient http;
    http.begin(url);
    int httpResponseCode = http.GET();

    // Verificar la respuesta del servidor
    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.println("Respuesta del servidor: " + response);
    } else {
      Serial.println("Error en la solicitud: " + String(httpResponseCode));
    }
    http.end();
  } else {
    Serial.println("WiFi desconectado, reconectando...");
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
      delay(1000);
      Serial.print(".");
    }
  }*/

  unsigned long executionTime = millis() - startTime;
  Serial.printf("Tiempo total de ejecución: %lu ms\n", executionTime);

  // Generar el mensaje para mandarlo por SMS
  mensaje = "serie=" + String(serie) + "&";;
  mensaje += "temperatura_dht22=" + String(temperature_dht22) + "&";
  mensaje += "humedad_dht22=" + String(humidity_dht22) + "&";
  mensaje += "temperatura_am2395b=" + String(temperatureAM2305B) + "&";
  mensaje += "humedad_am2305b=" + String(humidityAM2305B) + "&";
  mensaje += "humedad_suelo1=" + String(soil_humidity1) + "&";
  mensaje += "humedad_suelo2=" + String(soil_humidity2) + "&";
  mensaje += "humedad_suelo3=" + String(soil_humidity3) + "&";
  mensaje += "humedad_suelo4=" + String(soil_humidity4) + "&";
  mensaje += "humedad_promedio=" + String(avg_soil_humidity) + "&";
  if (digitalRead(WATER_LEVEL_PIN) == LOW) {
    mensaje += "nivel_del_agua=bajo&";
  } else {
    mensaje += "nivel_del_agua=adecuado&";
  }
  mensaje += "flujo=" + String(flowRate) + "&";
  mensaje += "mq135=" + String(mq135_value) + "&";
  mensaje += "co2=" + String(correctedPPM / 100000) + "&";
  mensaje += "co=" + String(correctedPPM / 100000) + "&";
  mensaje += "estado_rele=" + relayState + "&";
  mensaje += "ubicacion=" + String(ubicacion);
  
  cicloContador++; // Incrementar el contador de ciclos

  if (cicloContador >= ciclosParaEnviar) {
    EnviarMensaje(numero_telefonico, mensaje);  // Manda el SMS
    cicloContador = 0;                          // Reiniciar el contador
  }

  delay(1000); 
}

void EnviarMensaje(String StrNumTelf, String StrMensaje) 
{
    String StrTramaAT = "AT+CMGS=\"+" + StrNumTelf + "\"\r";
    Serial.println("Enviando el Mensaje ...");    // Mostrar este mensaje por el monitor serial
    sim800.print("AT+CMGF=1\r");                  // Coloca el modulo en modo de SMS
    delay(100);
    sim800.print(StrTramaAT);                     // Establece el numero de celular destino
    delay(500);
    sim800.print(StrMensaje);                     // Este es el mensaje que se envia por SMS, tratar de que no sea demasiado extenso
    delay(500);
    sim800.print((char)26);                       // Paso requerido por datasheet
    delay(500);
    sim800.println();
    Serial.println("Texto Enviado.");
    delay(500); 
}

void reinicioDHT22()
{
  digitalWrite(RELAY_DHT_RESET_PIN, LOW);         // Conmutar relé
  Serial.println("Se encendió el RELE");
}

byte getSensorValue(const byte* command, const char* name) {
  // Habilitar transmisión
  digitalWrite(DE_RE_PIN, HIGH);
  delayMicroseconds(10); // Pequeña espera

  // Enviar comando
  RS485Serial.write(command, 8);
  RS485Serial.flush(); // Esperar a que termine la transmisión

  // Habilitar recepción
  digitalWrite(DE_RE_PIN, LOW);
  delay(10); // Dar tiempo al sensor para responder

  // Leer respuesta
  if (RS485Serial.available() >= 7) {
    for (byte i = 0; i < 7; i++) {
      responseBuffer[i] = RS485Serial.read();
      Serial.printf("%02X\t", responseBuffer[i]);
    }
    Serial.println();
    return responseBuffer[4]; // El valor del sensor está en el byte 4
  } else {
    Serial.printf("Error leyendo %s\n", name);
    return 0; // Si no se reciben suficientes datos, devolver 0
  }
}

float calculateAverageSoilHumidity(float h1, float h2, float h3, float h4) {
    float sum = 0.0;
    int count = 0;

    if (h1 > 0) { sum += h1; count++; }
    if (h2 > 0) { sum += h2; count++; }
    if (h3 > 0) { sum += h3; count++; }
    if (h4 > 0) { sum += h4; count++; }

    return (count > 0) ? (sum / count) : 0; // Evita división por 0
}

void IRAM_ATTR flowPulseCounter() {
  flowPulseCount++;
}
