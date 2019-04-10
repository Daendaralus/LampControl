#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <stdio.h>
#include <FS.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include "ringstream.h"
#include "wifipw.h" //Contains my password and ssid so is not on the repository :)
#include <time.h>
#include "AM2320.h"
#include "Wire.h"
#define BUFFERSIZE 0xFFF
//const char* ssid = "";
//const char* password = "";
//const char* otapw = "";

/*
We want a web interface that:
-Controls lighting hours
-For which we need to control 2 GPIO pins
-maybe have a temperature/humidity sensor, which needs another 2 pins, SCL and SDA (I2C)

-maybe time frames as different configurations and calendar settings, configuring periods for which to use which configurations of hours

-Write timing stuff to memory. So parsing wil be necessary.


-reconfigure TX and RX to GPIO pins
-4bit  2d array, hour x minute -  


-Adjust the website obv
-rest function for updating time frames




TODO:
-parsing from web interface date format to array format
-checking time and date in loop to switch fans and light
-implement sleep to save money
-check sensor in some sensible interval. maybe store for graphs later idk


*/
AM2320 sensor;
ESP8266WebServer server(80);
LoopbackStream stream2(BUFFERSIZE); //Serial incoming + prints
const int led = LED_BUILTIN;
const int LIGHTPANEL1PIN = 1;
const int LIGHTPANEL2PIN = 3;
bool pin1=0, pin3 =0;

bool panelStatus = false;
bool fanStatus = false;

const String configFileName = "LightConfig.txt";
typedef uint8_t DailyConfiguration[24][60]; //Light configuration for a day, Minute resolution
std::map<int, std::pair<String, DailyConfiguration>> DailyConfigMap;
int CalendarToConfig[366]; //Configuration for a specific day

float lastHum = -1;
float lastTemp = -1;

void OTASetup();

template<typename ... Args>
String string_format( const String& format, Args ... args )
{
    size_t size = snprintf( nullptr, 0, format.c_str(), args ... ) + 1; // Extra space for '\0'
    char* buf =  new char[ size ] ; 
    snprintf( buf, size, format.c_str(), args ... );
    String out = String( buf );
    free(buf);
    return out; // We don't want the '\0' inside
}

time_t getLocalTime()
{
  return (time_t)time(nullptr) - 6*3600;
}

tm getLocalTM()
{
  auto now = getLocalTime();
  tm split = *localtime(&now);
  split.tm_mon+=1;

  split.tm_year+=1900;

  return split;
}

String getFormattedLocalTime()
{
  tm split = getLocalTM();
  return String(string_format("%d-%02d-%02d %02d:%02d:%02d", split.tm_year, split.tm_mon, split.tm_mday, split.tm_hour, split.tm_min, split.tm_sec));
}

//Stolen fromm :))) https://mariusbancila.ro/blog/2017/08/03/computing-day-of-year-in-c/
namespace datetools
{
   namespace details
   {
      constexpr unsigned int days_to_month[2][12] =
      {
         // non-leap year
         { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 },
         // leap year
         { 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335 },
      };
   }
 
   constexpr bool is_leap(int const year) noexcept
   {
      return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
   }
 
   constexpr unsigned int day_of_year(int const year, unsigned int const month, unsigned int const day)
   {
      return details::days_to_month[is_leap(year)][month - 1] + day;
   }
}


void startMessageLine()
{
 stream2.print(getFormattedLocalTime()+": ");
}
template <class T>
void StatusPrintln(T&& line)
{
  startMessageLine();
  stream2.println(line);
}



String readFile(String path) { // send the right file to the client (if it exists)
  startMessageLine();
  stream2.print("handleFileRead: " + path);
  //if (path.endsWith("/")) path += "index.html";         // If a folder is requested, send the index file
  String contentType = "text/html";            // Get the MIME type
  if (SPIFFS.exists(path)) {                            // If the file exists
    File file = SPIFFS.open(path, "r");                 // Open it
    auto text = file.readString();
    file.close();
    //size_t sent = server.streamFile(file, contentType); // And send it to the client
    //file.close();                                       // Then close the file again
    return text;
  }
  stream2.println("\tFile Not Found");
  return "";                                         // If the file doesn't exist, return false
}
String getContentType(String filename){
  if(filename.endsWith(".htm")) return "text/html";
  else if(filename.endsWith(".html")) return "text/html";
  else if(filename.endsWith(".css")) return "text/css";
  else if(filename.endsWith(".js")) return "application/javascript";
  else if(filename.endsWith(".png")) return "image/png";
  else if(filename.endsWith(".gif")) return "image/gif";
  else if(filename.endsWith(".jpg")) return "image/jpeg";
  else if(filename.endsWith(".ico")) return "image/x-icon";
  else if(filename.endsWith(".xml")) return "text/xml";
  else if(filename.endsWith(".pdf")) return "application/x-pdf";
  else if(filename.endsWith(".zip")) return "application/x-zip";
  else if(filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}
bool handleFileRead(String path){  // send the right file to the client (if it exists)
  startMessageLine();
  stream2.print("handleFileRead: " + path);
  if(path.endsWith("/")) path += "status.html";           // If a folder is requested, send the index file
  String contentType = getContentType(path);             // Get the MIME type
  if(SPIFFS.exists(path)){  // If the file exists, either as a compressed archive, or normal                                       // Use the compressed version
    File file = SPIFFS.open(path, "r");                    // Open the file
    size_t sent = server.streamFile(file, contentType);    // Send it to the client
    file.close();                                          // Close the file again
    stream2.println(String("\tSent file: ") + path);
    return true;
  }
  stream2.println(String("\tFile Not Found: ") + path);
  return false;                                          // If the file doesn't exist, return false
}

bool writeFile(String text, String path)
{
  StatusPrintln("Trying to write file...");
  int bufsize= text.length()*sizeof(char);
  StatusPrintln(string_format("file size is: %i", bufsize)); 
  FSInfo info;
  SPIFFS.info(info);

  unsigned int freebytes = info.totalBytes-info.usedBytes;
  StatusPrintln(string_format("free bytes on flash: %i", freebytes)); 
  return true;
}

String getFlashData()
{
  uint32_t realSize = ESP.getFlashChipRealSize();
  uint32_t ideSize = ESP.getFlashChipSize();
  FlashMode_t ideMode = ESP.getFlashChipMode();
  String mess = "";
  mess+=string_format("Flash real id:   %08X\n", ESP.getFlashChipId());
  mess+=string_format("Flash real size: %u bytes\n\n", realSize);

  mess+=string_format("Flash ide  size: %u bytes\n", ideSize);
  mess+=string_format("Flash ide speed: %u Hz\n", ESP.getFlashChipSpeed());
  mess+=string_format("Flash ide mode:  %s\n", (ideMode == FM_QIO ? "QIO" : ideMode == FM_QOUT ? "QOUT" : ideMode == FM_DIO ? "DIO" : ideMode == FM_DOUT ? "DOUT" : "UNKNOWN"));

  if (ideSize != realSize) {
    mess+=string_format("Flash Chip configuration wrong!\n");
  } else {
    mess+=string_format("Flash Chip configuration ok.\n");
  }
  FSInfo info;
  SPIFFS.info(info);
  mess+=string_format("total bytes on flash: %i\n", info.totalBytes);
  mess+=string_format("used bytes on flash: %i\n", info.usedBytes);
  return mess;
}

void handleFlash()
{
  digitalWrite(led, 1);
  String mess =getFlashData();
  server.send(200, "text/plain", mess);
  digitalWrite(led, 0);
}

void handleNotFound(){
  digitalWrite(led, 1);
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
  digitalWrite(led, 0);
}

void handleStatusData()
{
  StaticJsonBuffer<0x1FF> jsonBuffer; //TODO SOMETHING + STATUS BUFFER SIZE
  JsonObject& jsonObj = jsonBuffer.createObject();
  char JSONmessageBuffer[0x1FF];
  jsonObj["tval"] = lastTemp;
  jsonObj["hval"] = lastHum;
  jsonObj["light"] = panelStatus;
  jsonObj["fan"] = fanStatus;
  jsonObj["status"] = stream2.available()?stream2.readString():""; //TODO move time thing to read buffers part on condition of encountering a \n :)
  jsonObj.prettyPrintTo(JSONmessageBuffer, sizeof(JSONmessageBuffer));
  server.send(200, "application/json", JSONmessageBuffer);
}

// void handleUpdateTarget()
// {
//   int c = server.args();
//   for(int i =0; i<c;++i)
//   {
//     StatusPrintln(string_format("Arg %i: %s", i, server.arg(i).c_str())); 
//   }
//   StatusPrintln("Target update successful."); 
//   stream.print("$");stream.print((char)5); stream.print((char)1);stream.println("~");
//   //stream.println(string_format("$%c%c&%c~", (char)START_FLASH_LED, (char)1, char(95)));
//   blinking = ! blinking;
//   server.send(202);
// }

void handleSerialInput()
{
  bool nextcharisnumber=false;
  if(server.args() > 0&& server.hasArg("input"))
  {
    for(auto c : server.arg("input"))
    {
      if(c=='~')
        {digitalWrite(1, !pin1);
        pin1=!pin1; StatusPrintln("Set pin 1 to: " + string_format("%d :)", pin1));}
      else if(c=='-')
      {
        digitalWrite(3, !pin3);
        pin3=!pin3; 
      }

    }
    StatusPrintln("Got command! : "+ server.arg("input"));
    //stream.println("");
  }
  server.send(200);
}


//Have to parse the config stuff from the array format to time and date start/end ranges and vice versa
void handleConfigGet()
{

  server.send(200);
}

void handleConfigSet()
{
  if(server.args() > 1&& server.hasArg("timeranges") &&  server.hasArg("dateranges"))
  {
    //json sstuff
    
    for(auto c : server.arg("input"))
    {
      if(c=='~')
        {digitalWrite(1, !pin1);
        pin1=!pin1; StatusPrintln("Set pin 1 to: " + string_format("%d :)", pin1));}
      else if(c=='-')
      {
        digitalWrite(3, !pin3);
        pin3=!pin3; 
      }

    }
    StatusPrintln("Got command! : "+ server.arg("input"));
    //stream.println("");
  }
  server.send(202);
}


void addRESTSources()
{
  server.on("/get/status", HTTP_GET, handleStatusData);
  //server.on("/update/target", HTTP_PUT, handleUpdateTarget);
  server.on("/update/serialinput", HTTP_PUT, handleSerialInput);
  server.on("/get/config", HTTP_GET, handleConfigGet);
  server.on("/set/config", HTTP_PUT, handleConfigSet);
  
}


/*
//dailyconfigs
-id:name
--minuteconfig,.....

~dailyid,....

*/
void saveLightConfiguration()
{
  File f = SPIFFS.open(configFileName, "w");
  if(!f)
  {
    startMessageLine();
    StatusPrintln("Failed to open file while saving configuration!");
  }

  for(auto daily : DailyConfigMap)
  {
    f.printf("-%d:%s\n", daily.first, daily.second.first.c_str());
    f.print("--");

    for(auto hourly : daily.second.second)
    {
      for(int i = 0; i<60;++i)
      {
        f.print(hourly[i]);f.print(',');
      }
    }
    f.println();
  }
  f.println("~");
  for(int i = 0;i<366;++i)
  {
    f.printf("%d,", CalendarToConfig[i]);
  }
}

void readLightConfiguration()
{
  File f = SPIFFS.open(configFileName, "r");
  if(!f)
  {
    startMessageLine();
    StatusPrintln("Failed to open file while reading configuration! IMPORTANT. CHECK LIGHTS AND CONFIG PLEASE");
  }

  String curline =  f.readStringUntil('\n');
  int lastid;
  while(curline.length()>0)
  {
    if (curline.length() > 1 && curline[0] == '-' && curline[1] != '-')    {
      String idname = curline.substring(1); //id:name
      size_t delimiterpos = idname.indexOf(':'); 
      lastid = idname.substring(0, delimiterpos).toInt();
      String name = idname.substring(delimiterpos+1);
      DailyConfigMap[lastid].first = name;
    }
    else if(curline.length()>1&&curline[0] == '-' && curline[1] =='-' )
    {
        int curhour = 0;
        int curmin = 0;
        curline = curline.substring(2);
        int lastdelimiterindex=0;
        int nextdelimiterindex =curline.indexOf(',');
        while(true)
        {
          DailyConfigMap[lastid].second[curhour][curmin++] = curline.substring(lastdelimiterindex, nextdelimiterindex-lastdelimiterindex).toInt();
          if(curmin>59)
          {
            curhour++;
            curmin=0;
          }
          if(curhour>23)
          {
            break;
          }
          lastdelimiterindex = nextdelimiterindex+1;
          nextdelimiterindex = curline.indexOf(',', nextdelimiterindex);
          if (lastdelimiterindex == 0)
            break;
        }
    }
    else if(curline.length()>0 && curline[0] =='~')
    {
      int curday = 0;
        curline = curline.substring(1);
        int lastdelimiterindex=0;
        int nextdelimiterindex =curline.indexOf(',');
        while(true)
        {
          CalendarToConfig[curday++] = curline.substring(lastdelimiterindex, nextdelimiterindex-lastdelimiterindex).toInt();
          if(curday>365)
          {
            break;
          }
          lastdelimiterindex = nextdelimiterindex+1;
          nextdelimiterindex = curline.indexOf(',', nextdelimiterindex);
          if (lastdelimiterindex == 0)
					  break;
        }
    }
  curline =  f.readStringUntil('\n');
  }
}


void setup(void){
  stream2.clear();

  //GPIO 1 (TX) swap the pin to a GPIO.
  pinMode(1, FUNCTION_3); 
  //GPIO 3 (RX) swap the pin to a GPIO.
  pinMode(3, FUNCTION_3); 
  pinMode(3, OUTPUT);
    pinMode(1, OUTPUT);

  digitalWrite(1, HIGH);
  digitalWrite(3, HIGH);
  
  //AM2320 stuff
  Wire.begin(0,2);
  sensor.begin();

  //pinMode(led, OUTPUT);
  //digitalWrite(led, 0);
  //Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  stream2.println("");

  startMessageLine();
  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    stream2.print(".");
  }
  stream2.println("");
  startMessageLine();
  stream2.print("Connected to ");
  stream2.println(ssid); 
  startMessageLine();
  stream2.print("IP address: ");
  stream2.println(WiFi.localIP()); 

  if (MDNS.begin("shrooms")) {
    StatusPrintln("MDNS responder started"); 
  }

  server.on("/flash", HTTP_GET, handleFlash);

  addRESTSources();
  // server.on("/inline", [](){
  //   server.send(200, "text/plain", "this works as well");
  // });

  server.onNotFound([]() {                              // If the client requests any URI
      if (!handleFileRead(server.uri()))                  // send it if it exists
        handleNotFound(); // otherwise, respond with a 404 (Not Found) error
    });


  OTASetup();
  SPIFFS.begin();
  server.begin();
  StatusPrintln("HTTP server started");
  startMessageLine();
  stream2.print("free heap=");
  stream2.println(ESP.getFreeHeap());
  startMessageLine();
  stream2.print("free sketch space=");
  stream2.println(ESP.getFreeSketchSpace());
  configTime(10*3600, 0, "pool.ntp.org", "time.nist.gov");
  StatusPrintln("Waiting for time");
  startMessageLine();
  while (!time(nullptr)) {
    stream2.print(".");
    delay(1000);
  }
  startMessageLine();
  StatusPrintln("Reading lighting data...");
  readLightConfiguration();

  stream2.println("Success.");
}

void loop(void){
  //bufferSerial();

  ArduinoOTA.handle();
// if (delayRunning && ((millis() - delayStart) >= 2000)) {
//      stream2.print(stream2.available());
//   stream2.print(" | ");
//   stream2.println(stream2.pos);
//   delayStart = millis();
//   }
    static int last_result_time = 0;
    if(millis() - last_result_time > 1000)
    {
      sensor.measure();
      auto h = sensor.getHumidity();
      auto t = sensor.getTemperature();
      startMessageLine();

      StatusPrintln(string_format("Time:"));
      last_result_time = millis();
    }

    server.handleClient();
  //console_send();
  }

void OTASetup()
{
  ArduinoOTA.setHostname("root");
  ArduinoOTA.setPassword(otapw);
  ArduinoOTA.setPort(8266);
  ArduinoOTA.onStart([]() {
    stream2.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    stream2.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    stream2.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    stream2.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) stream2.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) stream2.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) stream2.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) stream2.println("Receive Failed");
    else if (error == OTA_END_ERROR) stream2.println("End Failed");
  });
  ArduinoOTA.begin();
  stream2.println("OTA ready"); 

}