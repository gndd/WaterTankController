/* 
  WaterTankController
  Control de Nivel de Tanqe de Agua con Sensor de Temperatura DS18B20.
  Reporte por MQTT a Home Assistant.

  Copyright (C) 2020  Guillermo Di Donato

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

  ================= 
  = Sensor DS18B20 =
  =================
  The BME680 is an environmental digital sensor that measures gas, pressure, 
  humidity and temperature.
  connections:
  - VCC -> 3V3
  - GND -> GND
  - SCL -> 10 (NodeMCU 10)

*/

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h> 
#include "mqtt.configuration.h"
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <OneWire.h>                
#include <DallasTemperature.h>
#include <Bounce2.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_I2CDevice.h>

// ========== Inicio zona de parametrización del WaterTankController =============================

const unsigned int tiempoReporteMQTTBroker = 30000; // Tiempo de reporte al Broker MQTT.
const unsigned int timepoMaximoBombaEncendida = 720000; // Tiempo máximo en que la Bomba Centrífuga estará encendida (12 minutos).
const unsigned int timepoEntreEncendidosBomba = 1200000; // Tiempo permitido entre encendidos de la Bomba Centrífuga (20 minutos).

unsigned int ledBlinkONInterval = 50; // Tiempo de ON de Led Blink.
unsigned int ledBlinkOFFInterval = 3000; // Tiempo de OFF de Led Blink.

const int oneWirePin = 10; // Data wire is plugged into pin 10 on the NodemCu.
const int pinFlotanteTanqueCisterna = D6; // Pin control del flotnte del tanque cisterna.
const int pinFlotanteTanqueElevado = D7; // Pin control del flotante del tanque elevado.
const int pinDesbordeTanqueElevado = D0; // Pin control del sensor de desborde del tanque elevado.
const int pinRelayBombaCentrifuga = D5; // Pin control Relay del Contactor.

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

// ========== Fin zona de parametrización del WaterTankController ================================

// Estados del Sistema
#define LLENANDO  0
#define VACIANDO 1

WiFiManager wm;
WiFiClient espClient;
PubSubClient client(espClient);
Bounce flotanteTanqueElevado = Bounce(); 
Bounce desbordeTanqueElevado = Bounce(); 
Bounce flotanteTanqueCisterna = Bounce(); 
OneWire oneWireBus(oneWirePin);
DallasTemperature ds18b20(&oneWireBus);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

const char* mqtt_user = MQTT_USER;
const char* mqtt_password = MQTT_PASSWORD;
const char* mqtt_topic = MQTT_TOPIC;
const char* mqtt_clientId = MQTT_CLIENTID;
IPAddress mqtt_server_ip(MQTT_SERVER_IP);
int mqtt_server_port = MQTT_SERVER_PORT;

bool ledState = HIGH; 
bool nivelTanqueElevado, estadoDesbordeTanqueElevado, nivelTanqueCisterna, estadoBombaCentrifuga;
byte estado = VACIANDO;
byte DeviceHeapFragmentation;
float temperature;
unsigned int signalQuality, DeviceFreeHeap, i; 
unsigned long currentTime, previousReporteMQTTBroker, previousTiempoBombaEncendida, ultimoEncendidoBomba, previousTime4, previousTime5, DeviceUptime;
String DeviceResetReason;

// Helper functions declarations
void reconnect();
void ReadSensors();
void ReadWiFiSignalLevel();
void callback(char* p_topic, byte* p_payload, unsigned int p_length);
void ledBlink();
void PublishData();
bool readFlotanteTanqueElevado();
bool readDesbordeTanqueElevado();
bool readFlotanteTanqueCisterna();
bool readEstadoBombaCentrifuga();
void setBombaCentrifuga (bool Switch);
void updateDisplay();
void readESPInfo ();

void setup() {
  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP    
  Serial.begin(115200);

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3C for 128x64
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  delay(2000);
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE);

  ds18b20.begin(); // Inicializo el Sensor DS18B20.
  ds18b20.setResolution(12);
    
  //reset settings - wipe credentials for testing
  //wm.resetSettings();
  
  wm.setConfigPortalBlocking(false);

  //automatically connect using saved credentials if they exist
  //If connection fails it starts an access point with the specified name
  if(wm.autoConnect("WaterTankController")){
    Serial.println("Connected to WiFi!");
  }
  else {
    Serial.println("Configportal running");
  }

  client.setServer(mqtt_server_ip, mqtt_server_port);
  client.setCallback(callback);
  
  pinMode(LED_BUILTIN, OUTPUT); // Uses LED_BUILTIN to find the pin with the internal LED
 
  flotanteTanqueElevado.attach(pinFlotanteTanqueElevado, INPUT_PULLUP); // Attach the debouncer to a pin with INPUT_PULLUP mode
  flotanteTanqueElevado.interval(50); // // Use a debounce interval of 50 milliseconds
  desbordeTanqueElevado.attach(pinDesbordeTanqueElevado, INPUT_PULLDOWN_16); // Attach the debouncer to a pin with INPUT_PULLUP mode
  desbordeTanqueElevado.interval(50); // // Use a debounce interval of 50 milliseconds
  flotanteTanqueCisterna.attach(pinFlotanteTanqueCisterna, INPUT_PULLUP); // Attach the debouncer to a pin with INPUT_PULLUP mode
  flotanteTanqueCisterna.interval(50); // // Use a debounce interval of 50 milliseconds
  pinMode(pinRelayBombaCentrifuga, OUTPUT); // Rele Contactor Bomba Centrifuga
  digitalWrite(pinRelayBombaCentrifuga, HIGH); // Inicializamos el Rele de la Bomba Centrifuga como APAGADA.

  temperature = 0;
  signalQuality = 0;
  previousReporteMQTTBroker = millis();
  previousTiempoBombaEncendida = millis();
  ultimoEncendidoBomba = millis();
  previousTime4 = millis();
  previousTime5 = millis();
  i = 500;
}

void loop() {
  wm.process();

  if (!client.connected() && (WiFi.status() == WL_CONNECTED) && (i >= 500))
  {
    reconnect();
    i = 0;
  }
  i++;

  client.loop();

  // Leemos los estados de los Tanques.
  nivelTanqueElevado = readFlotanteTanqueElevado();
  estadoDesbordeTanqueElevado = readDesbordeTanqueElevado();
  nivelTanqueCisterna = readFlotanteTanqueCisterna();
  estadoBombaCentrifuga = readEstadoBombaCentrifuga();
  // Actualizamos el Display.
  updateDisplay();

  currentTime = timepoEntreEncendidosBomba + millis();

  if ((nivelTanqueElevado == 0) && (estadoDesbordeTanqueElevado == 1) && (nivelTanqueCisterna == 0) && (currentTime - previousTiempoBombaEncendida <= timepoMaximoBombaEncendida) && (currentTime - ultimoEncendidoBomba >= timepoEntreEncendidosBomba)) // Situación de encendido de Bomba Centrífuga.
    {
      if (estado == VACIANDO)
      {
        setBombaCentrifuga (true); // Encedemos la Bomba Centrifuga.
        estado = LLENANDO;
      }
    } else
    {
      previousTiempoBombaEncendida = currentTime;
      if (estado == LLENANDO)
      {
        setBombaCentrifuga (false); // Apagamos la Bomba Centrifuga.
        ultimoEncendidoBomba = currentTime;
        estado = VACIANDO;
      }
    }
  
  if (currentTime - previousReporteMQTTBroker >= tiempoReporteMQTTBroker) // Timer1 para el reporte de "KEEP-ALIVE" al Broker.
  {
    ReadSensors();
    ReadWiFiSignalLevel();
    readESPInfo();
    PublishData();
    previousReporteMQTTBroker = currentTime;
  }
  ledBlink();

}

bool readFlotanteTanqueElevado()
{
  flotanteTanqueElevado.update();
  bool temp = flotanteTanqueElevado.read();
  return temp;
}

bool readDesbordeTanqueElevado()
{
  desbordeTanqueElevado.update();
  bool temp = desbordeTanqueElevado.read();
  return temp;
}

bool readFlotanteTanqueCisterna()
{
  flotanteTanqueCisterna.update();
  bool temp = flotanteTanqueCisterna.read();
  return temp;
}

bool readEstadoBombaCentrifuga()
{
  bool temp;
  temp = digitalRead(pinRelayBombaCentrifuga);
  return temp;
}

void setBombaCentrifuga (bool Switch) // Función de encendido y apagado de la Bomba Centrífuga.
{
  if (Switch == true)
  {
    digitalWrite(pinRelayBombaCentrifuga, LOW); // Encedemos la Bomba Centrifuga.
  }
  else
  {
    digitalWrite(pinRelayBombaCentrifuga, HIGH); // Apagamos la Bomba Centrifuga.
  }
}

//Function to connect to MQTT Broker.
void reconnect()
{
  // Loop until we're reconnected
  if (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(mqtt_clientId, mqtt_user, mqtt_password)) {
      Serial.println("Connected to MQTT Broker!");
    } else {
      Serial.print("Failed to connect to MQTT Broker, rc=");
      Serial.println(client.state());
    }
  }
}

// Función para la lectura de los sensores de NodeMCU.
void ReadSensors()
{
  ds18b20.requestTemperatures(); // Send the command to get temperatures
  temperature = ds18b20.getTempCByIndex(0);
}

void updateDisplay()
{
  display.clearDisplay();

 // Parseo el estado del Desborde del Tanque Elevado.
  if (estadoDesbordeTanqueElevado == LOW)
  {
    display.setCursor(0, 0);
    display.setTextSize(2);
    display.println("Desborde!");
  } else if (estado == 0) // Parseo el estado del sistema
  {
    display.setCursor(0, 0);
    display.setTextSize(2);
    display.println("Llenando");
  } else if (estado == 1)
  {
    display.setCursor(0, 0);
    display.setTextSize(2);
    display.println("Vaciando");
  } else
  {
    display.setCursor(0, 0);
    display.setTextSize(2);
    display.println("Error!");
  }

  // Parseo el estado del Tanque Cisterna.
  if (nivelTanqueCisterna == HIGH)
  {
    display.drawRoundRect(0, 34, 40, 30, 2, WHITE);
  }
  else
  {
    display.fillRoundRect(0, 34, 40, 30, 2, WHITE);
  }
  
  // Parseo el estado de la Bomba Centrifuga.
  if (estadoBombaCentrifuga == HIGH)
  {
    display.drawCircle(64, 45, 10, WHITE);
  } else
  {
    display.fillCircle(64, 45, 10, WHITE);
  }
  
  // Parseo el estado del Tanque Elevado.
  if (nivelTanqueElevado == HIGH)
  {
    display.fillRoundRect(88, 24, 40, 30, 2, WHITE);
  } else
  {
    display.drawRoundRect(88, 24, 40, 30, 2, WHITE);
  }

  display.drawLine(40, 45, 54, 45, WHITE);
  display.drawLine(74, 45, 88, 45, WHITE);
  // Escribimos la temperatura
  display.setCursor(88, 57);
  display.setTextSize(1);
  display.println(temperature);
  display.display(); 
}

// Función para la lectura de la intensidad de Señal de NodeMCU.
void ReadWiFiSignalLevel()
{
  //Lectura de la intensidad de señal WiFi
   long rssi = WiFi.RSSI();
   // dBm to Signal Quality [%]:
    if(rssi <= -100)
        signalQuality = 0;
    else if(rssi >= -50)
        signalQuality = 100;
    else
        signalQuality = 2 * (rssi + 100);
}

void readESPInfo ()
{
  DeviceUptime = millis(); // Device Uptime
  DeviceResetReason = ESP.getResetReason(); // Returns a String containing the last reset reason in human readable format.
  DeviceFreeHeap = ESP.getFreeHeap(); // Returns the free heap size.
  DeviceHeapFragmentation = ESP.getHeapFragmentation(); // Returns the fragmentation metric (0% is clean, more than ~50% is not harmless)
}

// function called when a MQTT message arrived
void callback(char* p_topic, byte* p_payload, unsigned int p_length)
{
  // Esta funcion se ejecuta cuando recibimos un mensaje MQTT.
}

//  function called to blink the blue LED without delay.
void ledBlink() {
  if (ledState == LOW) {
    if (currentTime - previousTime4 >= ledBlinkONInterval)
    {
      previousTime4 = currentTime;
      ledState = HIGH;  // Note that this switches the LED *off*
      digitalWrite(LED_BUILTIN, ledState);
    } 
  } else {
    if (currentTime - previousTime5 >= ledBlinkOFFInterval)
    {
      previousTime5 = currentTime;
      ledState = LOW;  // Note that this switches the LED *on*
      digitalWrite(LED_BUILTIN, ledState);
    } 
  }
}

// function called to publish data to the Broker.
void PublishData()
{
  // create a JSON object
  StaticJsonDocument<256> doc;

  // Parseo el estado.
  if (estado == 0)
  {
    doc["estado"] = "Llenando";
  }else if (estado == 1)
  {
    doc["estado"] = "Vaciando";
  }else
  {
    doc["estado"] = "Error";
  }
  
  // Parseo el estado del Tanque Elevado.
  if (nivelTanqueElevado == HIGH)
  {
    doc["TE"] = "Lleno";
  }else
  {
    doc["TE"] = "Vacio";
  }

  // Parseo el estado del Desborde del Tanque Elevado.
  if (estadoDesbordeTanqueElevado == LOW)
  {
    doc["D"] = "true";
  }else
  {
    doc["D"] = "false";
  }

  // Parseo el estado del Tanque Cisterna.
  if (nivelTanqueCisterna == HIGH)
  {
    doc["TC"] = "Vacio";
  }else
  {
    doc["TC"] = "Lleno";
  }

  // Parseo el estado de la Bomba Centrifuga.
  if (estadoBombaCentrifuga == HIGH)
  {
    doc["BC"] = "off";
  }else
  {
    doc["BC"] = "on";
  }

  // DS18B20
  doc["T"] = (String)temperature;
  //NodemCu
  doc["SQ"] = (String)signalQuality;
  doc["UP"] = DeviceUptime;
  doc["RST"] = DeviceResetReason;
  doc["FH"] = DeviceFreeHeap;
  doc["HF"] = DeviceHeapFragmentation;

  char buffer[256];
  serializeJson(doc, buffer);
  Serial.println(buffer);
  client.publish(MQTT_TOPIC, buffer, true);
  yield();
}