#define FASTLED_ESP8266_NODEMCU_PIN_ORDER
#define FASTLED_ALLOW_INTERRUPTS 0
#define DEBUG true
#include "InternalFunc.h"
#include "ArduinoOTA.h"
#include <ESP8266WiFi.h>
#include <FS.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

#define NUM_LEDS 1
#define DATA_PIN 4
#define SLEEP_TIME 30000

// configfilename
static const char CONFIG_FILE[] = "/config.json";
// Define the array of leds
CRGB leds[NUM_LEDS];
// location string
char location[50] = "";
// api key
char apikey[50] = "";
//flag for saving data
bool shouldSaveConfig = false;
//timestamp calls
unsigned long timestamp;
bool force_update;
//tz timezone
const long utcOffsetInSeconds = 10 * 60 * 60 + (0 * 60);
// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);

void setup()
{
  Serial.begin(9600);
  configWIFIAndParameters();
  arduinoOTASetup();
  // WORKS : GRB
  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS);
  timeClient.begin();
}

void loop()
{
  controlSun();
  sunrise();
  FastLED.show();
  ArduinoOTA.handle();
}

void controlSun()
{
  // Only update every 30 seconds
  if (((millis() - timestamp) >= SLEEP_TIME) || force_update)
  {
    force_update = false;
    timestamp = millis();
    timeClient.update();
    Serial.print("Epoch Time:");
    Serial.println(timeClient.getEpochTime());
  }
}

void sunrise()
{

  // total sunrise length, in minutes
  static const uint8_t sunriseLength = 5;

  // how often (in seconds) should the heat color increase?
  // for the default of 30 minutes, this should be about every 7 seconds
  // 7 seconds x 256 gradient steps = 1,792 seconds = ~30 minutes
  static const uint8_t interval = (sunriseLength * 60) / 256;

  // current gradient palette color index
  static uint8_t heatIndex = 0; // start out at 0

  // HeatColors_p is a gradient palette built in to FastLED
  // that fades from black to red, orange, yellow, white
  // feel free to use another palette or define your own custom one
  CRGB color = ColorFromPalette(HeatColors_p, heatIndex);

  // fill the entire strip with the current color
  fill_solid(leds, NUM_LEDS, color);

  // slowly increase the heat
  EVERY_N_SECONDS(interval)
  {
    // stop incrementing at 255, we don't want to overflow back to 0
    if (heatIndex < 255)
    {
      heatIndex++;
    }
  }
}
// calls related to config file
void configWIFIAndParameters()
{
  //clean FS, for testing
  //SPIFFS.format();
  //SPIFFS.remove(CONFIG_FILE);
  //read configuration from FS json
  //Serial.println("mounting FS...");

  if (SPIFFS.begin())
  {
    if (DEBUG)
      Serial.println("\nMSG: mounted file system");
    if (SPIFFS.exists(CONFIG_FILE))
    {
      //file exists, reading and loading
      if (DEBUG)
        Serial.println("\nMSG: reading config file");
      File configFile = SPIFFS.open(CONFIG_FILE, "r");
      if (configFile)
      {
        if (DEBUG)
          Serial.println("\nMSG: opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);
        configFile.readBytes(buf.get(), size);
        StaticJsonDocument<512> jsonBuffer;
        auto error = deserializeJson(jsonBuffer, buf.get());
        if (!error)
        {
          if (DEBUG)
          {
            serializeJsonPretty(jsonBuffer, Serial);
            Serial.println();
          }
          if (jsonBuffer["location"] != NULL)
          {
            strcpy(location, jsonBuffer["location"]);
            if (DEBUG)
            {
              Serial.print("\nMSG: saved location::::");
              Serial.println(location);
            }
          }
          if (jsonBuffer["apikey"] != NULL)
          {
            strcpy(apikey, jsonBuffer["apikey"]);
            if (DEBUG)
            {
              Serial.print("\nMSG: saved apikey::::");
              Serial.println(apikey);
            }
          }
        }
        else
        {
          Serial.println("failed to load json config");
        }
      }
      configFile.close();
    }
    else
    {
      Serial.println("\nMSG: missing config file");
    }
  }
  else
  {
    Serial.println("\nERR: Failed to mount file system");
  }

  WiFiManagerParameter custom_location("Location", "location", location, 50);
  WiFiManagerParameter custom_apikey("WeatherAPIKey", "apikey", apikey, 50);

  WiFiManager wifiManager;

  //set config save notify callback
  wifiManager.setDebugOutput(DEBUG);
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.setTimeout(180);
  //SPIFFS.format();
  //wifiManager.resetSettings();
  wifiManager.addParameter(&custom_location);
  wifiManager.addParameter(&custom_apikey);

  if (!wifiManager.autoConnect("badal"))
  {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }

  //save the custom parameters to FS
  if (shouldSaveConfig)
  {
    if (DEBUG)
      Serial.println("\nMSG: Should save config");
    StaticJsonDocument<512> jsonBuffer;
    jsonBuffer["location"] = custom_location.getValue();
    strcpy(location, custom_location.getValue());
    jsonBuffer["apikey"] = custom_apikey.getValue();
    strcpy(apikey, custom_apikey.getValue());

    File configFile = SPIFFS.open(CONFIG_FILE, "w");
    if (!configFile)
    {
      Serial.println("ERR: failed to open config file for writing");
    }
    if (DEBUG)
    {
      serializeJson(jsonBuffer, Serial);
      Serial.println();
    }
    if (serializeJson(jsonBuffer, configFile) == 0)
    {
      Serial.println(F("\nERR: Failed to write to file"));
    }
    else
    {
      Serial.println(F("\nMSG: Config file written successfully"));
    }
    configFile.close();
    //end save
  }
}

//callback notifying us of the need to save config
void saveConfigCallback()
{
  if (DEBUG)
    Serial.println("\nMSG: Should save config");
  shouldSaveConfig = true;
}

//OTA
void arduinoOTASetup()
{
  // Port defaults to 8266
  //ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  // ArduinoOTA.setHostname("myesp8266");

  // No authentication by default
  ArduinoOTA.setPassword("password");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
    {
      type = "sketch";
    }
    else
    { // U_SPIFFS
      type = "filesystem";
    }

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR)
    {
      Serial.println("Auth Failed");
    }
    else if (error == OTA_BEGIN_ERROR)
    {
      Serial.println("Begin Failed");
    }
    else if (error == OTA_CONNECT_ERROR)
    {
      Serial.println("Connect Failed");
    }
    else if (error == OTA_RECEIVE_ERROR)
    {
      Serial.println("Receive Failed");
    }
    else if (error == OTA_END_ERROR)
    {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}