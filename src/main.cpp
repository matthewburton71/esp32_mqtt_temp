/*********
 Matthew Burton
*********/

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <AutoConnect.h>
#include <PubSubClient.h>
#include <SPIFFS.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#define GET_CHIPID() ((uint16_t)(ESP.getEfuseMac() >> 32))

#define PARAM_FILE "/param.json"
#define AUX_MQTTSETTING "/mqtt_setting"
#define AUX_MQTTSAVE "/mqtt_save"
#define AUX_MQTTCLEAR "/mqtt_clear"

typedef WebServer WiFiWebServer;

// Data wire is plugged TO GPIO 4
#define ONE_WIRE_BUS 15

#define HOSTNAME_BASE "esp32-"
const int WAIT = 10 * 1000; // 10 sec

const int STATUS_LED = LED_BUILTIN;

// Add your MQTT Broker IP address, example:
const char *mqtt_server = "192.168.86.5";

String hostName;
String serverName;
String pubTopicBase;
String subTopic;
String channelId;
String username;
String password;
unsigned int updateInterval = 0;

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

String loadParams(AutoConnectAux &aux, PageArgument &args)
{
  (void)(args);
  File param = SPIFFS.open(PARAM_FILE, "r");
  if (param)
  {
    aux.loadElement(param);
    param.close();
  }
  else
    Serial.println(PARAM_FILE " open failed");
  return String("");
}

String saveParams(AutoConnectAux &aux, PageArgument &args)
{
  serverName = args.arg("mqttserver");
  serverName.trim();

  channelId = args.arg("channelid");
  channelId.trim();

  username = args.arg("username");
  username.trim();

  password = args.arg("password");
  password.trim();

  String upd = args.arg("period");
  updateInterval = upd.substring(0, 2).toInt() * 1000;

  String uniqueid = args.arg("uniqueid");

  hostName = args.arg("hostname");
  hostName.trim();

  // The entered value is owned by AutoConnectAux of /mqtt_setting.
  // To retrieve the elements of /mqtt_setting, it is necessary to get
  // the AutoConnectAux object of /mqtt_setting.
  File param = SPIFFS.open(PARAM_FILE, "w");
  Portal.aux("/mqtt_setting")->saveElement(param, {"mqttserver", "channelid", "username", "password", "period", "uniqueid", "hostname"});
  param.close();

  // Echo back saved parameters to AutoConnectAux page.
  AutoConnectText &echo = aux["parameters"].as<AutoConnectText>();
  echo.value = "Server: " + serverName + "<br>";
  echo.value += "Channel ID: " + channelId + "<br>";
  echo.value += "User Name: " + username + "<br>";
  echo.value += "Password: " + password + "<br>";
  echo.value += "Update period: " + String(updateInterval / 1000) + " sec.<br>";
  echo.value += "Use APID unique: " + uniqueid + "<br>";
  echo.value += "ESP host name: " + hostName + "<br>";

  return String("");
}

void rootPage()
{
  String content =
      "<html>"
      "<head>"
      "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
      "</head>"
      "<body>"
      "Tempeture"
      "<p style=\"padding-top:10px;text-align:center\">" AUTOCONNECT_LINK(COG_24) "</p>"
                                                                                  "</body>"
                                                                                  "</html>";
  Server.send(200, "text/html", content);
}

// Load AutoConnectAux JSON from SPIFFS.
bool loadAux(const String auxName)
{
  bool rc = false;
  String fn = auxName + ".json";
  File fs = SPIFFS.open(fn.c_str(), "r");
  if (fs)
  {
    rc = Portal.load(fs);
    fs.close();
  }
  else
    Serial.println("SPIFFS open failed: " + fn);
  return rc;
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
  Serial.println();

  // Serial.println("begin SPIFFS...");
  SPIFFS.begin();

  loadAux(AUX_MQTTSETTING);
  loadAux(AUX_MQTTSAVE);

  // Serial.println("loading config...");
  AutoConnectAux *setting = Portal.aux(AUX_MQTTSETTING);
  if (setting)
  {
    PageArgument args;
    AutoConnectAux &mqtt_setting = *setting;
    loadParams(mqtt_setting, args);
    AutoConnectCheckbox &uniqueidElm = mqtt_setting["uniqueid"].as<AutoConnectCheckbox>();
    AutoConnectInput &hostnameElm = mqtt_setting["hostname"].as<AutoConnectInput>();
    if (uniqueidElm.checked)
    {
      Config.apid = String("ESP") + "-" + String(GET_CHIPID(), HEX);
      Serial.println("apid set to " + Config.apid);
    }
    if (hostnameElm.value.length())
    {
      Config.hostName = hostnameElm.value;
      Serial.println("hostname set to " + Config.hostName);
    }
    Config.bootUri = AC_ONBOOTURI_HOME;
    Config.homeUri = "/";
  }
  else
    Serial.println("aux. load error");

  // AutoConfig
  Config.title = "Temp Probes";
  // Make the name using the last 2 dig the MAC Address.
  hostName = HOSTNAME_BASE + WiFi.macAddress().substring(15);
  //Config.hostName = hostName;
  Config.tickerPort = STATUS_LED;
  Config.ticker = true;
  Config.tickerOn = HIGH;
  Portal.config(Config);

  Portal.on(AUX_MQTTSETTING, loadParams);
  Portal.on(AUX_MQTTSAVE, saveParams);

  // Serial.println("Portal begin...");
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