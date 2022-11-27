/*
  ESP32_Credentials
  Brian B Smith
  November 2022
  brianbsmith.com
  info@brianbsmith.com
  
  Copyright (c) 2022 Brian B Smith. All rights reserved.

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

#include <Arduino.h>            // 
#include <WiFi.h>               // 
#include <ESPAsyncWebServer.h>  // https://github.com/me-no-dev/ESPAsyncWebServer
#include <AsyncTCP.h>           // https://github.com/me-no-dev/AsyncTCP
#include "FS.h"                 // 
#include "LITTLEFS.h"           // LittleFS_esp32 by loral from Arduino library manager
//#include "SPIFFS.h"             // 
#include <time.h>               // for NTP time
#include <ESP32Time.h>          // for RTC time https://github.com/fbiego/ESP32Time

#define VERBOSE            // more output for debugging
#define FORMAT_LITTLEFS_IF_FAILED true

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
#define gatewayPath  "/gateway.txt"
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
//const char* ntpServer = "pool.ntp.org";
#define ntpServer "pool.ntp.org"

long  gmtOffset_sec = 0;//-18000;
int   daylightOffset_sec = 0;// 3600;
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

//
// connect to wifi. if credentials not present/invalid, boot to access point and present page to get them
//
void setup()
{
  #ifdef VERBOSE
  Serial.begin(115200);
  #endif

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
    Serial.println("An error has occurred while mounting LITTLEFS");
    #endif
    return;
  }
  #ifdef VERBOSE
  Serial.println("LITTLEFS mounted successfully");
  #endif
}
//
// Read File from LITTLEFS
//
String LITTLEFS_ReadFile(fs::FS &fs, const char * path)
{
  #ifdef VERBOSE
  Serial.printf("Reading file: %s\r\n", path);
  #endif
  File file = fs.open(path);
  if(!file || file.isDirectory())
  {
    #ifdef VERBOSE
    Serial.println("- failed to open file for reading");
    #endif
    return String();
  }
  
  String fileContent;
  while(file.available())
  {
    fileContent = file.readStringUntil('\n');
    #ifdef VERBOSE
    Serial.println(fileContent);
    #endif
    //break;     
  }
  return fileContent;
}
//
// Write file to LITTLEFS
//
void LITTLEFS_WriteFile(fs::FS &fs, const char * path, const char * message)
{
  #ifdef VERBOSE
  Serial.printf("Writing file: %s\r\n", path);
  #endif
  File file = fs.open(path, FILE_WRITE);
  if(!file)
  {
    #ifdef VERBOSE
    Serial.println("- failed to open file for writing");
    #endif
    return;
  }
  if(file.print(message))
  {
    #ifdef VERBOSE
    Serial.println("- file written");
    #endif
  }
   else 
   {
    #ifdef VERBOSE
    Serial.println("- file write failed");
    #endif
  }
}
//
// list LITTLEFS files
//
void LITTLEFS_ListDir(fs::FS &fs, const char * dirname, uint8_t levels)
{
    Serial.printf("Listing directory: %s\r\n", dirname);

    File root = fs.open(dirname);
    if(!root)
    {
        Serial.println("- failed to open directory");
        return;
    }
    if(!root.isDirectory())
    {
        Serial.println(" - not a directory");
        return;
    }

    File file = root.openNextFile();
    while(file)
    {
        if(file.isDirectory())
        {
            Serial.print("  DIR : ");
            Serial.println(file.name());
            if(levels)
            {
                LITTLEFS_ListDir(fs, file.name(), levels -1);
            }
        }
         else 
         {
            Serial.print("  FILE: ");
            Serial.print(file.name());
            Serial.print("\tSIZE: ");
            Serial.println(file.size());
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
  Serial.printf("Deleting file: %s\r\n", path);
  #endif
  if(fs.remove(path))
  {
    #ifdef VERBOSE
    Serial.println("- file deleted");
    #endif
  }
  else 
  {
    #ifdef VERBOSE
    Serial.println("- delete failed");
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
    Serial.println("Undefined SSID");
    #endif    
    return false;
  }

  WiFi.mode(WIFI_STA);

  // for (optional) user defined gateway and local IP  
  localIP.fromString(ip.c_str());
  localGateway.fromString(gateway.c_str());
  if((ip != "") && (gateway != ""))
  {
    #ifdef VERBOSE
    Serial.printf("Configure wifi localIP %s gateway %s\n", localIP, localGateway);
    #endif
    if (!WiFi.config(localIP, localGateway, subnet))
    {
      #ifdef VERBOSE
      Serial.println("STA Failed to configure");
      #endif
      return false;
    }
  }
  else
  {
    #ifdef VERBOSE  
    Serial.println("Connect to wifi with DNS assigned IP");
    #endif
  }
  // set up and connect to wifi
  WiFi.begin(ssid.c_str(), pass.c_str());
  #ifdef VERBOSE
  Serial.println("Connecting to WiFi...");
  #endif
  unsigned long currentMillis = millis();
  previousMillis = currentMillis;

  while(WiFi.status() != WL_CONNECTED) 
  {
    currentMillis = millis();
    if (currentMillis - previousMillis >= WIFI_WAIT) 
    {
      #ifdef VERBOSE
      Serial.println("Failed to connect.");
      #endif
      return false;
    }
  }
  #ifdef VERBOSE
  Serial.print("Connected to wifi ");
  Serial.println(WiFi.localIP());
  #endif
  return true;
}
//
// load wifi credentials from LITTLEFS
//
void LoadCredentials()
{
  ssid = LITTLEFS_ReadFile(LITTLEFS, ssidPath);
  pass = LITTLEFS_ReadFile(LITTLEFS, passPath);
  ip = LITTLEFS_ReadFile(LITTLEFS, ipPath);
  gateway = LITTLEFS_ReadFile (LITTLEFS, gatewayPath);
  tz = LITTLEFS_ReadFile (LITTLEFS, tzPath);
  dst = LITTLEFS_ReadFile (LITTLEFS, dstPath);
  
  gmtOffset_sec = atoi(tz.c_str()) * 3600; // convert hours to seconds
  daylightOffset_sec = atoi(dst.c_str()) * 3600; // convert hours to seconds
  
  LITTLEFS_ReadFile(LITTLEFS, "/wifimanager.html");
  #ifdef VERBOSE
  Serial.print("SSID: ");
  Serial.println(ssid);
  Serial.print("PASSWORD: ");
  Serial.println(pass);
  Serial.print("Fixed IP (optional): ");
  Serial.println(ip);
  Serial.print("Fixed gateway (optional): ");
  Serial.println(gateway);
  Serial.print("Timezone offset: ");
  Serial.print(tz);
  Serial.print(" ");
  Serial.println(gmtOffset_sec);
  Serial.print("DST offset: ");
  Serial.print(dst);
  Serial.print(" ");
  Serial.println(daylightOffset_sec);
  #endif
}
//
// get new credentials from user from web page in access point mode
//
void GetCredentials()
{
  // Connect to Wi-Fi network with SSID and password
  #ifdef VERBOSE
  Serial.print("Setting AP (Access Point) ");
  #endif
  // NULL sets an open Access Point
  WiFi.softAP("ESP-WIFI-MANAGER", NULL);

  IPAddress IP = WiFi.softAPIP();
  #ifdef VERBOSE
  Serial.print(" address: ");
  Serial.println(IP); 
  #endif
  // Web Server Root URL
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    #ifdef VERBOSE
    Serial.println("Request from webserver, send page");
    #endif  
    request->send(LITTLEFS, "/wifimanager.html", "text/html");
  });
    
  server.serveStatic("/", LITTLEFS, "/");
  //
  // display web page and get credentials from user
  //  
  server.on("/", HTTP_POST, [](AsyncWebServerRequest *request) 
  {
    #ifdef VERBOSE
    Serial.println("Display web page and get credentials from user");
    #endif  
    int params = request->params();
    for(int i=0;i<params;i++)
    {
      AsyncWebParameter* p = request->getParam(i);
      if(p->isPost())
      {
        // HTTP POST ssid value
        if (p->name() == PARAM_INPUT_1) 
        {
          ssid = p->value().c_str();
          #ifdef VERBOSE
          Serial.print("SSID set to: ");
          Serial.println(ssid);
          #endif
          // Write file to save value
          LITTLEFS_WriteFile(LITTLEFS, ssidPath, ssid.c_str());
        }
        // HTTP POST password value
        if (p->name() == PARAM_INPUT_2) 
        {
          pass = p->value().c_str();
          #ifdef VERBOSE
          Serial.print("Password set to: ");
          Serial.println(pass);
          #endif
          // Write file to save value
          LITTLEFS_WriteFile(LITTLEFS, passPath, pass.c_str());
        }
        // HTTP POST local ip value
        if (p->name() == PARAM_INPUT_3) 
        {
          ip = p->value().c_str();
          #ifdef VERBOSE
          Serial.print("IP Address set to: ");
          Serial.println(ip);
          #endif
          // Write file to save value
          LITTLEFS_WriteFile(LITTLEFS, ipPath, ip.c_str());
        }
        // HTTP POST gateway ip value
        if (p->name() == PARAM_INPUT_4) 
        {
          gateway = p->value().c_str();
          #ifdef VERBOSE
          Serial.print("Gateway set to: ");
          Serial.println(gateway);
          #endif
          // Write file to save value
          LITTLEFS_WriteFile(LITTLEFS, gatewayPath, gateway.c_str());
        }
        // HTTP POST gateway ip value
        if (p->name() == PARAM_INPUT_5) 
        {
          tz = p->value().c_str();
          #ifdef VERBOSE
          Serial.print("Time zone offset set to: ");
          Serial.println(tz);
          #endif
          // Write file to save value
          LITTLEFS_WriteFile(LITTLEFS, tzPath, tz.c_str());
        }
        // HTTP POST gateway ip value
        if (p->name() == PARAM_INPUT_6) 
        {
          dst = p->value().c_str();
          #ifdef VERBOSE
          Serial.print("DST offset set to: ");
          Serial.println(dst);
          #endif
          // Write file to save value
          LITTLEFS_WriteFile(LITTLEFS, dstPath, dst.c_str());
        }
        //Serial.printf("POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
      }
    } 
    request->send(200, "text/plain", "Done. ESP will restart, connect to your router and go to IP address: " + ip);
    delay(WIFI_WAIT);
    ESP.restart();
  });
  server.begin();
}
//
// clear credentials and restart
// allows user to change wifi SSIDs easily
//
void ClearCredentials()
{
  #ifdef VERBOSE
  Serial.println("Clear WiFi credentials");
  #endif
  LITTLEFS_DeleteFile(LITTLEFS, ssidPath);
  LITTLEFS_DeleteFile(LITTLEFS, passPath);
  LITTLEFS_DeleteFile(LITTLEFS, ipPath);
  LITTLEFS_DeleteFile(LITTLEFS, gatewayPath);
  LITTLEFS_DeleteFile(LITTLEFS, tzPath);
  LITTLEFS_DeleteFile(LITTLEFS, dstPath);
  #ifdef VERBOSE
  Serial.println("Restart...");
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
  // if not set from NTP, get time and set RTC
  if(!rtcTimeSet)
  {
    #ifdef VERBOSE
    Serial.print("Time from NTP server ");
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
  }
  // use RTC for time
  else
  {
    #ifdef VERBOSE
    Serial.print("Time from local RTC ");
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
  Serial.println(localTimeStr);
  #endif
}
// ================================ end NTP/RTC time functions ================================