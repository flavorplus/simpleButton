#include <Arduino.h>
#include <FS.h>
#include <ESP8266WiFi.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <DNSServer.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <OneButton.h>
#include <Ticker.h>


/* ***************************************************** */
/* MQTT Settings */
char mqtt_server[40] = "casabril.local";
char mqtt_port[6] = "1883";
char mqtt_topic[40] = "/bedroom/bedButton";
#define BUFFER_SIZE 100
#define CLIENT_ID "client-cf3fff"

/* Button settings */
#define BTN1PIN D3
OneButton btn1(BTN1PIN, true);

WiFiClient wificlient;
PubSubClient client(wificlient);
Ticker ticker;
/* ***************************************************** */

//flag for saving data
bool shouldSaveConfig = false;

void statusLed() {
  //toggle state
  int state = digitalRead(BUILTIN_LED);  // get the current state of GPIO1 pin
  digitalWrite(BUILTIN_LED, !state);     // set pin to the opposite state
}
// ----- button 1 callback functions
// This function will be called when the button1 was pressed 1 time (and no 2. button press followed).
void click1() {
  Serial.println("Button 1 click.");
  client.publish(mqtt_topic, "CLICK");
}
// This function will be called when the button1 was pressed 2 times in a short timeframe.
void doubleclick1() {
  Serial.println("Button 1 doubleclick.");
  client.publish(mqtt_topic, "DOUBLE_CLICK");
} // doubleclick1
// This function will be called, when the button1 is pressed for a long time.
void longPress1() {
  Serial.println("Button 1 longPress...");
  client.publish(mqtt_topic, "LONG");
} // longPress1

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

//MQTT callback to process messages
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
    Serial.println();
}

void saveConfig(){
  Serial.print("Mounting FS...");
  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    Serial.println("saving config...");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;
    json["mqtt_topic"] = mqtt_topic;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }
    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    Serial.println("done.\nRebooting....");
    //end save
    delay(500);
    //reset in tot normal mode
    ESP.reset();
    delay(5000);
  } else {
    Serial.println("failed to mount FS");
  }
}

void loadConfig() {
  //read configuration from FS json
  Serial.print("Mounting FS...");
  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.print("Reading config file...");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file:");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          strcpy(mqtt_server, json["mqttServer"]);
          strcpy(mqtt_port, json["mqttPort"]);
          strcpy(mqtt_topic, json["mqttTopic"]);
        }
        Serial.println();
      } else {
        Serial.println("failed to load json config");
      }
    }
    //Unmount the file system
    SPIFFS.end();
  } else {
    Serial.println("failed to mount FS");
  }
}

//gets called when WiFiManager enters configuration mode
void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered WiFi config mode.");
  Serial.printf("Connect to SSID: %s and go to IP: %s\n", myWiFiManager->getConfigPortalSSID().c_str(), WiFi.softAPIP().toString().c_str());
  //entered config mode, make led toggle faster
  ticker.attach(0.2, statusLed);
}

//Setup
void setup() {
  Serial.begin(115200);
  //Serial.setDebugOutput(true);
  Serial.println("Booting...");
  //Setup status led
  pinMode(BUILTIN_LED, OUTPUT);
  ticker.attach(0.5, statusLed);
  //clean FS, for testing
  //SPIFFS.format();
  //Load config form SPIFFS
  loadConfig();

  //Start the Wifi Connection
  Serial.println("WiFi connecting...");
  WiFiManager wifiManager;
  //Reset wifi settings for testing
  //wifiManager.resetSettings();
  //set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setDebugOutput(false);
  if (!wifiManager.autoConnect()) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }
  Serial.println("Proceeding...");

  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  // ArduinoOTA.setHostname("myesp8266");

  // No authentication by default
  // ArduinoOTA.setPassword((const char *)"123");

  ArduinoOTA.onStart([]() {
    Serial.println("Starting OTA upload.....");
    ticker.attach(0.2, statusLed);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA upload done.\nRebooting...");
    ticker.detach();
    digitalWrite(BUILTIN_LED, HIGH);
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if      (error == OTA_AUTH_ERROR   ) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR  ) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR    ) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  // link the button 1 functions.
  btn1.attachClick(click1);
  btn1.attachDoubleClick(doubleclick1);
  btn1.attachLongPressStop(longPress1);
  //Done
  Serial.println("---------------\nFinished booting");
  Serial.printf("WiFi connected to: %s with IP: %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
  //change status LED
  ticker.detach();
  //digitalWrite(BUILTIN_LED, LOW);
  analogWrite(BUILTIN_LED, 1000);
}

/**
 * Main
 */
void loop() {
  ArduinoOTA.handle();

  if (WiFi.status() == WL_CONNECTED) {
    if (!client.connected()) {
      Serial.print("Connecting to MQTT...");
      client.setServer(mqtt_server, atoi(mqtt_port));
      if (client.connect(CLIENT_ID)) {
        client.setCallback(mqttCallback);
        client.subscribe(mqtt_topic);
        Serial.println("done.");
      } else {
        Serial.println("failed!!!");
      }
    }
  }

  if (client.connected())
    client.loop();
  // keep watching the push button:
  btn1.tick();
}
