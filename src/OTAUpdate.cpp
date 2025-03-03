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
    int lastProgress = -1; // Store last progress to prevent duplicate prints

    while (written < contentLength)
    {
        int bytesRead = stream->readBytes(buffer, sizeof(buffer));
        if (bytesRead > 0)
        {
            Update.write(buffer, bytesRead);
            written += bytesRead;

            int progress = (written * 100) / contentLength;

            if (progress > lastProgress) // Print only if progress changed
            {
                Serial.printf("📊 Progress: %d%%\n", progress);
                lastProgress = progress;
            }
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
bool OTAUpdate::performUpdateFromFile(Stream &updateStream, size_t contentLength, int partitionType)
{
    if (contentLength <= 0)
    {
        Serial.println("❌ Invalid update file size.");
        return false;
    }

    if (!Update.begin(contentLength, partitionType))
    {
        Serial.println("❌ Not enough space for update.");
        return false;
    }

    Serial.println("⬇️ Applying update from stream...");
    size_t written = 0;
    uint8_t buffer[128];
    int lastProgress = -1;

    while (written < contentLength)
    {
        size_t bytesRead = updateStream.readBytes(buffer, sizeof(buffer));
        if (bytesRead > 0)
        {
            Update.write(buffer, bytesRead);
            written += bytesRead;

            int progress = (written * 100) / contentLength;
            if (progress > lastProgress)
            {
                Serial.printf("📊 Progress: %d%%\n", progress);
                lastProgress = progress;
            }
        }
    }

    Serial.println("✅ File update complete. Finalizing...");

    if (!Update.end() || Update.hasError())
    {
        Serial.printf("❌ Update error: %s\n", Update.errorString());
        return false;
    }

    Serial.println("✅ Update successful!");
    return true;
}

void OTAUpdate::checkForUpdates()
{
    bool ESPUPGRADED = false;
    Serial.println("🔍 Checking for firmware update...");

    HTTPClient http;
    http.setTimeout(5000);
    http.begin(serverUrl + "/config.json");

    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK)
    {
        String input = http.getString();
        JsonDocument doc;

        DeserializationError error = deserializeJson(doc, input);

        if (error)
        {
            Serial.print("deserializeJson() failed: ");
            Serial.println(error.c_str());
            return;
        }

        const char *firmware_version = doc["firmware_version"];
        int arr[3];
        stringToFirmware(firmware_version, arr);
        Serial.printf("Found version: %s\n",firmware_version);

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

// Handles POST request: Begins the update process
void OTAUpdate::handleUpdatePost(WebServer &server) {
    if (Update.hasError()) {
        server.send(500, "text/plain", "❌ Update Failed.");
    } else {
        server.send(200, "text/plain", "✅ Update Successful! Rebooting...");
        delay(1500);
        ESP.restart();
    }
}

// Handles file upload during OTA update
// void OTAUpdate::handleUpdatePost(WebServer &server) {
//     HTTPUpload &upload = server.upload();
//     static File updateFile;
//     static int partitionType;

//     if (upload.status == UPLOAD_FILE_START) {
//         // Validate update type
//         String typeArg = server.arg("type");
//         if (typeArg != "0" && typeArg != "1") {
//             server.send(400, "text/plain", "❌ Invalid update type.");
//             return;
//         }

//         partitionType = (typeArg.toInt() == 1) ? U_SPIFFS : U_FLASH;

//         // Open file for writing
//         updateFile = SPIFFS.open("/update.bin", "w");
//         if (!updateFile) {
//             server.send(500, "text/plain", "❌ Failed to open file for writing.");
//             return;
//         }
//         Serial.println("⬇️ Receiving update file...");
//     } 
//     else if (upload.status == UPLOAD_FILE_WRITE) {
//         if (updateFile) {
//             updateFile.write(upload.buf, upload.currentSize);
//         }
//     } 
//     else if (upload.status == UPLOAD_FILE_END) {
//         if (!updateFile) {
//             server.send(500, "text/plain", "❌ File write failed.");
//             return;
//         }

//         updateFile.close();
//         updateFile = SPIFFS.open("/update.bin", "r");
        
//         if (!updateFile || updateFile.size() <= 0) {
//             server.send(500, "text/plain", "❌ Invalid update file.");
//             return;
//         }

//         // Perform the update
//         if (performUpdateFromFile(updateFile, updateFile.size(), partitionType)) {
//             Serial.println("✅ Update successful.");
//         } else {
//             server.send(500, "text/plain", "❌ Update Failed.");
//         }
//     }
// }
void OTAUpdate::setupManualOTA(WebServer &server) {
    server.on("/update", HTTP_GET, std::bind(&OTAUpdate::handleUpdateGet, this, std::ref(server)));
    server.on("/update", HTTP_POST, std::bind(&OTAUpdate::handleUpdatePost, this, std::ref(server)), 
              std::bind(&OTAUpdate::handleUpdateUpload, this, std::ref(server)));
}

// Handles GET request: Serves the OTA update webpage
void OTAUpdate::handleUpdateGet(WebServer &server) {
    server.send(200, "text/html",
        "<html><body>"
        "<h2>ESP32 OTA Update</h2>"
        "<form method='POST' action='/update' enctype='multipart/form-data'>"
        "    <input type='file' name='update'>"
        "    <select name='type'>"
        "        <option value='0'>Firmware</option>"
        "        <option value='1'>SPIFFS</option>"
        "    </select>"
        "    <input type='submit' value='Update'>"
        "</form>"
        "</body></html>");
}

// Handles POST request: Begins the update process
// void OTAUpdate::handleUpdatePost(WebServer &server) {
//     if (Update.hasError()) {
//         server.send(500, "text/plain", "❌ Update Failed.");
//     } else {
//         server.send(200, "text/plain", "✅ Update Successful! Rebooting...");
//         delay(1500);
//         ESP.restart();
//     }
// }

// Handles file upload during OTA update
void OTAUpdate::handleUpdateUpload(WebServer &server) {
    HTTPUpload &upload = server.upload();
    static File updateFile;
    static int partitionType;

    if (upload.status == UPLOAD_FILE_START) {
        // Validate update type
        String typeArg = server.arg("type");
        if (typeArg != "0" && typeArg != "1") {
            server.send(400, "text/plain", "❌ Invalid update type.");
            return;
        }

        partitionType = (typeArg.toInt() == 1) ? U_SPIFFS : U_FLASH;

        // Open file for writing
        updateFile = SPIFFS.open("/update.bin", "w");
        if (!updateFile) {
            server.send(500, "text/plain", "❌ Failed to open file for writing.");
            return;
        }
        Serial.println("⬇️ Receiving update file...");
    } 
    else if (upload.status == UPLOAD_FILE_WRITE) {
        if (updateFile) {
            updateFile.write(upload.buf, upload.currentSize);
        }
    } 
    else if (upload.status == UPLOAD_FILE_END) {
        if (!updateFile) {
            server.send(500, "text/plain", "❌ File write failed.");
            return;
        }

        updateFile.close();
        updateFile = SPIFFS.open("/update.bin", "r");
        
        if (!updateFile || updateFile.size() <= 0) {
            server.send(500, "text/plain", "❌ Invalid update file.");
            return;
        }

        // Perform the update
        if (performUpdateFromFile(updateFile, updateFile.size(), partitionType)) {
            Serial.println("✅ Update successful.");
        } else {
            server.send(500, "text/plain", "❌ Update Failed.");
        }
    }
}