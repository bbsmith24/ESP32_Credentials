/*
  Brian Smith
  November 2022
  Copyright (c) 2022 Brian B Smith. All rights reserved.
  brianbsmith.com
  info@brianbsmith.com
  
  ESP32_Credentials
    
  get wifi credentials from built in web page, store to LITTLEFS file system, use until reset
  basis for IOT and other projects requiring WiFi connection and NTP/RTC time

  This is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

  This does nothing interesting on its own, except to get credentials and timezone offset (if needed) then connect to 
  WiFi and output the time every minute. Use as a basis for other applications that need WiFi
  
  'ESP32_Credentials' is based on "ESP32: Create a Wi-Fi Manager" tutorial by Rui Santos at Random Nerds Tutorials
  found here: https://randomnerdtutorials.com/esp32-wi-fi-manager-asyncwebserver/
  They have lots of useful tutorials here, well worth a visit!

  changes from original:
    just get wifi credentials, no web server page after connection
    refactored for clarity/ease of use (at least it's more clear to me...)
    added 'clear credentials' to allow for changing between wifi networks (reset, reboots to credentials page, reboots to connection)
    added 'define VERBOSE' for debug statements
    use defines for constant values
    allow undefined local ip, allows router to set ip from DNS
    get time from NTP server and set internal RTC
    added local time offset from GMT and daylight savings time offset to credentials page - hours now, could be enhanced with a combo box for timezone

*/
#define VERBOSE            // more output for debugging
#define FORMAT_LITTLEFS_IF_FAILED true
// for using alternate serial ports
//#define ALT_SERIAL
#define SERIALX Serial
//#define SERIALX Serial2
#define RXD2 16
#define TXD2 17

#include <Arduino.h>            // 
#include <WiFi.h>               // 
#include <ESPAsyncWebServer.h>  // https://github.com/me-no-dev/ESPAsyncWebServer
#include <AsyncTCP.h>           // https://github.com/me-no-dev/AsyncTCP
#include "FS.h"                 // 
#include "LITTLEFS.h"           // LittleFS_esp32 by loral from Arduino library manager
//#include "SPIFFS.h"             // 
#include <time.h>               // for NTP time
#include <ESP32Time.h>          // for RTC time https://github.com/fbiego/ESP32Time


// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

//
// global variables for ESP32 credentials
// using #define instead of const saves some program space
#define WIFI_WAIT 10000    // interval between attempts to connect to wifi
// Search for parameter in HTTP POST request
#define PARAM_INPUT_1  "ssid"
#define PARAM_INPUT_2  "pass"
#define PARAM_INPUT_3  "ip"
#define PARAM_INPUT_4  "gateway"
#define PARAM_INPUT_5  "timezone"
#define PARAM_INPUT_6  "dst"
//Variables to save values from HTML form
String ssid;
String pass;
String ip;
String gateway;
String tz;
String dst;
// File paths to save input values permanently
#define ssidPath  "/ssid.txt"
#define passPath  "/pass.txt"
#define ipPath  "/ip.txt"
#define gatewayPath  "/gate.txt"
#define tzPath  "/tz.txt"
#define dstPath  "/dst.txt"

// ESP32 IP address (use DNS if blank)
IPAddress localIP;
// local Gateway IP address and subnet mask
IPAddress localGateway;
IPAddress subnet(255, 255, 0, 0);
//
// global variables for time
//
// NTP Server Details
#define ntpServer "pool.ntp.org"

long  gmtOffset_sec = 0;
int   daylightOffset_sec = 0;
ESP32Time rtc(0);

int dayNum;
int monthNum;
int yearNum;
int hourNum;
int minNum;
int secondNum;

int lastDayNum = -1;
int lastHourNum = -1;
int lastMinNum = -1;

char weekDay[10];
char dayMonth[4];
char monthName[5];
char year[6];
char hour[4];
char minute[4];
char second[4];
char localTimeStr[256];
char connectDateTime[256];
bool connectDateTimeSet = false;
bool rtcTimeSet = false;
// Timer variables
unsigned long previousMillis = 0;

char wifiState[256];
bool wifiConnected = false;
//
// connect to wifi. if credentials not present/invalid, boot to access point and present page to get them
//
void setup()
{
#ifdef VERBOSE
  // Serial port for debugging purposes
  #ifndef ALT_SERIAL
  SERIALX.begin(115200);
  #else
  SERIALX.begin(115200, SERIAL_8N1, RXD2, TXD2);
  #endif
  #endif

  #ifdef VERBOSE
  SERIALX.println("");
  SERIALX.println("BBS Nov 2022");
  SERIALX.println("IOT WiFi Credentials");
  SERIALX.println("====================");
  delay(1000);
  #endif
    // set CPU freq to 80MHz, disable bluetooth  to save power
  #ifdef VERBOSE
  int freq = getCpuFrequencyMhz();
  SERIALX.printf("Default Freq\n");
  SERIALX.printf("CPU Freq = %dMhz\n", freq);
  freq = getXtalFrequencyMhz();
  SERIALX.printf("XTAL Freq = %dMhz\n", freq);
  freq = getApbFrequency();
  SERIALX.printf("APB Freq = %dHz\n", freq);
  #endif
  setCpuFrequencyMhz(80);
  #ifdef VERBOSE
  SERIALX.printf("Updated Freq\n");
  freq = getCpuFrequencyMhz();
  SERIALX.printf("CPU Freq = %dMhz\n", freq);
  freq = getXtalFrequencyMhz();
  SERIALX.printf("XTAL Freq = %dMhz\n", freq);
  freq = getApbFrequency();
  SERIALX.printf("APB Freq = %dHz\n", freq);
  #endif
  btStop();
  #ifdef VERBOSE
  SERIALX.printf("Bluetooth disabled\n");
  #endif  

  sprintf(wifiState, "Not connected");

  // initialize LITTLEFS for reading credentials
  LITTLEFS_Init();
  
  #ifdef VERBOSE
  // list files in LITTLEFS
  LITTLEFS_ListDir(LITTLEFS, "/", 10);
  #endif
  // uncomment to clear saved credentials 
  //ClearCredentials();

  // Load values saved in LITTLEFS if any
  LoadCredentials();
  //
  // try to initalize wifi with stored credentials
  // if getting credentials from config page, reboot after to connect with new credentials
  //
  if(!WiFi_Init()) 
  {
    #ifdef VERBOSE
    SERIALX.println("No credentials stored - get from locally hosted page");
    #endif
    GetCredentials();
  }
}
//
// main loop
// this justs updates local time every minute, replace with whatever functionality needed
//
void loop() 
{
  UpdateLocalTime();
  delay(60000);
}
// ================================ begin LITTLEFS functions ================================
//
// Initialize LITTLEFS
//
void LITTLEFS_Init() 
{
  if (!LITTLEFS.begin(true)) 
  {
    #ifdef VERBOSE
    SERIALX.println("An error has occurred while mounting LITTLEFS");
    #endif
    return;
  }
  #ifdef VERBOSE
  SERIALX.println("LITTLEFS mounted successfully");
  #endif
}
//
// Read File from LITTLEFS
//
String LITTLEFS_ReadFile(fs::FS &fs, const char * path)
{
  #ifdef VERBOSE
  SERIALX.printf("Reading file: %s - ", path);
  #endif
  File file = fs.open(path);
  if(!file || file.isDirectory())
  {
    #ifdef VERBOSE
    SERIALX.println("- failed to open file for reading");
    #endif
    return String();
  }
  
  String fileContent;
  while(file.available())
  {
    fileContent = file.readStringUntil('\n');
  }
  #ifdef VERBOSE
  SERIALX.println(" - read file");
  #endif
  return fileContent;
}
//
// Write file to LITTLEFS
//
void LITTLEFS_WriteFile(fs::FS &fs, const char * path, const char * message)
{
  #ifdef VERBOSE
  SERIALX.printf("Writing >>%s<< to file: %s ", message, path);
  #endif
  File file = fs.open(path, FILE_WRITE);
  if(!file)
  {
    #ifdef VERBOSE
    SERIALX.println("- failed to open file for writing");
    #endif
    return;
  }
  if(file.print(message))
  {
    #ifdef VERBOSE
    SERIALX.println("- file written");
    #endif
  }
   else 
   {
    #ifdef VERBOSE
    SERIALX.println("- file write failed");
    #endif
  }
}
//
// list LITTLEFS files
//
void LITTLEFS_ListDir(fs::FS &fs, const char * dirname, uint8_t levels)
{
    SERIALX.printf("Listing directory: %s\r\n", dirname);

    File root = fs.open(dirname);
    if(!root)
    {
        SERIALX.println("- failed to open directory");
        return;
    }
    if(!root.isDirectory())
    {
        SERIALX.println(" - not a directory");
        return;
    }

    File file = root.openNextFile();
    while(file)
    {
        if(file.isDirectory())
        {
            SERIALX.print("  DIR : ");
            SERIALX.println(file.name());
            if(levels)
            {
                LITTLEFS_ListDir(fs, file.name(), levels -1);
            }
        }
         else 
         {
            SERIALX.print("  FILE: ");
            SERIALX.print(file.name());
            SERIALX.print("\tSIZE: ");
            SERIALX.println(file.size());
        }
        file = root.openNextFile();
    }
}
//
// delete named file from LITTLEFS
//
void LITTLEFS_DeleteFile(fs::FS &fs, const char * path)
{
  #ifdef VERBOSE
  SERIALX.printf("Deleting file: %s ", path);
  #endif
  if(fs.remove(path))
  {
    #ifdef VERBOSE
    SERIALX.println("- file deleted");
    #endif
  }
  else 
  {
    #ifdef VERBOSE
    SERIALX.println("- delete failed");
    #endif
  }
}
// ================================ end LITTLEFS functions ================================
// ================================ begin WiFi initialize/credentials functions ================================
//
// Initialize WiFi
//
bool WiFi_Init() 
{
  // cant connect if there's no WiFi SSID defined
  if(ssid=="")
  {
    #ifdef VERBOSE
    SERIALX.println("Undefined SSID");
    #endif    
    wifiConnected = false;
    return false;
  }

  WiFi.mode(WIFI_STA);

  // for (optional) user defined gateway and local IP  
  localIP.fromString(ip.c_str());
  localGateway.fromString(gateway.c_str());
  if((ip != "") || (gateway != ""))
  {
    #ifdef VERBOSE
    SERIALX.printf("Configure wifi localIP %s gateway %s\n", ip, gateway);
    #endif
    if (!WiFi.config(localIP, localGateway, subnet))
    {
      #ifdef VERBOSE
      SERIALX.println("STA Failed to configure");
      #endif
      wifiConnected = false;
      return false;
    }
  }
  else
  {
    #ifdef VERBOSE  
    SERIALX.println("Connect to wifi with DNS assigned IP");
    #endif
  }
  // set up and connect to wifi
  WiFi.begin(ssid.c_str(), pass.c_str());
  #ifdef VERBOSE
  SERIALX.printf("Connecting to WiFi SSID: %s PWD: %s...", ssid.c_str(), pass.c_str());
  #endif
  unsigned long currentMillis = millis();
  previousMillis = currentMillis;

  int retryCount = 0;
  previousMillis = millis();
  wifiConnected = true;
  while((WiFi.status() != WL_CONNECTED) && (retryCount < 10))
  {
    currentMillis = millis();
    if (currentMillis - previousMillis >= WIFI_WAIT) 
    {
      #ifdef VERBOSE
      SERIALX.printf("Failed to connect on try %d of 10.", retryCount+1);
      #endif
      wifiConnected = false;
      retryCount++;
      previousMillis = currentMillis;
    }
  }
  wifiConnected = (WiFi.status() == WL_CONNECTED);
  if(!wifiConnected)
  {
      #ifdef VERBOSE    
      SERIALX.printf("Failed to connect after 10 attempts - reset credentials");
      #endif
      ClearCredentials();
  }
  sprintf(wifiState, "Connected %s ",WiFi.localIP().toString().c_str());

  #ifdef VERBOSE
  SERIALX.println(wifiState);
  #endif
  return wifiConnected;
}
//
// load wifi credentials from LITTLEFS
//
void LoadCredentials()
{
  ssid = LITTLEFS_ReadFile(LITTLEFS, ssidPath);
  ssid.trim();
  pass = LITTLEFS_ReadFile(LITTLEFS, passPath);
  pass.trim();
  ip = LITTLEFS_ReadFile(LITTLEFS, ipPath);
  ip.trim();
  gateway = LITTLEFS_ReadFile (LITTLEFS, gatewayPath);
  gateway.trim();
  tz = LITTLEFS_ReadFile (LITTLEFS, tzPath);
  tz.trim();
  dst = LITTLEFS_ReadFile (LITTLEFS, dstPath);
  dst.trim();

  gmtOffset_sec = atoi(tz.c_str()) * 3600; // convert hours to seconds
  daylightOffset_sec = atoi(dst.c_str()) * 3600; // convert hours to seconds
  
  LITTLEFS_ReadFile(LITTLEFS, "/wifimanager.html");
  #ifdef VERBOSE
  SERIALX.print("SSID: ");
  SERIALX.println(ssid);
  SERIALX.print("PASSWORD: ");
  SERIALX.println(pass);
  SERIALX.print("Fixed IP (optional): ");
  SERIALX.println(ip);
  SERIALX.print("Fixed gateway (optional): ");
  SERIALX.println(gateway);
  SERIALX.print("Timezone offset: ");
  SERIALX.print(tz);
  SERIALX.print(" ");
  SERIALX.println(gmtOffset_sec);
  SERIALX.print("DST offset: ");
  SERIALX.print(dst);
  SERIALX.print(" ");
  SERIALX.println(daylightOffset_sec);
  #endif
}
//
// get new credentials from user from web page in access point mode
//
void GetCredentials()
{
  disableCore0WDT();
  disableCore1WDT();
  // Connect to Wi-Fi network with SSID and password
  #ifdef VERBOSE
  SERIALX.print("Setting AP (Access Point) ");
  #endif
  // NULL sets an open Access Point
  WiFi.softAP("ESP-WIFI-MANAGER", NULL);

  IPAddress IP = WiFi.softAPIP();
  #ifdef VERBOSE
  SERIALX.print(" address: ");
  SERIALX.println(IP); 
  #endif
  // Web Server Root URL
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    #ifdef VERBOSE
    SERIALX.println("Request from webserver, send page");
    #endif  
    request->send(LITTLEFS, "/wifimanager.html", "text/html");
  });
    
  server.serveStatic("/", LITTLEFS, "/");
  //
  // display web page and get credentials from user
  //  
  server.on("/", HTTP_POST, [](AsyncWebServerRequest *request) 
  {
    int params = request->params();
    for(int i=0;i<params;i++)
    {
      yield();
      AsyncWebParameter* p = request->getParam(i);
      if(p->isPost())
      {
        // HTTP POST ssid value
        if (p->name() == PARAM_INPUT_1) 
        {
          ssid = p->value().c_str();
          #ifdef VERBOSE
          SERIALX.printf("%s %s\n", PARAM_INPUT_1, ssid);
          #endif
        }
        // HTTP POST password value
        if (p->name() == PARAM_INPUT_2) 
        {
          pass = p->value().c_str();
          #ifdef VERBOSE
          SERIALX.printf("%s %s\n", PARAM_INPUT_2, pass);
          #endif
        }
        // HTTP POST local ip value
        if (p->name() == PARAM_INPUT_3) 
        {
          ip = p->value().c_str();
          #ifdef VERBOSE
          SERIALX.printf("%s %s\n", PARAM_INPUT_3, ip);
          #endif
        }
        // HTTP POST gateway ip value
        if (p->name() == PARAM_INPUT_4) 
        {
          gateway = p->value().c_str();
          #ifdef VERBOSE
          SERIALX.printf("%s %s\n", PARAM_INPUT_4, gateway);
          #endif
        }
        // HTTP POST time zone offset value
        if (p->name() == PARAM_INPUT_5) 
        {
          tz = p->value().c_str();
          #ifdef VERBOSE
          SERIALX.printf("%s %s\n", PARAM_INPUT_5, tz);
          #endif
        }
        // HTTP POST dst offset value
        if (p->name() == PARAM_INPUT_6) 
        {
          dst = p->value().c_str();
          #ifdef VERBOSE
          SERIALX.printf("%s %s\n", PARAM_INPUT_6, dst);
          #endif
        }
      }
    } 
    request->send(200, "text/plain", "Done. ESP will restart, connect to your router and go to IP address: " + ip);
    #ifdef VERBOSE
    SERIALX.println("Store credentials in LITTLEFS and reboot");
    #endif
    SaveCredentials();
  });
  server.begin();
}
//
// save credentials to files
//
void SaveCredentials()
{
  LITTLEFS_WriteFile(LITTLEFS, ssidPath, ssid.c_str());
  LITTLEFS_WriteFile(LITTLEFS, passPath, pass.c_str());
  LITTLEFS_WriteFile(LITTLEFS, ipPath, ip.c_str());
  LITTLEFS_WriteFile(LITTLEFS, gatewayPath, gateway.c_str());
  LITTLEFS_WriteFile(LITTLEFS, tzPath, tz.c_str());
  LITTLEFS_WriteFile(LITTLEFS, dstPath, dst.c_str());
  #ifdef VERBOSE
  SERIALX.print("SSID set to: ");
  SERIALX.println(ssid);
  SERIALX.print("Password set to: ");
  SERIALX.println(pass);
  SERIALX.print("IP Address set to: ");
  SERIALX.println(ip);
  SERIALX.print("Time zone offset set to: ");
  SERIALX.println(tz);
  SERIALX.print("DST offset set to: ");
  SERIALX.println(dst);
  SERIALX.print("Gateway set to: ");
  SERIALX.println(gateway);
  #endif
  ESP.restart();
}
//
// clear credentials and restart
// allows user to change wifi SSIDs easily
//
void ClearCredentials()
{
  #ifdef VERBOSE
  SERIALX.println("Clear WiFi credentials");
  #endif
  LITTLEFS_DeleteFile(LITTLEFS, ssidPath);
  LITTLEFS_DeleteFile(LITTLEFS, passPath);
  LITTLEFS_DeleteFile(LITTLEFS, ipPath);
  LITTLEFS_DeleteFile(LITTLEFS, gatewayPath);
  LITTLEFS_DeleteFile(LITTLEFS, tzPath);
  LITTLEFS_DeleteFile(LITTLEFS, dstPath);
  #ifdef VERBOSE
  SERIALX.println("Restart...");
  #endif
  delay(WIFI_WAIT);
  ESP.restart();
}
// ================================ end WiFi initialize/credentials functions ================================
// ================================ begin NTP/RTC time functions ================================
//
// get local time (initially set via NTP server)
//
void UpdateLocalTime()
{
  if(!wifiConnected)
  {
    return;
  }
  // if not set from NTP, get time and set RTC
  if(!rtcTimeSet)
  {
    #ifdef VERBOSE
    SERIALX.print("Time from NTP server ");
    #endif
    struct tm timeinfo;
    if(!getLocalTime(&timeinfo))
    {
      // Init timeserver
      configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
      getLocalTime(&timeinfo);
    }
    //GET DATE
    strftime(dayMonth, sizeof(dayMonth), "%d", &timeinfo);
    strftime(monthName, sizeof(monthName), "%b", &timeinfo);
    strftime(year, sizeof(year), "%Y", &timeinfo);
    dayNum = atoi(dayMonth);
    monthNum = timeinfo.tm_mon+1;
    yearNum = atoi(year);

    //GET TIME
    strftime(hour, sizeof(hour), "%H", &timeinfo);
    strftime(minute, sizeof(minute), "%M", &timeinfo);
    strftime(second, sizeof(second), "%S", &timeinfo);
    hourNum = atoi(hour);
    minNum = atoi(minute);
    secondNum = atoi(second);

    //rtc.setTime(secondNum, minNum, hourNum, dayNum, monthNum, yearNum);
    rtc.setTimeStruct(timeinfo);
    rtcTimeSet = true;
  
    lastMinNum = minNum;
    lastHourNum = hourNum;
    lastDayNum = dayNum;
}
  // use RTC for time
  else
  {
    #ifdef VERBOSE
    SERIALX.print("Time from local RTC ");
    #endif
    dayNum = rtc.getDay();
    monthNum = rtc.getMonth() + 1;
    yearNum = rtc.getYear();
    hourNum = rtc.getHour();
    minNum = rtc.getMinute();
    secondNum = rtc.getSecond();
  }
  // set last time values to current
  if(lastMinNum == -1)
  {
    lastMinNum = minNum;
    lastHourNum = hourNum;
    lastDayNum = dayNum;
  }
  sprintf(localTimeStr, "%02d/%02d/%04d %02d:%02d:%02d", monthNum, dayNum, yearNum, hourNum, minNum, secondNum);
  if(!connectDateTimeSet)
  {
    strcpy(connectDateTime, localTimeStr);
    connectDateTimeSet = true;
  }
  #ifdef VERBOSE
  SERIALX.println(localTimeStr);
  #endif
}
// ================================ end NTP/RTC time functions ================================