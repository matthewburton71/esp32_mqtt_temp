/*********
 Matthew Burton
*********/

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <AutoConnect.h>
#include <PubSubClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// Data wire is plugged TO GPIO 4
#define ONE_WIRE_BUS 15

#define HOSTNAME_BASE "esp32-"
const int WAIT = 10 * 1000; // 10 sec

const int STATUS_LED = LED_BUILTIN;

// Add your MQTT Broker IP address, example:
const char *mqtt_server = "192.168.86.5";

String hostName;
String pubTopicBase;
String subTopic;

// Setup a oneWire instance to communicate with any OneWire devices
OneWire oneWire(ONE_WIRE_BUS);
// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);

// We'll use this variable to store a found device address
DeviceAddress tempDeviceAddress;

WiFiClient espClient;
PubSubClient mqttClient(espClient);
WebServer Server;
AutoConnect Portal(Server);
AutoConnectConfig Config;

long lastMsg = 0;
// Number of temperature devices found
int numberOfDevices;
float temperature;

// LED Pin
const int ledPin = LED_BUILTIN;
boolean led = false;

//**************************
int discoverOneWireDevices(void)
{
  byte addr[8];
  byte i;
  int count = 0;

  Serial.println("Looking for OneWire devices...");
  while (oneWire.search(addr))
  {
    Serial.print("Found \'1-Wire\' device with address: ");
    for (i = 0; i < 8; i++)
    {
      Serial.print("0x");
      if (addr[i] < 16)
      {
        Serial.print('0');
      }
      Serial.print(addr[i], HEX);
      if (i < 7)
      {
        Serial.print(", ");
      }
    }
    if (OneWire::crc8(addr, 7) != addr[7])
    {
      Serial.println("CRC is not valid!");
      return 0;
    }
    Serial.println();
    count++;
  }
  Serial.println("That is it");
  oneWire.reset_search();
  return count;
}

void rootPage()
{
  char content[] = "Hello, world";
  Server.send(200, "text/plain", content);
}


void callback(char *topic, byte *message, unsigned int length)
{
  Serial.print("Message arrived on topic: ");
  Serial.print(topic);
  Serial.print(". Message: ");
  String messageTemp;

  for (int i = 0; i < length; i++)
  {
    Serial.print((char)message[i]);
    messageTemp += (char)message[i];
  }
  Serial.println();

  // Feel free to add more if statements to control more GPIOs with MQTT

  // If a message is received on the topic esp32/output,
  //  you check if the message is either "on" or "off".
  // Changes the output state according to the message
  if (String(topic) == subTopic)
  {
    Serial.print("Changing output to ");
    if (messageTemp == "on")
    {
      Serial.println("on");
      digitalWrite(ledPin, HIGH);
    }
    else if (messageTemp == "off")
    {
      Serial.println("off");
      digitalWrite(ledPin, LOW);
    }
  }
}
//**************************

void setup()
{
  delay(1000);
  Serial.begin(115200);

  // AutoConfig
  Config.title = "Temp Probes";
  // Make the name using the last 2 dig the MAC Address.
  hostName = HOSTNAME_BASE + WiFi.macAddress().substring(15);
  Config.hostName = hostName;
  Config.tickerPort = STATUS_LED;
  Config.ticker = true;
  Config.tickerOn = HIGH;
  Portal.config(Config);

  Server.on("/", rootPage);
  if (Portal.begin())
  {
    Serial.println("HTTP server:" + WiFi.localIP().toString());
  }
  Serial.print("MAC address: ");
  Serial.println(WiFi.macAddress());
  Serial.print("DNS Name: ");
  Serial.println(Config.hostName);

  // Start up the library
  sensors.begin();

  // Grab a count of devices on the wire
  // numberOfDevices = sensors.getDeviceCount();
  numberOfDevices = discoverOneWireDevices();

  pubTopicBase = "home/" + hostName + "/temperature/";
  //subTopic = "home/" + hostName + "/output";
  subTopic = "home/esp32/output";
  mqttClient.setServer(mqtt_server, 1883);
  mqttClient.setCallback(callback);

  pinMode(ledPin, OUTPUT);
}

void mqttConnect()
{
  // Loop until we're reconnected
  while (!mqttClient.connected())
  {
    // flash the led
    led = !led;
    digitalWrite(ledPin, (led) ? HIGH : LOW);

    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (mqttClient.connect(hostName.c_str()))
    {
      Serial.println("connected");
      // Subscribe
      mqttClient.subscribe(subTopic.c_str());
    }
    else
    {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
  // turn led off
  led = false;
  digitalWrite(ledPin, LOW);
}

void loop()
{
  Portal.handleClient();

  if (!mqttClient.connected())
  {
    mqttConnect();
  }
  mqttClient.loop();

  long now = millis();
  if (now - lastMsg > WAIT)
  {
    Serial.println("Poll temps.");

    lastMsg = now;

    // Temperature in Celsius
    sensors.requestTemperatures(); // Send the command to get temperatures

    if (numberOfDevices == 0)
    { // no temp probes found.
      led = true;
      digitalWrite(ledPin, HIGH);
    }
    else
    {
      led = false;
      digitalWrite(ledPin, LOW);
    }
    // Loop through each device, print out temperature data
    for (int i = 0; i < numberOfDevices; i++)
    {
      // Search the wire for address
      if (sensors.getAddress(tempDeviceAddress, i))
      {
        // Output the device ID
        Serial.print("Temperature for device: ");
        Serial.println(i, DEC);
        // Print the data
        temperature = sensors.getTempF(tempDeviceAddress);
        if (temperature > 184)
        {
          Serial.print("Invalid temperature - skipping.");
        }
        else
        {
          Serial.print(" Temp F: ");
          Serial.println(temperature);

          // send the MQTT message
          // Convert the value to a char array
          char tempString[8];
          dtostrf(temperature, 3, 2, tempString);
          //sprintf(topic, topicBase, i);
          //mqttClient.publish(topic, tempString);
          String topic = pubTopicBase + String(i);
          mqttClient.publish(topic.c_str(), tempString);
          Serial.print(topic);
          Serial.print(" => ");
          Serial.println(tempString);
        }
      }
    }
  }
}