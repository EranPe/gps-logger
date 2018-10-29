/*
GPS data logger. Collect cordinates and store in a text file on a micro SD card.
Using nodeMCU v3.
*/

#include <TinyGPS++.h>                                        // Tiny GPS Plus Library
#include <SoftwareSerial.h>                                   // Software Serial Library so we can use other Pins for communication with the GPS module
#include <EEPROM.h>                                           // EEPROM Library. To handle the EEPROM memory of the NodeMCU for storing the changeable variables
#include <SPI.h>                                              // This library allows you to communicate with SPI devices, with the Arduino as the master device
#include <SD.h>                                               // SD Library to create a file to save and update the coordinates (and other info) in
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define OLED_RESET LED_BUILTIN  //4
Adafruit_SSD1306 display(OLED_RESET);

#include "ConvertUTC.cpp"
#include "WifiWebServer.h"                                       // wifi library to handle the wireless connection of the NodeMCU (ESP8266) with the surroundings (wifi direct - SoftAP)

static const int RXPin = 4, TXPin = 5;                        // Ublox 6m GPS module to pins 4 and 5
static const uint32_t GPSBaud = 9600;                         // Ublox GPS default Baud Rate is 9600

static const int mosi = 13, miso = 12, sck = 14, cs = 15;     // SD card attached to SPI bus as follows: MOSI - pin 13, MISO - pin 12, CLK/SCK - pin 14, CS - pin 15
 
static bool hasSD = false;

static const int batteryPin  = A0;                            // Battery input (A0 - analog)
bool WriteBatteryStatusToFile = false;                         // Debugging battery status
File batteryFile;
unsigned long batteryTime;
String batteryPath = "battery.txt";
int currentBatteryPercent = 100;

static const int chooseButtonPin = 16;                        // choose button pin - 16
int buttonState = 0;
int mode = 0;                                                 // Mode/state of the system at default (0 - GPS logger, 1 - Web server)
int counter = 5;                                              // Counter at setup to change to "Web" Mode or not ("GPS Logger" Mode)

static const int setButtonPin = 2;                           // set button pin - 2
int option = 0;                                               // options of the shown data on display during GPS logger mode (4)

TinyGPSPlus gps;                                              // Create an Instance of the TinyGPS++ object called gps
SoftwareSerial gpsSerial(RXPin, TXPin);                       // The serial connection to the GPS device

File dataFile;                                                // Files of logging and battery
                         
String fileName = "20000000.txt";                             // File name format: yyyymmdd
String directoryName = "gpslog";
String filePath;
String dateAndTime;
String date;
bool isFileCreated = false;

static const int UTC = 0;                                     // GPS time is UTC which is zero.
unsigned long smartDelayTime = 500;

// Default changeable variables. Will changed (via settings web page) according to the data that stored in the EEPROM memory (Or not if it is the first use)
float TimeZone = UTC;                                         // Time Zone. Jerusalem, for example, is UTC +2. India: UTC +5.5 (UTC +5:30). Nepal: UTC +5.75 (UTC +5:45)
int DST = 0;                                                  // DST - Daylight saving time
int gpsSampleTime = 500;                                      // GPS sample time

// Wifi variables
String host = "esp8266sd";                                    // Name of host (local host)
String wifiStatus;

// start time of current period of gps sample time
unsigned long start = 0;

// previous and current Latitude and Longitude and distance
double prevLat, prevLng, currLat, currLng;
double distance_km;
double totalDistance_km;

// Current time of logging and previous time
int currHour, currMin, currSec;
int prevHour = -1, prevMin = -1, prevSec = -1;

// total time so far
int totalTime = 0;
int totalTimeHours = 0;
int totalTimeMinutes = 0;
int totalTimeSeconds = 0;
String totalTimeSecondsStr, totalTimeMinutesStr;

// New (=1) or old track (=0)
int newTrack = 1;

// Position of the first line description (summary) and the end point
int pos = 0, EndPointPos = 0;

WifiWebServer WifiWebServer(host, directoryName);             // SoftAP SSID host (+mdns host) and the directory that contains the log files

void printDisplay(String text, int textSize, int displayTime) // Prints a new text on the display in "textSize" size for some time (or forever, when displayTime = 0) 
{
  display.clearDisplay();
  display.display();

  display.setTextSize(textSize);
  display.setTextColor(WHITE);
  display.setCursor(0,0);
  display.println(text);
  display.display();
  
  if (displayTime != 0)                                        // display text for some time (0 = display all the time)
  {
    delay(displayTime);
    display.clearDisplay();
    display.display();
  }
}

void setupDisplay()                                           // Begins and clears the display 
{
  Wire.begin(1,3);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

  // Clear the buffer.
  display.clearDisplay();
  display.display();

  /*display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(0,0);
  display.println("GPS Logger");
  display.println("Ver. 1.0");
  display.display();*/
}

void smartDelay(unsigned long ms)                             // This custom version of delay() ensures that the gps object is being "fed".
{
  unsigned long start = millis();
  do 
  {
    while (gpsSerial.available())
      gps.encode(gpsSerial.read());
  } while (millis() - start < ms);
}

void CreateLogFile(String path, String date)       // Create a new log file
{                    
  if(SD.exists((char *)path.c_str())) {
    Serial.println("Already created. No need to create a new one.");
    return;
  }

  if(path.indexOf('.') > 0){
    File file = SD.open((char *)path.c_str(), FILE_WRITE);
    if(file){
      file.print("Started logging on: ");   
      file.print(date);                                           // Write the date
      file.println(" ");
      // Write type (T = tracking), new track (1 = yes, 0 = no, same track), latitude, longitude, time (HH:MM:SS)
      // No. of satellites, elevation(m), speed(kmph), course
      // elapsed time (HH:MM:SS, so far), total distance (kilometers so far) description (summary) and color headers
      file.println("type, new_track, latitude, longitude, time, satellites, elevation (m), speed (kmph), course, elapsed time, total distance (km), description, color");
      file.close();
    }
  }
  Serial.println("OK");
}

void CreatePath()
{
  String y, m, d;
  
  Serial.print("Chceking for a valid file name...");
  do
  {
    dateAndTime = ConvertUTC::localTime(gps.date.value(), gps.time.value(), TimeZone, DST, ConvertUTC::isLeapYear(gps.date.year()));
    delay(500);
  
    y = String(gps.date.year());                               // Save the year, month and day
    m = String(dateAndTime[2]) + String(dateAndTime[3]);
    d = String(dateAndTime[4]) + String(dateAndTime[5]);
  
    fileName = y + m + d + ".txt";
    
    Serial.println("The file name is invalid!");
    Serial.print("Waiting for data (3 seconds)");                   // Wait 3 seconds and restart
    delay(1000); Serial.print('.');    
    delay(1000); Serial.print('.');    
    delay(1000); Serial.print('.');
  } while ((fileName == "20000000.txt") || (fileName == "20000001.txt"));   // If the name didn't change due to a fail in retrieving the date
  Serial.println("OK");

  Serial.println("Folder name is: " + directoryName);
  Serial.println("File name is: " + fileName);

  filePath = directoryName + '/' + fileName;                // Combine directory name and file name to get the file path
  Serial.println("The path file is: " + filePath);

  SD.mkdir((char *)directoryName.c_str());                  // Make a new directory which the gps logs will be sotred in (If hasn't already existed)
    
  date = String(d) + '/' + String(m) + '/' + String(y);
}

void createFile()
{
  Serial.print("Creating a directory and a valid file name (file path)...");
  CreatePath();
  
  Serial.print("Creating a new log file...");
  CreateLogFile(filePath, date);

  isFileCreated = true;
}

void gpsLoggerStart()
{
  Serial.println("GPS Logger mode"); 
  Serial.println(TinyGPSPlus::libraryVersion());
  delay(1500);                                                // Pause 1.5 seconds
  Serial.println();
  Serial.println("Starting GPS serial...");
  gpsSerial.begin(GPSBaud);                                   // Set Software Serial Comm Speed to 9600
}

void readFromEEPROMMemory()
{
  String TimeZoneStr;                             // Time Zone format - sxx:xx (s - sign +/-, xx:xx - time), for example: "+03:30" (6 bytes)
  for (int i = 100; i < 106; ++i) {
    TimeZoneStr += char(EEPROM.read(i));
  }
  if (TimeZoneStr =='\0') {
    Serial.print("No Time Zone in memory.");
    Serial.println(" Using default: " + String(TimeZone));
  }
  else {
    TimeZone = (TimeZoneStr[1]-48)*10 + (TimeZoneStr[2]-48);
    TimeZone += float((TimeZoneStr[4]-48)*10 + (TimeZoneStr[5]-48))/60;
    
    if (TimeZoneStr[0] =='-') TimeZone = (-1) * TimeZone;
    Serial.println("TimeZone = " + String(TimeZone));
  }
  
  char DSTStr = char(EEPROM.read(106));           // DST - '1' or '0' (1 byte)
  if (DSTStr =='\0') {
    Serial.print("No DST in memory.");
    Serial.println(" Using default: " + String(DST));
  }
  else {
    DST = DSTStr - 48;
    Serial.println("DST = " + String(DST));
  }

  String gpsSampleTimeStr;                        // GPS Sample Time - "xx:yy" (xx - minutes between 0 and 60, yy - seconds between 1 and 59), for example: 01:30, i.e, 1 minute and 30 seconds (5 bytes)
  for (int i = 107; i < 112; ++i) {
    gpsSampleTimeStr += char(EEPROM.read(i));
  }
  if (gpsSampleTimeStr =='\0') {
    Serial.print("No GPS Sample Time in memory.");
    Serial.println(" Using default: " + String(gpsSampleTime));
  }
  else {
    int minutes = (gpsSampleTimeStr[0]-48)*10 + (gpsSampleTimeStr[1]-48);
    gpsSampleTime = (minutes*60)*1000;                                                      // minutes to miliseconds
    int seconds = (gpsSampleTimeStr[3]-48)*10 + (gpsSampleTimeStr[4]-48);
    gpsSampleTime += seconds*1000;                                                          // seconds to miliseconds
    gpsSampleTime -= smartDelayTime;                                                        // Minus the smart delay time
    
    Serial.println("gpsSampleTime = " + gpsSampleTimeStr);
    Serial.print("Total Time = ");
    Serial.println(gpsSampleTime + smartDelayTime);                                         //  Total time = gpsSampleTime + smartDelayTime
  }
}

int batteryStatus(int batteryPin)
{
  int battery = 0;
  battery = analogRead(batteryPin);                    // Read battery voltage (Analog read: 0 - 1023)
  if (battery > 765) battery = 765;                    // Limit the maximmum value of the battery (max: 765)
  if (battery < 585) battery = 585;                    // Limit the minimmum value of the battery (min: 585)
  return map(battery, 585, 765 , 1, 100);              // Map battery to (1 - 100) units from (585 - 765) units
}

double distanceKm(double currLat, double currLng, double prevLat, double prevLng)
{
  double distanceKm =
  gps.distanceBetween(
    prevLat,
    prevLng,
    currLat,
    currLng) / 1000.0;
    return distanceKm;
}

double distanceM(double currLat, double currLng, double prevLat, double prevLng)
{
  double distanceM =
  gps.distanceBetween(
    prevLat,
    prevLng,
    currLat,
    currLng);
    return distanceM;
}

void elapsedTime()
{
  currHour = (int)dateAndTime[6] * 10 + (int)dateAndTime[7];
  currMin = (int)dateAndTime[8] * 10 + (int)dateAndTime[9];
  currSec = (int)dateAndTime[10] * 10 + (int)dateAndTime[11];
  
  if ((prevSec != -1) || (prevMin != -1) || (prevHour != -1))
  {
    if ((currHour - prevHour) < 0) totalTimeHours += 24 + currHour - prevHour;
    else totalTimeHours += currHour - prevHour;
    
    if ((currMin - prevMin) < 0)
    {
      totalTimeMinutes += 60 + currMin - prevMin;
      totalTimeHours--;
    }
    else totalTimeMinutes += currMin - prevMin;
    
    if ((currSec - prevSec) < 0) 
    {
      totalTime += 60 + currSec - prevSec;
      totalTimeMinutes--;
    }
    else totalTime += currSec - prevSec;
  }
  
  prevHour = currHour;
  prevMin = currMin;
  prevSec = currSec;
  
  totalTimeSeconds = totalTime % 60;
  totalTimeMinutes = (totalTime / 60) % 60;
  totalTimeHours = (totalTime / 60) / 60;
  
  if (totalTimeSeconds < 10) totalTimeSecondsStr = '0' + String(totalTimeSeconds);
  else totalTimeSecondsStr = String(totalTimeSeconds);
  if (totalTimeMinutes < 10) totalTimeMinutesStr = '0' + String(totalTimeMinutes);
  else totalTimeMinutesStr = String(totalTimeMinutes);
}

void drawClockIcon(int x, int y)
{
  display.drawPixel(x+2,y+0,WHITE);
  display.drawPixel(x+3,y+0,WHITE);
  display.drawPixel(x+4,y+0,WHITE);
  display.drawPixel(x+5,y+0,WHITE);

  display.drawPixel(x+1,y+1,WHITE);
  display.drawPixel(x+6,y+1,WHITE);

  display.drawPixel(x+0,y+2,WHITE);
  display.drawPixel(x+3,y+2,WHITE);
  display.drawPixel(x+7,y+2,WHITE);
  
  display.drawPixel(x+0,y+3,WHITE);
  display.drawPixel(x+3,y+3,WHITE);
  display.drawPixel(x+7,y+3,WHITE);

  display.drawPixel(x+0,y+4,WHITE);
  display.drawPixel(x+3,y+4,WHITE);
  display.drawPixel(x+4,y+4,WHITE);
  display.drawPixel(x+5,y+4,WHITE);
  display.drawPixel(x+7,y+4,WHITE);

  display.drawPixel(x+0,y+5,WHITE);
  display.drawPixel(x+7,y+5,WHITE);

  display.drawPixel(x+1,y+6,WHITE);
  display.drawPixel(x+6,y+6,WHITE);

  display.drawPixel(x+2,y+7,WHITE);
  display.drawPixel(x+3,y+7,WHITE);
  display.drawPixel(x+4,y+7,WHITE);
  display.drawPixel(x+5,y+7,WHITE);

  display.display();
}

void drawFullBatteryIcon(int x, int y)
{
  display.drawPixel(x+0,y+1,WHITE);
  display.drawPixel(x+1,y+1,WHITE);
  display.drawPixel(x+2,y+1,WHITE);
  display.drawPixel(x+3,y+1,WHITE);
  display.drawPixel(x+4,y+1,WHITE);
  display.drawPixel(x+5,y+1,WHITE);
  display.drawPixel(x+6,y+1,WHITE);

  display.drawPixel(x+0,y+2,WHITE);
  display.drawPixel(x+1,y+2,WHITE);
  display.drawPixel(x+2,y+2,WHITE);
  display.drawPixel(x+3,y+2,WHITE);
  display.drawPixel(x+4,y+2,WHITE);
  display.drawPixel(x+5,y+2,WHITE);
  display.drawPixel(x+6,y+2,WHITE);

  display.drawPixel(x+0,y+3,WHITE);
  display.drawPixel(x+1,y+3,WHITE);
  display.drawPixel(x+2,y+3,WHITE);
  display.drawPixel(x+3,y+3,WHITE);
  display.drawPixel(x+4,y+3,WHITE);
  display.drawPixel(x+5,y+3,WHITE);
  display.drawPixel(x+6,y+3,WHITE);
  display.drawPixel(x+7,y+3,WHITE);

  display.drawPixel(x+0,y+4,WHITE);
  display.drawPixel(x+1,y+4,WHITE);
  display.drawPixel(x+2,y+4,WHITE);
  display.drawPixel(x+3,y+4,WHITE);
  display.drawPixel(x+4,y+4,WHITE);
  display.drawPixel(x+5,y+4,WHITE);
  display.drawPixel(x+6,y+4,WHITE);
  display.drawPixel(x+7,y+4,WHITE);

  display.drawPixel(x+0,y+5,WHITE);
  display.drawPixel(x+1,y+5,WHITE);
  display.drawPixel(x+2,y+5,WHITE);
  display.drawPixel(x+3,y+5,WHITE);
  display.drawPixel(x+4,y+5,WHITE);
  display.drawPixel(x+5,y+5,WHITE);
  display.drawPixel(x+6,y+5,WHITE);

  display.drawPixel(x+0,y+6,WHITE);
  display.drawPixel(x+1,y+6,WHITE);
  display.drawPixel(x+2,y+6,WHITE);
  display.drawPixel(x+3,y+6,WHITE);
  display.drawPixel(x+4,y+6,WHITE);
  display.drawPixel(x+5,y+6,WHITE);
  display.drawPixel(x+6,y+6,WHITE);
  
  display.display();
}

void drawHalfBatteryIcon(int x, int y)
{
  display.drawPixel(x+0,y+1,WHITE);
  display.drawPixel(x+1,y+1,WHITE);
  display.drawPixel(x+2,y+1,WHITE);
  display.drawPixel(x+3,y+1,WHITE);
  display.drawPixel(x+4,y+1,WHITE);
  display.drawPixel(x+5,y+1,WHITE);
  display.drawPixel(x+6,y+1,WHITE);

  display.drawPixel(x+0,y+2,WHITE);
  display.drawPixel(x+1,y+2,WHITE);
  display.drawPixel(x+2,y+2,WHITE);
  display.drawPixel(x+3,y+2,WHITE);
  display.drawPixel(x+6,y+2,WHITE);

  display.drawPixel(x+0,y+3,WHITE);
  display.drawPixel(x+1,y+3,WHITE);
  display.drawPixel(x+2,y+3,WHITE);
  display.drawPixel(x+3,y+3,WHITE);
  display.drawPixel(x+6,y+3,WHITE);
  display.drawPixel(x+7,y+3,WHITE);

  display.drawPixel(x+0,y+4,WHITE);
  display.drawPixel(x+1,y+4,WHITE);
  display.drawPixel(x+2,y+4,WHITE);
  display.drawPixel(x+3,y+4,WHITE);
  display.drawPixel(x+6,y+4,WHITE);
  display.drawPixel(x+7,y+4,WHITE);

  display.drawPixel(x+0,y+5,WHITE);
  display.drawPixel(x+1,y+5,WHITE);
  display.drawPixel(x+2,y+5,WHITE);
  display.drawPixel(x+3,y+5,WHITE);
  display.drawPixel(x+6,y+5,WHITE);

  display.drawPixel(x+0,y+6,WHITE);
  display.drawPixel(x+1,y+6,WHITE);
  display.drawPixel(x+2,y+6,WHITE);
  display.drawPixel(x+3,y+6,WHITE);
  display.drawPixel(x+4,y+6,WHITE);
  display.drawPixel(x+5,y+6,WHITE);
  display.drawPixel(x+6,y+6,WHITE);
  
  display.display();
}

void drawEmptyBatteryIcon(int x, int y)
{
  display.drawPixel(x+0,y+1,WHITE);
  display.drawPixel(x+1,y+1,WHITE);
  display.drawPixel(x+2,y+1,WHITE);
  display.drawPixel(x+3,y+1,WHITE);
  display.drawPixel(x+4,y+1,WHITE);
  display.drawPixel(x+5,y+1,WHITE);
  display.drawPixel(x+6,y+1,WHITE);

  display.drawPixel(x+0,y+2,WHITE);
  display.drawPixel(x+6,y+2,WHITE);

  display.drawPixel(x+0,y+3,WHITE);
  display.drawPixel(x+6,y+3,WHITE);
  display.drawPixel(x+7,y+3,WHITE);

  display.drawPixel(x+0,y+4,WHITE);
  display.drawPixel(x+6,y+4,WHITE);
  display.drawPixel(x+7,y+4,WHITE);

  display.drawPixel(x+0,y+5,WHITE);
  display.drawPixel(x+6,y+5,WHITE);

  display.drawPixel(x+0,y+6,WHITE);
  display.drawPixel(x+1,y+6,WHITE);
  display.drawPixel(x+2,y+6,WHITE);
  display.drawPixel(x+3,y+6,WHITE);
  display.drawPixel(x+4,y+6,WHITE);
  display.drawPixel(x+5,y+6,WHITE);
  display.drawPixel(x+6,y+6,WHITE);
  
  display.display();
}

void drawSocketIcon(int x, int y)
{
  display.drawPixel(x+1,y+0,WHITE);
  display.drawPixel(x+4,y+0,WHITE);
  
  display.drawPixel(x+1,y+1,WHITE);
  display.drawPixel(x+4,y+1,WHITE);

  display.drawPixel(x+0,y+2,WHITE);
  display.drawPixel(x+1,y+2,WHITE);
  display.drawPixel(x+2,y+2,WHITE);
  display.drawPixel(x+3,y+2,WHITE);
  display.drawPixel(x+4,y+2,WHITE);
  display.drawPixel(x+5,y+2,WHITE);

  display.drawPixel(x+1,y+3,WHITE);
  display.drawPixel(x+2,y+3,WHITE);
  display.drawPixel(x+3,y+3,WHITE);
  display.drawPixel(x+4,y+3,WHITE);

  display.drawPixel(x+1,y+4,WHITE);
  display.drawPixel(x+2,y+4,WHITE);
  display.drawPixel(x+3,y+4,WHITE);
  display.drawPixel(x+4,y+4,WHITE);

  display.drawPixel(x+2,y+5,WHITE);
  display.drawPixel(x+3,y+5,WHITE);
  display.drawPixel(x+8,y+5,WHITE);
  display.drawPixel(x+9,y+5,WHITE);

  display.drawPixel(x+3,y+6,WHITE);
  display.drawPixel(x+7,y+6,WHITE);

  display.drawPixel(x+4,y+6,WHITE);
  display.drawPixel(x+5,y+6,WHITE);
  display.drawPixel(x+6,y+6,WHITE);
  
  display.display();
}

void setup()
{
  pinMode(chooseButtonPin, INPUT);         // button pin
  pinMode(setButtonPin, INPUT);     // set button pin
  
  Serial.begin(115200);

  setupDisplay();

  EEPROM.begin(512);
  delay(500);
  Serial.println("\n");
  Serial.println("Reading data stored in EEPROM memory:");
  readFromEEPROMMemory();

  Serial.println();
  Serial.print("Initializing SD card...");                  //setup the SD card
  delay(1500);

  if (SD.begin(SS))                                        // See if the SD card is present and can be initialized
  {
    Serial.println("card initialized.");
    printDisplay("card initialized.", 1, 3000); 
    hasSD = true;                          
  } else {
    Serial.println("Card failed, or not present.");         // If SD isn't ready don't do anything more
    Serial.println("Please restart");
    printDisplay("Card failed,\nor not present.\nPlease restart", 1, 3000);                     
  }

  printDisplay("GPS Logger\nVer. 1.2", 2, 3000);

   display.clearDisplay();
   display.display();
  
   display.setTextSize(1);
   display.setTextColor(WHITE);
   display.setCursor(0,0);
   display.println("Press button to\nchange to web mode");
   
   display.setTextSize(2);
   display.setTextColor(WHITE, BLACK);
  
  while (counter > 0)
  {
   display.setCursor(48,16);
   display.print('(');
   display.print(counter--);
   display.print(')');
   display.display();
   delay(1000);

   buttonState = digitalRead(chooseButtonPin);
   if (!buttonState == HIGH)
   {                                        
     Serial.println("Button was pressed. Changing mode...");
     mode = 1;
     counter = 0;
   }
  }
    
  Serial.println();
  Serial.println("Battery: " + String(batteryStatus(batteryPin)) + '%');
  Serial.println();
  
  switch (mode) {
    case 0:
      printDisplay("GPS Logger\nmode", 2, 0);
      gpsLoggerStart();
      break;
    case 1:
      printDisplay("Web Server\nmode", 2, 0);
      wifiStatus = WifiWebServer.start();        // Start Wifi connection process (Wifi direct with the system or Wifi connection to a network with a ssid and a password) and print files in the log directory to the client (for downloading) 
      printDisplay(wifiStatus, 1, 0);
      break;
    default: 
      // statements
    break;
  }
}
     
void loop()
{
  if (WriteBatteryStatusToFile && ((millis() / 1000) % 300 == 0)) {     // Debugging: Saving Battery status every 5 minutes
     batteryFile = SD.open((char *)batteryPath.c_str(), FILE_WRITE);    // Write Battery status to file
    if (batteryFile)
    {
      int battery = 0;
      battery = analogRead(batteryPin);
      batteryTime = millis();
      //dateAndTime = ConvertUTC::localTime(gps.date.value(), gps.time.value(), TimeZone, DST, ConvertUTC::isLeapYear(gps.date.year()));
      
      batteryFile.print("Battery (analog read): ");
      batteryFile.print(battery);
      batteryFile.println(", Time (in seconds): " + String(batteryTime / 1000));
     // dataFile.println(", Time: " + dateAndTime);
      batteryFile.close();
      Serial.println("Battery: " + String(batteryStatus(batteryPin)) + '%');
    }
  }
  
 buttonState = digitalRead(chooseButtonPin);
 if (!buttonState == HIGH) {                                         // Button is pressed
  Serial.println("Button was pressed. Changing mode...");
  delay(1000);
  if (mode == 0) {                                                  // Flip the mode 1->0, 0->1. Setup each mode accordingly
    /*mode = 1;
    printDisplay("Web Server\nmode", 2, 0);
    wifiStatus = WifiWebServer.start();
    printDisplay(wifiStatus, 1, 0);*/
    option++;
    if (option == 4)
    {
      display.clearDisplay();
      display.display();
    }
    if (option > 4) option = 0;
  }
  else {
    mode = 0;
    printDisplay("GPS Logger\nmode", 2, 0);
    readFromEEPROMMemory();
    gpsLoggerStart();
  }
 }
  switch (mode) {
    case 0:
    {
      // change options of display in GPS logger mode every change made by the user
      /*if (digitalRead(setButtonPin))
      {
        option++;
        if (option == 3)
        {
          display.clearDisplay();
          display.display();
        }
        if (option > 3) option = 0;
      }*/
  
      // Calculate the local time according to the UTC time received from the GPS module
      dateAndTime = ConvertUTC::localTime(gps.date.value(), gps.time.value(), TimeZone, DST, ConvertUTC::isLeapYear(gps.date.year()));
      String h = String(dateAndTime[6]) + String(dateAndTime[7]);
      String m = String(dateAndTime[8]) + String(dateAndTime[9]);
      String s = String(dateAndTime[10]) + String(dateAndTime[11]);
    
      // Print to console the location (latitude, longitude), No. of satellites, Elevation, Time in UTC, Local time, Heading and Speed
      Serial.println();
      Serial.print("Latitude   : ");
      Serial.println(gps.location.lat(), 6);
      Serial.print("Longitude  : ");
      Serial.println(gps.location.lng(), 6);
      Serial.print("Satellites : ");
      Serial.println(gps.satellites.value());
      Serial.print("Elevation  : ");
      Serial.print(gps.altitude.meters());
      Serial.println("m"); 
      Serial.print("Time UTC   : ");
      Serial.print(gps.time.hour());                              // GPS time UTC 
      Serial.print(":");
      Serial.print(gps.time.minute());                            // Minutes
      Serial.print(":");
      Serial.println(gps.time.second());                          // Seconds
      Serial.print("Local Time : ");
      Serial.print(h);                                            // Local time
      Serial.print(":");
      Serial.print(m);                                            // Minutes
      Serial.print(":");
      Serial.println(s);                                          // Seconds
      Serial.print("Heading    : ");
      Serial.println(gps.course.deg());
      Serial.print("Speed(kmph): ");
      Serial.println(gps.speed.kmph());

      if ((millis() - start) > gpsSampleTime)                     // Make sure a time passed that is over gps sample time to display
      {
        if (batteryStatus(batteryPin) < currentBatteryPercent)
          currentBatteryPercent = batteryStatus(batteryPin);
        if (option != 4) {
          if (analogRead(batteryPin) < 10) printDisplay("  " + h + ":" + m + ":" + s, 1, 0);
          else printDisplay("  " + h + ":" + m + ":" + s + "   " + String(currentBatteryPercent) + '%' + ' ' /*+ char(14)sound enabled/disabled + char(8) sd card inserted/or not*/, 1, 0);  /// Status bar on display
          drawClockIcon(0, 0);
          if (currentBatteryPercent > 75) drawFullBatteryIcon(65, 0);
          else if (currentBatteryPercent > 25) drawHalfBatteryIcon(65, 0);
          else if (analogRead(batteryPin) < 10) drawSocketIcon(65,0);
          else drawEmptyBatteryIcon(65, 0);
        }
        switch (option) {
        case 0:
        {
          display.print("latitude : ");
          display.println(gps.location.lat(), 6);
          display.print("longitude: ");
          display.println(gps.location.lng(), 6);
          break;
        }
        case 1:
        {
          display.print("Satellites : ");
          display.println(gps.satellites.value());
          display.print("Elevation  : ");
          display.print(gps.altitude.meters());
          display.println("m");
          break;
        }
        case 2:
        {
          display.print("Heading    : ");
          display.println(gps.course.deg());
          display.print("Speed(kmph): ");
          display.println(gps.speed.kmph());
          break;
        }
        case 3:
        {
          elapsedTime();
          display.print("Total time: ");
          display.print(totalTimeHours);             // Total (accumulated) hours time
          display.print(":");
          display.print(totalTimeMinutesStr);           // Total (accumulated) minutes time
          display.print(":");  
          display.println(totalTimeSecondsStr);          // Total (accumulated) seconds time        
          display.print("Kilometers: ");
          display.println(totalDistance_km);
        }
        case 4:
        {
          break;
        }
        default: 
        // statements
        break;
        }
        display.display();
      }
    
      if (gps.location.isValid()) {                              // Check if the gps location (coordinates) is ready
        if (gps.location.age() < 1500) {                          // If this returns a value greater than 1500 or so, it may be a sign of a problem like a lost fix.
          if ((millis() - start) > gpsSampleTime) {                    // Make sure a time passed that is over gps sample time to display (Save coordinates every GPS sample time)
            if (!isFileCreated) createFile();
            dataFile = SD.open((char *)filePath.c_str(), FILE_WRITE);    // Write to file
            if (dataFile) {                                              // If the file is opened it's ready to be written
              // Calculate the local time according to the UTC time received from the GPS module
              dateAndTime = ConvertUTC::localTime(gps.date.value(), gps.time.value(), TimeZone, DST, ConvertUTC::isLeapYear(gps.date.year()));
              h = String(dateAndTime[6]) + String(dateAndTime[7]);
              m = String(dateAndTime[8]) + String(dateAndTime[9]);
              s = String(dateAndTime[10]) + String(dateAndTime[11]);

              currLat = gps.location.lat();
              currLng = gps.location.lng();

              if ((prevLat == 0) && (prevLng == 0)) distance_km = 0;
              else distance_km = distanceKm(prevLat, prevLng, currLat, currLng);

              prevLat = currLat;
              prevLng = currLng;

              totalDistance_km += distance_km;

              elapsedTime();

              int satellitesValue = gps.satellites.value();
              float altitudeValue = gps.altitude.meters();
              float speedValue = gps.speed.kmph();
              float courseValue= gps.course.deg();
                
              Serial.print("Data is valid! Printing to file...");     // Print the valid data to the data file (location, time and others)

              // Starting point of the track (waypoint)
              if (newTrack == 1)
              {
                dataFile.print('W');
                dataFile.print(",, ");
                dataFile.print(currLat, 6);
                dataFile.print(", ");
                dataFile.print(currLng, 6);
                dataFile.print(", ");
                dataFile.print(h);                          // Local time
                dataFile.print(":");
                dataFile.print(m);                          // Minutes
                dataFile.print(":");  
                dataFile.print(s);                          // Seconds
                dataFile.print(",,,,,,, ");
                dataFile.println("Start, green");

                EndPointPos = dataFile.position();
                dataFile.println("                                                  ");
              }
              //
              
              dataFile.print('T');                        // Track point (W - Waypoint, T - Trackpoint, R - Routepoint)
              dataFile.print(", ");
              dataFile.print(newTrack);                   // New track or the same (old) track
              dataFile.print(", ");
              dataFile.print(currLat, 6);
              dataFile.print(", ");
              dataFile.print(currLng, 6);
              dataFile.print(", ");
              dataFile.print(h);                          // Local time
              dataFile.print(":");
              dataFile.print(m);                          // Minutes
              dataFile.print(":");  
              dataFile.print(s);                          // Seconds
              dataFile.print(", ");
              dataFile.print(satellitesValue);            // No. of satellites
              dataFile.print(", ");
              dataFile.print(altitudeValue);              // Alt/Altitude/Elevation 
              dataFile.print(", ");
              dataFile.print(speedValue);                 // Speed/Velocity 
              dataFile.print(", ");
              dataFile.print(courseValue);                // Course/Heading
              dataFile.print(", ");
              dataFile.print(totalTimeHours);             // Total (accumulated) hours time
              dataFile.print(":");
              dataFile.print(totalTimeMinutesStr);        // Total (accumulated) minutes time
              dataFile.print(":");  
              dataFile.print(totalTimeSecondsStr);        // Total (accumulated) seconds time 
              dataFile.print(", ");
              dataFile.println(totalDistance_km);         // Total (accumulated) distance in km

              // Description data in the file
              if (newTrack == 1) pos = dataFile.position() - 2;
              dataFile.seek(pos);
              dataFile.print(", Total tracking time: <b>");
              dataFile.print(totalTimeHours);             // Total (accumulated) hours time
              dataFile.print(":");
              dataFile.print(totalTimeMinutesStr);        // Total (accumulated) minutes time
              dataFile.print(":");  
              dataFile.print(totalTimeSecondsStr);        // Total (accumulated) seconds time 
              dataFile.print("</b><br>Total distance (km): <b>");
              dataFile.print(totalDistance_km);           // Total (accumulated) distance in km
              dataFile.println("</b>");

              // Ending point of the track (waypoint)
              dataFile.seek(EndPointPos);
              dataFile.print('W');
              dataFile.print(",, ");
              dataFile.print(currLat, 6);
              dataFile.print(", ");
              dataFile.print(currLng, 6);
              dataFile.print(", ");
              dataFile.print(h);                          // Local time
              dataFile.print(":");
              dataFile.print(m);                          // Minutes
              dataFile.print(":");  
              dataFile.print(s);                          // Seconds
              dataFile.print(",,,,,,, ");
              dataFile.print("End, red");
              //
              
              dataFile.close();
              Serial.println("Done!");

              newTrack = 0;
  
              if (option != 4) {
                display.print("Saved to file!");
                display.display();
              }          
            } else {
              Serial.println("Error opening " + filePath);                  // If the file isn't open, pop up an error
               if (option != 4) {
                display.print("Error opening file!");
                display.display();
               }
            }
            start = millis();
          }
        } else {
          Serial.println("Lost GPS Signal!");                    // There is a lost fix (data hasn't changed), so print a message
          if ((millis() - start) > gpsSampleTime) { 
            if (option != 4) {
              display.print("Lost GPS Signal!");
              display.display();
            }
            start = millis();
          }
        }
      } else {
        Serial.println("Invalid data! Waiting for a valid data...");  // Invalid data is blank cordinates (00.000000)
        if ((millis() - start) > gpsSampleTime) { 
          if (option != 4) {
            display.print("Invailid data!");
            display.display();
            }
          start = millis();
        }
      }
      
      smartDelay(smartDelayTime);                                       // Run Procedure smartDelay.
    
      if (millis() > 5000 && gps.charsProcessed() < 10)
        Serial.println(F("No GPS data received: check wiring"));      
    }
    break;
    case 1:
      WifiWebServer.launchWeb();
      break;
    default: 
      // statements
    break;
  }
}
