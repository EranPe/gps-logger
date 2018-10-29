/*
  WifiWebServer.cpp - implementation of the Wifi Web Server.
*/

#include "Arduino.h"
#include "WifiWebServer.h"

ESP8266WebServer server(80);
MDNSResponder mdns; 
String webPage, directory;
File uploadFile;

int ssidMaxLength = 32;
int passMaxLength = 64;
bool searchForNetworksWebPage = true;

const char* UTC[40] = {"UTC-12:00","UTC-11:00","UTC-10:00","UTC-09:30","UTC-09:00",
  "UTC-08:00","UTC-07:00","UTC-06:00","UTC-05:00","UTC-04:00","UTC-03:30","UTC-03:00",
  "UTC-02:00","UTC-01:00","UTC","UTC+01:00","UTC+02:00","UTC+03:00","UTC+03:30",
  "UTC+04:00","UTC+04:30","UTC+05:00","UTC+05:30","UTC+05:45","UTC+06:00","UTC+06:30",
  "UTC+07:00","UTC+08:00","UTC+08:30","UTC+08:45","UTC+09:00","UTC+09:30","UTC+10:00",
  "UTC+10:30","UTC+11:00","UTC+12:00","UTC+12:45","UTC+13:00","UTC+13:45","UTC+14:00"};

void returnOK() {
  server.send(200, "text/plain", "");
}

void returnFail(String msg) {
  server.send(500, "text/plain", msg + "\r\n");
}

bool loadFromSdCard(String path){
  String dataType = "text/plain";
  if(path.endsWith("/")) path += "index.htm";

  if(path.endsWith(".src")) path = path.substring(0, path.lastIndexOf("."));
  else if(path.endsWith(".htm")) dataType = "text/html";
  else if(path.endsWith(".css")) dataType = "text/css";
  else if(path.endsWith(".js")) dataType = "application/javascript";
  else if(path.endsWith(".png")) dataType = "image/png";
  else if(path.endsWith(".gif")) dataType = "image/gif";
  else if(path.endsWith(".jpg")) dataType = "image/jpeg";
  else if(path.endsWith(".ico")) dataType = "image/x-icon";
  else if(path.endsWith(".xml")) dataType = "text/xml";
  else if(path.endsWith(".pdf")) dataType = "application/pdf";
  else if(path.endsWith(".zip")) dataType = "application/zip";

  File dataFile = SD.open(path.c_str());
  if(dataFile.isDirectory()){
    path += "/index.htm";
    dataType = "text/html";
    dataFile = SD.open(path.c_str());
  }

  if (!dataFile)
    return false;

  if (server.hasArg("download")) dataType = "application/octet-stream";

  if (server.streamFile(dataFile, dataType) != dataFile.size()) {
    Serial.println("Sent less data than expected!");
  }

  dataFile.close();
  return true;
}

void handleFileUpload(){
  if(server.uri() != "/edit") return;
  HTTPUpload& upload = server.upload();
  if(upload.status == UPLOAD_FILE_START){
    if(SD.exists((char *)upload.filename.c_str())) SD.remove((char *)upload.filename.c_str());
    uploadFile = SD.open(upload.filename.c_str(), FILE_WRITE);
    Serial.print("Upload: START, filename: "); Serial.println(upload.filename);
  } else if(upload.status == UPLOAD_FILE_WRITE){
    if(uploadFile) uploadFile.write(upload.buf, upload.currentSize);
    Serial.print("Upload: WRITE, Bytes: "); Serial.println(upload.currentSize);
  } else if(upload.status == UPLOAD_FILE_END){
    if(uploadFile) uploadFile.close();
    Serial.print("Upload: END, Size: "); Serial.println(upload.totalSize);
  }
}

void deleteRecursive(String path){
  File file = SD.open((char *)path.c_str());
  if(!file.isDirectory()){
    file.close();
    SD.remove((char *)path.c_str());
    return;
  }

  file.rewindDirectory();
  while(true) {
    File entry = file.openNextFile();
    if (!entry) break;
    String entryPath = path + "/" +entry.name();
    if(entry.isDirectory()){
      entry.close();
      deleteRecursive(entryPath);
    } else {
      entry.close();
      SD.remove((char *)entryPath.c_str());
    }
    yield();
  }

  SD.rmdir((char *)path.c_str());
  file.close();
}

void handleDelete(){
  if(server.args() == 0) return returnFail("BAD ARGS");
  String path = server.arg(0);
  if(path == "/" || !SD.exists((char *)path.c_str())) {
    returnFail("BAD PATH");
    return;
  }
  deleteRecursive(path);
  returnOK();
}

void handleCreate(){
  if(server.args() == 0) return returnFail("BAD ARGS");
  String path = server.arg(0);
  if(path == "/" || SD.exists((char *)path.c_str())) {
    returnFail("BAD PATH");
    return;
  }

  if(path.indexOf('.') > 0){
    File file = SD.open((char *)path.c_str(), FILE_WRITE);
    if(file){
      file.write((const char *)0);
      file.close();
    }
  } else {
    SD.mkdir((char *)path.c_str());
  }
  returnOK();
}

void printDirectory() {
  if(!server.hasArg("dir")) return returnFail("BAD ARGS");
  String path = server.arg("dir");
  if(path != "/" && !SD.exists((char *)path.c_str())) return returnFail("BAD PATH");
  File dir = SD.open((char *)path.c_str());
  path = String();
  if(!dir.isDirectory()){
    dir.close();
    return returnFail("NOT DIR");
  }
  dir.rewindDirectory();
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/json", "");
  WiFiClient client = server.client();

  server.sendContent("[");
  for (int cnt = 0; true; ++cnt) {
    File entry = dir.openNextFile();
    if (!entry)
    break;

    String output;
    if (cnt > 0)
      output = ',';

    output += "{\"type\":\"";
    output += (entry.isDirectory()) ? "dir" : "file";
    output += "\",\"name\":\"";
    output += entry.name();
    output += "\"";
    output += "}";
    server.sendContent(output);
    entry.close();
 }
 server.sendContent("]");
 dir.close();
}

void handleNotFound(){
  if(loadFromSdCard(server.uri())) return;
  String message = "SDCARD Not Detected\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " NAME:"+server.argName(i) + "\n VALUE:" + server.arg(i) + "\n";
  }
  
  server.send(404, "text/plain", message);
  
  Serial.print(message);
}

void printFiles(String directory, File dir, int numTabs) { 
  while (true) {
    File entry =  dir.openNextFile();
    if (! entry) {
      // no more files
      break;
    }
    for (uint8_t i = 0; i < numTabs; i++) {
      Serial.print('\t');
    }
    Serial.print(entry.name());
    if (entry.isDirectory()) {
      Serial.println("/");
      printFiles(directory, entry, numTabs + 1);
    } else {
      // files have sizes, directories do not
      Serial.print("\t\t");
      Serial.println(entry.size(), DEC);

      webPage += "<ul><li><pre>";
      webPage += "<form action=\"/files\" method=\"delete\">";
      webPage += "<a href=\"" + directory + "/" + entry.name() + "\">";
      webPage += entry.name();
      webPage += "</a>";
      webPage += " (<a href=\"" + directory + "/" + entry.name() + "\" download>";
      webPage += "download";
      webPage += "</a>)";
      webPage += "&#9;";
      webPage += String(entry.size()) + " bytes";
      webPage += "&#9;<button name=\"delete\" type=\"submit\" value=\"";   
      webPage += directory + '/' + entry.name();
      webPage += "\">Delete</button>";
      webPage += "</form>";
      webPage += "</pre>";
      webPage += "</li></ul>";
    }
    entry.close();
  }
}

void handleFiles() {
  Serial.println("Files page");
  if (server.args() > 0)
  {
    if (server.argName(0) == "delete")
    {
      String delFile = server.arg(0);
      deleteRecursive(delFile);
      Serial.println("File " + delFile + " has been deleted!");
    }
    if (server.argName(0) == "deleteAll")
    {
      deleteRecursive(directory);
      Serial.println("All files have been deleted!");
    }
  }
 
  webPage = "<!DOCTYPE HTML>\r\n<html>\r\n\r\n";
  webPage += "<h1>Files:</h1>\r\n";
  if(SD.exists((char *)directory.c_str())) {
    File root = SD.open((char *)directory.c_str());
    printFiles(directory, root, 0);
    webPage += "<form onsubmit=\"return confirm('Are you sure you want to delete all files?');\">\r\n";
    webPage += "<input type=\"submit\" name=\"deleteAll\" value=\"Delete all\">\r\n";
    webPage += "</form>\r\n";
    webPage += "<p>Go to <a href=\"http://www.gpsvisualizer.com/\">GPS Visualizer</a>: Do-It-Yourself Mapping.";
    webPage += " Upload a log file to view the route on the map.</p>";
  }
  else
  {
    webPage += "<h2><li>There is no files in \"" + directory + "\" directory.</li></h2>\r\n";
    webPage += "<h3>Start using GPS logger mode to create new files.</h3>\r\n";
  }

  webPage += "\r\n\r\n</html>";
  
  server.send(200, "text/html", webPage);
}

void handleSettings() {  
   Serial.println("Settings page");

   webPage = "<!DOCTYPE HTML>\r\n<html>\r\n\r\n";
   webPage += "<h1>Settings</h1>";
   webPage += "<form action=\"/settings\" method=\"get\">";
   webPage += "<b>Time Zone: </b>";
   
   webPage += "<select name=\"TimeZoneOptions\">";  
   for (int i =0; i < 40; i++)
   {
    webPage += "<option value=\"";
    webPage += String(UTC[i]);
    webPage += "\">";
    webPage += String(UTC[i]);
    webPage += "</option>";
   }
   webPage += "</select>";
   
   /*webPage += "<input type=\"text\" name=\"TimeZone\" value=\"+2\">";*/
   webPage += " (Check: <a href=\"https://www.timeanddate.com/worldclock/\"> World Clock </a>)<br><br>";
   webPage += "<b> DST (Daylight saving time)? </b>";
   webPage += "<input type=\"radio\" name=\"DST\" value=\"Yes\" checked> Yes";
   webPage += "<input type=\"radio\" name=\"DST\" value=\"No\"> No<br><br>";
   webPage += "<b>GPS sample time: </b>";
   webPage += "Minutes: <input type=\"number\" name=\"minutes\" min=\"0\" max=\"60\" value=\"0\">";
   webPage += " Seconds: <input type=\"number\" name=\"seconds\" min=\"0\" max=\"59\" value=\"3\"><br><br>";
   /*webPage += "<input type=\"time\" name=\"usr_time\">";
   webPage += "<input type=\"text\" name=\"gpsSampleTime\" value=\"0.5\"> seconds<br><br>";
   webPage += "<input type=\"checkbox\" name=\"enSound\" value=\"on\" checked><b> Enable sound</b><br><br>";*/
   webPage += "<input type=\"submit\" value=\"Submit\">";
   webPage += "</form>";
   webPage += "\r\n\r\n</html>";
    
    if (server.args() >= 4) 
    {
      Serial.print("Clearing EEPROM...");
      for (int i = 100; i < 115; ++i) EEPROM.write(i, 0);
      Serial.println("Done!");

      String TimeZone = server.arg(0);
      Serial.println();
      Serial.println("Time Zone: " + TimeZone);
      if (TimeZone == "UTC") TimeZone = "UTC+00:00";
      for (int i = 100; i < 106; ++i)
        EEPROM.write(i, TimeZone[(i - 100) + 3]);
      
      Serial.println("DST: " + server.arg(1));
      (server.arg(1) == "Yes") ? EEPROM.write(106, '1') : EEPROM.write(106, '0');
      
      Serial.println("GPS Sample Time (Minutes): " + server.arg(2));
      if (server.arg(2).length() == 2) {
        EEPROM.write(107, server.arg(2)[0]);
        EEPROM.write(108, server.arg(2)[1]);
      }
      else {
        EEPROM.write(107, '0');
        EEPROM.write(108, server.arg(2)[0]);
      }
      EEPROM.write(109, ':');
      
      Serial.println("GPS Sample Time (Seconds): " + server.arg(3));
      if (server.arg(3).length() == 2) {
        EEPROM.write(110, server.arg(3)[0]);
        EEPROM.write(111, server.arg(3)[1]);
      }
      else {
        EEPROM.write(110, '0');
        if (server.arg(3)[0] == '0') {                                  // Minimum time is 1 second
          EEPROM.write(111, '1');
          webPage += "<br>Minimum GPS Sample Time is 1 second - Saved as 1 second.";
        }
          else EEPROM.write(111, server.arg(3)[0]);
      }
      
      /*(server.args() == 5) ? EEPROM.write(112, '1') : EEPROM.write(112, '0');
      (server.args() == 5) ? Serial.println("Sound: On") : Serial.println("Sound: Off");*/
      EEPROM.commit();
      Serial.println("New settings saved!");
      
      webPage += "<br>Saved!";
    }
   
   server.send(200, "text/html", webPage);
}

void handleClear() {
  Serial.print("clearing EEPROM...");
  for (int i = 0; i < 96; ++i) EEPROM.write(i, 0);
  EEPROM.commit();
  Serial.println("Done!");
  server.send(200, "text/html", "<h1>EEPROM Memory is now cleared</h1><h2>Reset to make the change</h2>");
}

void handleNetwork() {
  
  Serial.println("Clearing EEPROM");
  for (int i = 0; i < 96; ++i) EEPROM.write(i, 0);
  
  String ssid = server.arg(0);

  Serial.println("Writing EEPROM ssid: ");
  Serial.print("Wrote: ");
  for (int i = 0; i < ssid.length(); ++i)
  {
    EEPROM.write(i, ssid[i]);
    Serial.print(ssid[i]); 
  }
  Serial.println("\nDone");

  String pass = server.arg(1);
      
  Serial.println("writing EEPROM password: ");
  Serial.print("Wrote: ");
  for (int i = 0; i < pass.length(); ++i)
  {
    EEPROM.write(32+i, pass[i]);
    Serial.print(pass[i]); 
  }
  Serial.println("\nDone");  
  EEPROM.commit();

  String message = "<h1>Saved to EEPROM... reset to boot into the network</h1>";

  server.send(200, "text/html", message);
}

String ScannedNetworks() {
  String networks;
  Serial.print("Scanning networks...");
  int n = WiFi.scanNetworks();
  Serial.println("done!");
  if (n == 0)
    Serial.println("no networks found");
  else
  {
    Serial.print(n);
    Serial.println(" networks found");
    for (int i = 0; i < n; ++i)
     {
      // Print SSID and RSSI for each network found
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(WiFi.SSID(i));
      Serial.print(" (");
      Serial.print(WiFi.RSSI(i));
      Serial.print(")");
      Serial.println((WiFi.encryptionType(i) == ENC_TYPE_NONE)?" ":"*");
      delay(10);
     }
  }
  Serial.println(""); 
  networks = "<ul>";
  for (int i = 0; i < n; ++i)
    {
      // Print SSID and RSSI for each network found
      networks += "<li>";
      networks +=i + 1;
      networks += ": ";
      networks += WiFi.SSID(i);
      networks += " (";
      networks += WiFi.RSSI(i);
      networks += ")";
      networks += (WiFi.encryptionType(i) == ENC_TYPE_NONE)?" ":"*";
      networks += "</li>";
    }
  networks += "</ul>";
  return networks;
}

void handleRoot() {
  Serial.println("Home page");
  webPage = "<!DOCTYPE HTML>\r\n<html>\r\n\r\n";
  
  if (searchForNetworksWebPage) {
    IPAddress ip = WiFi.softAPIP();
    String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
    
    webPage += "<h1>You are connected via AP at ";
    webPage += ipStr;
    webPage += "</h1>\r\n";
    webPage += "<h2>Connect to a network to access the internet:</h2>\r\n";
    webPage += ScannedNetworks();
    webPage += "<form method='get' action='network'><label>SSID: </label><input name='ssid' length=32><label> PASSWORD: </label><input name='pass' length=64>&#9;<input type='submit'></form>\r\n";
    webPage += "<ul>Or go to <a href=\"/files\">files page</a></ul>\r\n";
  }
  else {
    webPage += "<h1>You are connected</h1>\r\n";
    webPage += "<h2>Go to <a href=\"/files\">files page</a></h2>\r\n";
    webPage += "<h3><a href=\"/cleareeprom\">Disconnect from the network (clear EEPROM memory)</a></h3>\r\n";
  }

  webPage += "Go to <a href=\"/settings\">settings</a> page";
  webPage += "\r\n\r\n</html>";
  
  server.send(200, "text/html", webPage);
}

void setupAP(String host) {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  
  Serial.println();
  Serial.print("Configuring access point...");
  WiFi.softAP((char *)host.c_str());
  Serial.println("OK\n");
  Serial.println("Wifi direct network: " + host);

  IPAddress myIP = WiFi.softAPIP();
  Serial.print("You can now connect to AP IP address: ");
  Serial.println(myIP);
}

WifiWebServer::WifiWebServer(String hostName, String directoryName) {     
  host = hostName;
  dir = directoryName;
}

String WifiWebServer::start() {
  int i = 0;
  char c;
  directory = dir;
  String wifiStatus;
  
  Serial.println("Web server mode");
  Serial.println();

  Serial.println("Starting wifi connection...");

  Serial.println("Reading EEPROM ssid...");
  String ssid;
  for (int i = 0; i < 32; ++i) {
    ssid += char(EEPROM.read(i));
  }
  Serial.print("SSID: ");
  Serial.println(ssid);
  
  Serial.println("Reading EEPROM password...");
  String password;
  for (int i = 32; i < 96; ++i) {
    password += char(EEPROM.read(i));
  }
  Serial.print("PASS: ");
  Serial.println("********");

  if (ssid.length() > 1) {
    WiFi.disconnect();
    if (password.length() > 1)                                            // There's a passwrod in the EEPROM memory
      WiFi.begin((char *)ssid.c_str(), (char *)password.c_str());         // Connect to a password protected network
    else
      WiFi.begin((char *)ssid.c_str());                                   // else, Connect to a free network (without password)
      
      Serial.println("Waiting for the Wifi to connect to: " + ssid);
      delay(500);
      while (i < 20) { 
        if (WiFi.status() == WL_CONNECTED) 
        {
          Serial.println(" Connected!");

          IPAddress myIP = WiFi.localIP();
          String ipStr = String(myIP[0]) + '.' + String(myIP[1]) + '.' + String(myIP[2]) + '.' + String(myIP[3]);
          Serial.println("\nIP address: " + ipStr);
          
          if (MDNS.begin((char *)host.c_str(), myIP)) {
            MDNS.addService("http", "tcp", 80);
            Serial.println("MDNS responder started");
            Serial.print("You can now connect to http://");
            Serial.print(host);
            Serial.println(".local");
            String networkName;
            for (int i = 0; i < 32; ++i) {
              if (ssid[i] == NULL) break;
              networkName += ssid[i];
            }
            wifiStatus = "Device connected to:\n" + networkName + " (" + ipStr + ")\n" + char(16) + " Enter in browser:\n" + host + ".local";
          }

          searchForNetworksWebPage = false;
          i = 20;
        } else {
          delay(500);
          if (i % 2) Serial.print('.'); // Serial.print(10 - (i / 2));
          i++;      
        }
      }
      if (WiFi.status() != WL_CONNECTED) {
      Serial.println("\nConnection timed out, opening AP setup");
      setupAP(host);
      wifiStatus = "1.Connect to wifi:\n" + host + "\n2.Enter in browser:\n192.168.4.1";
      }
  } else {
    Serial.println("\nNo SSID information in memory, opening AP setup");
    setupAP(host);
    wifiStatus = "1.Connect to wifi:\n" + host + "\n2.Enter in browser:\n192.168.4.1";
  }
  
  server.on("/", handleRoot);
  server.on("/network", handleNetwork);
  server.on("/cleareeprom", handleClear);
  server.on("/files", handleFiles);
  server.on("/settings", handleSettings);
  
  server.on("/list", HTTP_GET, printDirectory);
  server.on("/edit", HTTP_DELETE, handleDelete);
  server.on("/edit", HTTP_PUT, handleCreate);
  server.on("/edit", HTTP_POST, [](){ returnOK(); }, handleFileUpload);
  
  server.onNotFound(handleNotFound);
  
  server.begin();
  Serial.println("HTTP server started\n");
  return wifiStatus;
}

void WifiWebServer::launchWeb()
{
  server.handleClient();
}
