#include "OTAUpdate.h"

OTAUpdate::OTAUpdate(const String &serverUrl)
    : serverUrl(serverUrl)
{
    firmwareUrl = serverUrl + "/firmware.bin";
    spiffsUrl = serverUrl + "/spiffs.bin";
}
void OTAUpdate::setFirmwareVersion(int major, int minor, int patch)
{
    currentFirmwareVersion[0] = major;
    currentFirmwareVersion[1] = minor;
    currentFirmwareVersion[2] = patch;
}

void OTAUpdate::begin()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("❌ WiFi not connected. OTA update requires an active WiFi connection.");
        return;
    }

    if (!SPIFFS.begin(true))
    {
        Serial.println("❌ SPIFFS Mount Failed");
    }

    checkForUpdates();
}
// void OTAUpdate::connectWiFi()
// {
//     WiFi.begin(ssid, password);
//     Serial.print("🔌 Connecting to WiFi");
//     while (WiFi.status() != WL_CONNECTED)
//     {
//         Serial.print(".");
//         delay(1000);
//     }
//     Serial.println("\n✅ WiFi connected.");
// }

void OTAUpdate::stringToFirmware(const String &Firmware, int arr[3])
{
    int firstDot = Firmware.indexOf('.');
    int secondDot = Firmware.lastIndexOf('.');

    if (firstDot == -1 || secondDot == -1 || firstDot == secondDot)
    {
        Serial.println("❌ Invalid firmware version format received.");
        return;
    }

    arr[0] = Firmware.substring(0, firstDot).toInt();
    arr[1] = Firmware.substring(firstDot + 1, secondDot).toInt();
    arr[2] = Firmware.substring(secondDot + 1).toInt();
}

bool OTAUpdate::checkUpgradedVersion(int arr[])
{
    return (arr[0] > currentFirmwareVersion[0]) ||
           (arr[0] == currentFirmwareVersion[0] && arr[1] > currentFirmwareVersion[1]) ||
           (arr[0] == currentFirmwareVersion[0] && arr[1] == currentFirmwareVersion[1] && arr[2] > currentFirmwareVersion[2]);
}

bool OTAUpdate::performUpdate(const char *updateUrl, int partitionType)
{
    HTTPClient http;
    http.setTimeout(5000);
    http.begin(updateUrl);

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK)
    {
        Serial.printf("❌ Failed to fetch update. HTTP Code: %d\n", httpCode);
        http.end();
        return false;
    }

    int contentLength = http.getSize();
    WiFiClient *stream = http.getStreamPtr();

    if (contentLength <= 0)
    {
        Serial.println("❌ Invalid update file.");
        http.end();
        return false;
    }

    if (!Update.begin(contentLength, partitionType))
    {
        Serial.println("❌ Not enough space for update.");
        http.end();
        return false;
    }

    Serial.println("⬇️ Downloading update...");
    int written = 0;
    uint8_t buffer[128];

    while (written < contentLength)
    {
        int bytesRead = stream->readBytes(buffer, sizeof(buffer));
        if (bytesRead > 0)
        {
            Update.write(buffer, bytesRead);
            written += bytesRead;

            int progress = (written * 100) / contentLength;
            Serial.printf("📊 Progress: %d%%\n", progress);
        }
    }

    Serial.println("✅ Download complete. Finalizing update...");

    if (!Update.end() || Update.hasError())
    {
        Serial.printf("❌ Update error: %s\n", Update.errorString());
        http.end();
        return false;
    }

    Serial.println("✅ Update successful!");
    http.end();
    return true;
}

void OTAUpdate::checkForUpdates()
{
    bool ESPUPGRADED = false;
    Serial.println("🔍 Checking for firmware update...");

    HTTPClient http;
    http.setTimeout(5000);
    http.begin(serverUrl + "/fv");

    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK)
    {
        String data = http.getString();
        int arr[3];
        stringToFirmware(data, arr);

        if (checkUpgradedVersion(arr))
        {
            Serial.println("🔍 Checking for SPIFFS update first...");
            if (performUpdate(spiffsUrl.c_str(), U_SPIFFS))
            {
                Serial.println("✅ SPIFFS updated successfully.");
                ESPUPGRADED = true;
            }
            else
            {
                Serial.println("⚠️ No SPIFFS update available.");
            }

            Serial.println("🔍 Checking for Firmware update...");
            if (performUpdate(firmwareUrl.c_str(), U_FLASH))
            {
                Serial.println("✅ Firmware updated successfully.");
                ESPUPGRADED = true;
            }
            else
            {
                Serial.println("⚠️ No firmware update available.");
            }

            if (ESPUPGRADED)
            {
                Serial.println("🔄 Rebooting ESP32 to apply updates...");
                delay(1000);
                ESP.restart();
            }
            else
            {
                Serial.println("✅ Everything is already up-to-date.");
            }
        }
    }
    else
    {
        Serial.println("❌ Failed to fetch version info.");
    }
    http.end();
}
