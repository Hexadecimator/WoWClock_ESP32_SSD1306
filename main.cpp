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
//#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <HTTPClient.h>

#include "Secrets.h"

/* 
  === example return packet:
  Fairbanks</h3><h6 class="server__info"><span>US </span><span class="server__info--break">West</span><span>PvP </span><span>3702</span></h6><div class="progress-bar alliance " style="width: 0.135062%;"></div><div class="progress-bar horde " style="width: 99.8649%;">3697 / 99.9%</div>
*/

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
unsigned long majorTimeout = 3000;  // 3 seconds
unsigned long minorTimeout = 20000; // 30 seconds


void connectWiFi()
{
  LED.clearDisplay();
  LED.setTextColor(SSD1306_WHITE);
  LED.setTextSize(2);
  LED.setCursor(0,0);
  LED.println("Connecting to WiFi..");
  LED.display();

  WiFi.begin(SSID, PASS);
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
  LED.println("CONNECTED TO " + String(SSID));
  LED.display();

  delay(1000);
}

void setup() 
{
  Serial.begin(9600);

  if(!LED.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) 
  {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  } 

  connectWiFi(); 

  // contact the time NTP server
  configTime(GMTOffset, daylightOffset, "pool.ntp.org", "time.nist.gov");

  majorDelta = millis();
  minorDelta = millis();
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

  // *** print the IP address of the ESP32
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

void connect2()
{
  //http.begin(client, "https://ironforge.pro/population/tbc/?locale=US&realm=PvP");
  //int checkBegin = http.begin(client, "ironforge.pro", 443, "/population/tbc/?locale=US&realm=PvP", false);
  int checkBegin = http.begin(client, "ironforge.pro", 443, "/", false);
  Serial.print("checkBegin = ");
  Serial.println(checkBegin);

  int code = http.GET();

  String payload = http.getString();

  Serial.print("Code = ");
  Serial.println(code);

  Serial.println(payload);

  http.end();
  client.stop();
}

void connect1()
{
  if (client.connected()) client.stop();

  if (!client.connect("https://ironforge.pro", 443)) // or 80 // if you put "https://ironforge.pro" the ESP32 crashes (?????????????????????)
  {
    Serial.println("Connection to IF failed.");
  }

  yield(); // this gives the ESP a breather

  client.print(F("GET "));

  //client.print("/population/tbc/?locale=US&realm=PvP");
  client.print("/population/tbc");
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
  
  LED.setTextColor(SSD1306_WHITE);
  LED.setTextSize(2);
  LED.setCursor(0, 20);
  LED.println("TODO:POP");

  //connect1();
  connect2();
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
  LED.setCursor(0, 28);
  LED.println("IP Address");

  LED.setTextSize(1);
  LED.setCursor(0, 40);
  if (WiFi.status() != WL_CONNECTED)
  {
    LED.println("(not connected)");
  }
  else
  {
    IPAddress ip = WiFi.localIP();
    LED.println(ip.toString());
  }
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
}


