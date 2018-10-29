/*
  WifiWebServer.h - Library for the Wifi Web Server.
*/
#ifndef WifiWebServer_h
#define WifiWebServer_h

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <EEPROM.h>
#include <SD.h>

#include "Arduino.h"

class WifiWebServer
{
  public:
    WifiWebServer(String hostName, String directoryName);
    String start();
    void launchWeb();
  private: 
    String host;
    String dir;
};

#endif
