#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <SPIFFS.h>

class OTAUpdate
{
public:
    OTAUpdate(const String &serverUrl);
    void setFirmwareVersion(int major, int minor, int patch);
    void begin();
    void checkForUpdates();
    

private:
    const char *ssid;
    const char *password;
    String serverUrl;
    String firmwareUrl;
    String spiffsUrl;
    int currentFirmwareVersion[3];
    void connectWiFi();
    void stringToFirmware(const String &Firmware, int arr[3]);
    bool checkUpgradedVersion(int arr[]);
    bool performUpdate(const char *updateUrl, int partitionType);
};

#endif
