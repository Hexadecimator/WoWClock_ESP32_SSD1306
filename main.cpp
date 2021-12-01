bool debug = false;

/***************************************************
Board: ESP32 Devkit V1
Hookup:
SCL to D22
SDA to D21
**** SSD1306 Lessons Learned:
0. 0,0 is top-left. Positive Y downwards
1. .setCursor(X, Y)
2. move .setCursor's Y value 10 for every 1 in
   the .setTextSize(#)
   - example: for .setTextSize(2) each line is
   Y = 20 pixels tall
3. SSD1306 is always 0x3C don't let the libs lie
4. @ .setTextSize(2) you can fit 10 characters
5. @ .setTextSize(1) you can fit 21 characters
6. That yellow title at the top is 20 pixels tall
TODO:
1. check out https://github.com/greiman/SSD1306Ascii for
   fast ASCII writing to SSD1306 (no graphics!)
***************************************************/
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "esp_wpa2.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <HTTPClient.h>

#define ARDUINOJSON_USE_LONG_LONG 1 // need this because the ironforge date string looks like this: 1637128800000
#include "ArduinoJson.h"

#include "Secrets.h"

WiFiClientSecure client;
HTTPClient http;

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C /// both are 0x3C don't listen to the lies
Adafruit_SSD1306 LED(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

bool swapper = false;
int GMTOffset = (-7)*60*60; // AZ is GMT-7 but it has to be in seconds
int daylightOffset = 0;

unsigned long majorDelta = 0;
unsigned long minorDelta = 0;
unsigned long majorTimeout = 5000;  // 3 seconds
unsigned long minorTimeout = 20000; // 20 seconds

const char* fairbanks_name = "Fairbanks";
int fairbanks_week     = 0;
int fairbanks_total    = 0;
int fairbanks_horde    = 0;
int fairbanks_alliance = 0;
bool connected_to_IF   = false;

void connectWiFiWork()
{
  // start WPA2 enterprise config magic
  if(debug) Serial.println("Cleaning WiFi connection...");
  WiFi.disconnect(true);

  WiFi.mode(WIFI_MODE_STA); // setting this here makes esp_wifi_sta_wpa2_ent_enable(&config); NOT restart-loop the ESP32

  if(debug) Serial.println("Setting wpa2 enterprise config...");
  if(debug) Serial.println("wpa2 config 1...");
  esp_wifi_sta_wpa2_ent_set_identity((uint8_t*)EAP_ID,       strlen(EAP_ID)      );
  if(debug) Serial.println("wpa2 config 2...");
  esp_wifi_sta_wpa2_ent_set_username((uint8_t*)EAP_USERNAME, strlen(EAP_USERNAME));
  if(debug) Serial.println("wpa2 config 3...");
  esp_wifi_sta_wpa2_ent_set_password((uint8_t*)EAP_PASSWORD, strlen(EAP_PASSWORD));

  if(debug) Serial.println("wpa2 config 4...");
  esp_wpa2_config_t config = WPA2_CONFIG_INIT_DEFAULT();
  if(debug) Serial.println("wpa2 config 5...");
  esp_wifi_sta_wpa2_ent_enable(&config); // this line causes ESP to reset loop
  // end WPA2 enterprise config magic

  if(debug) Serial.println("wpa2 config complete. Executing WiFi.begin()");
  WiFi.begin(SSID_PING);

  //int error_count = 0;

  if(debug) Serial.println("WiFi.begin() success, waiting for connection...");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);

    switch ( WiFi.status() )
    {
      case WL_NO_SHIELD:
        if(debug) Serial.println("WiFi.status() == WL_NO_SHIELD");
        break;
      case WL_IDLE_STATUS:
        if(debug) Serial.println("WiFi.status() == WL_IDLE_STATUS");
        break;
      case WL_NO_SSID_AVAIL:
        if(debug) Serial.println("WiFi.status() == WL_NO_SSID_AVAIL");
        // error_count += 1;
        // if(error_count >= 15)
        // {
        //   Serial.println("[ERROR] 15 FAILURES. ABORTING CONNECTION ATTEMPT");
        //   return;
        // }
        break;
      case WL_SCAN_COMPLETED:
        if(debug) Serial.println("WiFi.status() == WL_SCAN_COMPLETED");
        break;
      case WL_CONNECTED:
        if(debug) Serial.println("WiFi.status() == WL_CONNECTED");
        break;
      case WL_CONNECT_FAILED:
        if(debug) Serial.println("WiFi.status() == WL_CONNECT_FAILED");
        break;
      case WL_CONNECTION_LOST:
        if(debug) Serial.println("WiFi.status() == WL_CONNECTION_LOST");
        break;
      case WL_DISCONNECTED:
        if(debug) Serial.println("WiFi.status() == WL_DISCONNECTED");
        break;
      default:
        Serial.print("Unable to interpret WiFi.status() [");
        Serial.print(WiFi.status());
        Serial.println("]");
        return;
        break;
    }
  }

  LED.clearDisplay();
  LED.setTextColor(SSD1306_WHITE);
  LED.setTextSize(1.7);
  LED.setCursor(0, 20);
  LED.print("CONNECTED TO ");
  LED.print(WiFi.SSID());
  LED.display();

  Serial.println("");
  if(debug) Serial.print("Connected to " + String(SSID_PING) + ". IP Address: ");
  if(debug) Serial.println(WiFi.localIP());
}

void connectWiFiHome()
{
  LED.clearDisplay();
  LED.setTextColor(SSD1306_WHITE);
  LED.setTextSize(2);
  LED.setCursor(0,0);
  LED.println("Connecting to WiFi..");
  LED.display();

  WiFi.begin(SSID_HOME, PASS_HOME);
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("Still connecting.");
    delay(1000); 
  }

  delay(1000);

  LED.clearDisplay();
  LED.setTextColor(SSD1306_WHITE);
  LED.setTextSize(1.5);
  LED.setCursor(0,0);
  LED.println("CONNECTED TO " + String(SSID_HOME));
  LED.display();

  delay(1000);
}


void printTimeHeader()
{
  // ==================== CLEAR THE TOP LINE ==================
  LED.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
  LED.setTextSize(2);
  LED.setCursor(0, 0);
  LED.print("          "); // this should clear JUST the line

  LED.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
  LED.setTextSize(2);
  LED.setCursor(0, 0);
  LED.print("          "); // this should clear JUST the line
  // =========== FINISH CLEARING ============

  LED.setCursor(0, 0);

  // time stuff
  time_t rawtime = time(nullptr);
  struct tm* timeinfo = localtime(&rawtime);

  //convert to 12 hour time fomat
  int hour = timeinfo->tm_hour;
  if (hour > 12) hour = hour - 12;

  LED.print("TIME ");
  LED.print(String(hour));
  LED.print(":");
  if (timeinfo->tm_min < 10)
  {
    LED.print("0");
    LED.print(String(timeinfo->tm_min)); // pad 0
  }
  else 
  {
    LED.print(String(timeinfo->tm_min));
  }

  // if I don't show seconds I only have to refresh
  // the clock every 30 seconds or so
  // LED.print(":");

  // if (timeinfo->tm_sec < 10)
  // {
  //   LED.print("0");
  //   LED.print(String(timeinfo->tm_sec)); // pad 0
  // }
  // else
  // {
  //   LED.print(String(timeinfo->tm_sec));
  // } 
}

void getJsonFromIF()
{
  /****************************************************************************************
   * THIS WORKS!
   * It seems like the first request returns a status code 500 (bad request?) but then it 
   * starts working on subsequent connection attempts and returns JSON packets
  ****************************************************************************************/
  
  if( WiFi.status() == WL_CONNECTED )
  {
    http.begin("https://ironforge.pro/api/servers?timeframe=TBC"); // httpClient lib checks for "https:" at start so it has to be there

    http.addHeader("Content-Type", "application/json");

    int httpCode = http.GET();

    if (httpCode > 0)
    {
      String payload = http.getString();
      if(debug) Serial.println();
      if(debug) Serial.println("**** ironforge API results ****");
      if(debug) Serial.print(payload);

      DynamicJsonDocument IFdoc(16384);
      DeserializationError error = deserializeJson(IFdoc, payload);

      if (error) 
      {
        Serial.print("deserializeJson() failed: ");
        Serial.println(error.c_str());
        return;
      }

      fairbanks_week = IFdoc["week"]; // 25

      for (JsonObject value : IFdoc["values"].as<JsonArray>()) 
      {
        if(!(strcmp(value["name"], fairbanks_name) == 0)) continue; // skip loop iteration if we're not on the Fairbanks packet

        //const char* value_name = value["name"]; // "Firemaw", "Benediction", "Gehennas", "Whitemane", ...
        fairbanks_total = value["total"]; // 21560, 21296, 16970, 16400, 16085, 14517, 10655, 9269, 9148, 8853, ...
        fairbanks_alliance = value["alliance"]; // 11144, 17297, 4018, 4835, 3714, 5560, 6573, 3261, 334, 6759, ...
        fairbanks_horde = value["horde"]; // 10416, 3999, 12952, 11565, 12371, 8957, 4082, 6008, 8814, 2094, 0, ...
      
        connected_to_IF = true;
        break; // we have what we want.. exit the loop
      }

      Serial.println();
    }
    else
    {
      Serial.print("HTTP returned NOT OKAY Code: ");
      Serial.println(String(httpCode));
    }

    http.end();
  }
}

void connect2()
{
  //http.begin(client, "https://ironforge.pro/population/tbc/?locale=US&realm=PvP");
  //int checkBegin = http.begin(client, "ironforge.pro", 443, "/population/tbc/?locale=US&realm=PvP", false);
  //int checkBegin = http.begin(client, "ironforge.pro", 443, "/", false);
  int checkBegin = http.begin("https://ironforge.pro/population/tbc/?locale=US&realm=PvP");
  Serial.print("checkBegin = ");
  Serial.println(checkBegin);

  int httpCode = http.GET();

  String payload = http.getString();

  Serial.print("Code = ");
  Serial.println(httpCode);

  Serial.println(payload);

  http.end();
  //client.stop();
}

void connect1()
{
  if (client.connected()) client.stop();

  if (!client.connect("https://ironforge.pro", 443)) // or 80 // if you put "https://ironforge.pro" the ESP32 crashes (?????????????????????)
  {
    Serial.println("Connection to IF failed.");
  }

  yield(); // this gives the ESP a breather

  client.print(F("GET /api/servers?timeframe=TBC"));

  //client.print("/population/tbc/?locale=US&realm=PvP");
  // /api/servers?timeframe=TBC
  //client.print("/api/servers?timeframe=TBC");
  client.println(" HTTP/1.1");

  client.print(F("Host: "));
  client.println("ironforge.pro");

  client.println(F("Cache-Control: no-cache"));

  if(client.println() == 0)
  {
    Serial.println(F("Failed to send request"));
    return;
  }

  //char status[32] = {0};
  //client.readBytesUntil('\r', status, sizeof(status));
  char status[492] = {0};
  client.readBytes(status, sizeof(status));
  if (strcmp(status, "HTTP/1.1 200 OK") != 0)
  {
    Serial.print(F("Unexpected response: "));
    Serial.println(status);
    return;
  }

  char omgData[300] = {0};

  char searchString[] = "Fairbanks";

  if(!client.find(searchString))
  {
    Serial.println(F("Didn't find fairbanks"));
    return;
  }
  else
  {
    client.readBytes(omgData, sizeof(omgData));

    Serial.println(omgData);
  }
}

void displayFairbanksPop()
{
  // https://ironforge.pro/population/tbc/?locale=US&realm=PvP
  
  if (!connected_to_IF) getJsonFromIF();

  LED.setTextColor(SSD1306_WHITE);
  LED.setTextSize(1.9);
  LED.setCursor(0, 20);
  LED.print("Total Pop: ");
  LED.println(fairbanks_total);

  LED.setCursor(0, 35);
  LED.print("Horde:     ");
  LED.println(fairbanks_horde);

  LED.setCursor(0, 50);
  LED.print("Alliance:  ");
  LED.println(fairbanks_alliance);
}

void displayTeroconePrice()
{
  // retrieve Fairbanks horde current Terocone price
  // TODO: get URL off discord
 
  LED.setTextColor(SSD1306_WHITE);
  LED.setTextSize(2);
  LED.setCursor(0, 20);
  LED.println("TODO:CONE");
}

void displayIP()
{
  LED.setTextColor(SSD1306_WHITE);

  // *** print the IP address of the ESP32
  LED.setTextSize(1.7);
  LED.setCursor(0, 20);
  LED.print("**IP Address:");

  LED.setTextSize(1.7);
  LED.setCursor(0, 30);

  if (WiFi.status() != WL_CONNECTED)
  {
    LED.setTextSize(1.7);
    LED.setCursor(0, 30);
    LED.println("(not connected)");
  }
  else
  {
    LED.setTextSize(1.6);
    LED.setCursor(0, 30);
    IPAddress ip = WiFi.localIP();
    LED.println(ip.toString());

    LED.setTextSize(1.5);
    LED.setCursor(0, 40);
    LED.println("**MAC: ");

    LED.setCursor(0, 50);
    LED.println(WiFi.macAddress());
  }
}



void setup() 
{
  Serial.begin(115200);

  if(debug) Serial.println("Allocating SSD1306...");
  if(!LED.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) 
  {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  } 

  if(debug) Serial.println("SSD1306 Success! Connecting to WiFi...");

  LED.clearDisplay();
  LED.setTextColor(SSD1306_WHITE);
  LED.setTextSize(1.7);
  LED.setCursor(0, 20);
  LED.println("CONNECTING TO WIFI.\nPlease wait.");
  LED.display();
  
  //connectWiFiHome(); // non-WPA2 enterprise
  connectWiFiWork();   // WPA2 enterprise

  delay(100); // let WiFi settle before going to Time servers

  if(debug) Serial.println("WiFi Connected! Configuring Time servers...");

  // contact the time server
  //configTime(GMTOffset, daylightOffset, "pool.ntp.org", "time.nist.gov");
  configTime(GMTOffset, daylightOffset, "pool.ntp.org");


  majorDelta = millis();
  minorDelta = millis();

  if(debug) Serial.println("Time configured! Entering main loop...");
}

// magic
typedef void (*ScreenList[])();
uint8_t currentScreen = 0;

// list of all screens to loop through
ScreenList allScreens = { displayIP, displayFairbanksPop, displayTeroconePrice };
#define ARRAY_SIZE(A) (sizeof(A) / sizeof((A)[0]))
void nextPattern()
{
  currentScreen = (currentScreen + 1) % ARRAY_SIZE( allScreens );
}

void loop() 
{
  unsigned long currentTime = millis();

  // every 3 seconds move to the next screen
  if (currentTime - majorDelta >= majorTimeout)
  {
    LED.clearDisplay();
    printTimeHeader();
    nextPattern(); // increment currentScreen
    allScreens[currentScreen](); // absolute magic
    majorDelta = millis();
    LED.display();
  }

  // every 20 seconds refresh the clock
  if (currentTime - minorDelta >= minorTimeout)
  {
    printTimeHeader();
    minorDelta = millis();
    LED.display();
  }

  // TODO: add check to see if millis() is about to wrap
  // millis() maxes out at 2^32-1 
  // 4294967295; //unsigned long maximum value
  // but we shouldn't have to worry about wrapping to negative
  // values because these variables are unsigned long
}