#include "OTAUpdate.h"
OTAUpdate::OTAUpdate(const String &serverUrl)
    : serverUrl(serverUrl)
{
    firmwareUrl = serverUrl + "/firmware.bin";
    spiffsUrl = serverUrl + "/spiffs.bin";
}
void OTAUpdate::updateurl(const String &serve){
    serverUrl=serve;
    firmwareUrl = serverUrl + "/firmware.bin";
    spiffsUrl = serverUrl + "/spiffs.bin";
}

void OTAUpdate::updateDisplayProgress(String heading, int progress)
{
    display.clearDisplay();

    // Set text size and color
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    // Calculate center position for heading
    int16_t x, y;
    uint16_t w, h;
    display.getTextBounds(heading, 0, 0, &x, &y, &w, &h);
    int centerX = (display.width() - w) / 2;

    // Draw centered heading
    display.setCursor(centerX, 10);
    display.print(heading);

    // Draw progress bar
    int barX = 10;
    int barY = 30;
    int barWidth = 100;
    int barHeight = 10;

    display.drawRect(barX, barY, barWidth, barHeight, SSD1306_WHITE);
    display.fillRect(barX, barY, (progress * barWidth) / 100, barHeight, SSD1306_WHITE);

    // Draw progress percentage below bar
    String progressText = String(progress) + "%";
    display.getTextBounds(progressText, 0, 0, &x, &y, &w, &h);
    int progressTextX = (display.width() - w) / 2;

    display.setCursor(progressTextX, barY + barHeight + 5);
    display.print(progressText);

    // Display changes
    display.display();
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
    display.begin(SSD1306_SWITCHCAPVCC, 0x3c, -1);
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
    String heading;
    if (partitionType == U_FLASH)
    {
        heading = "Firmware OTA";
    }
    if (partitionType == U_SPIFFS)
    {
        heading = "SPIFFS OTA";
    }
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
                updateDisplayProgress(heading, progress);
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
    String heading;
    if (partitionType == U_FLASH)
    {
        heading = "Firmware OTA";
    }
    if (partitionType == U_SPIFFS)
    {
        heading = "SPIFFS OTA";
    }
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
                updateDisplayProgress(heading, progress);
                display.display(); // Ensure update is pushed to OLED
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
// bool OTAUpdate::updateavailabe(){
//     HTTPClient http
//     http.setTimeout(5000);
//     http.begin(serverUrl + "/config.json");

//     int httpCode = http.GET();
//     if (httpCode == HTTP_CODE_OK)
//     {
//         String input = http.getString();
//         JsonDocument doc;

//         DeserializationError error = deserializeJson(doc, input);

//         if (error)
//         {
//             Serial.print("deserializeJson() failed: ");
//             Serial.println(error.c_str());

//             display.clearDisplay();
//             display.setCursor(10, 10);
//             display.print("Update Error");
//             display.setCursor(10, 20);
//             display.print("JSON Failed");
//             display.display();

//             return false;
//         }

//         const char *firmware_version = doc["firmware_version"];
//         int arr[3];
//         stringToFirmware(firmware_version, arr);
//         http.end();
//         return checkUpgradedVersion(arr);
        

// }
// }

// void OTAUpdate::checkForUpdates()
// {
//     bool ESPUPGRADED = false;
//     Serial.println("🔍 Checking for firmware update...");

//     HTTPClient http;
//     http.setTimeout(5000);
//     http.begin(serverUrl + "/config.json");

//     int httpCode = http.GET();
//     if (httpCode == HTTP_CODE_OK)
//     {
//         String input = http.getString();
//         JsonDocument doc;

//         DeserializationError error = deserializeJson(doc, input);

//         if (error)
//         {
//             Serial.print("deserializeJson() failed: ");
//             Serial.println(error.c_str());
//             return;
//         }

//         const char *firmware_version = doc["firmware_version"];
//         int arr[3];
//         stringToFirmware(firmware_version, arr);
//         Serial.printf("Found version: %s\n",firmware_version);

//         if (checkUpgradedVersion(arr))
//         {
//             Serial.println("🔍 Checking for SPIFFS update first...");
//             if (performUpdate(spiffsUrl.c_str(), U_SPIFFS))
//             {
//                 Serial.println("✅ SPIFFS updated successfully.");
//                 ESPUPGRADED = true;
//             }
//             else
//             {
//                 Serial.println("⚠️ No SPIFFS update available.");
//             }

//             Serial.println("🔍 Checking for Firmware update...");
//             if (performUpdate(firmwareUrl.c_str(), U_FLASH))
//             {
//                 Serial.println("✅ Firmware updated successfully.");
//                 ESPUPGRADED = true;
//             }
//             else
//             {
//                 Serial.println("⚠️ No firmware update available.");
//             }

//             if (ESPUPGRADED)
//             {
//                 Serial.println("🔄 Rebooting ESP32 to apply updates...");
//                 delay(1000);
//                 ESP.restart();
//             }
//             else
//             {
//                 Serial.println("✅ Everything is already up-to-date.");
//             }
//         }
//     }
//     else
//     {
//         Serial.println("❌ Failed to fetch version info.");
//     }
//     http.end();
// }
void OTAUpdate::checkForUpdates()
{
    bool ESPUPGRADED = false;
    Serial.println("🔍 Checking for firmware update...");

    // display.clearDisplay();
    // display.setTextSize(1);
    // display.setTextColor(SSD1306_WHITE);
    // display.setCursor(10, 10);
    // display.print("Checking for");
    // display.setCursor(10, 20);
    // display.print("Updates...");
    // display.display();

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

            // display.clearDisplay();
            // display.setCursor(10, 10);
            // display.print("Update Error");
            // display.setCursor(10, 20);
            // display.print("JSON Failed");
            // display.display();

            return;
        }

        const char *firmware_version = doc["firmware_version"];
        int arr[3];
        stringToFirmware(firmware_version, arr);
        Serial.printf("Found version: %s\n", firmware_version);

        // display.clearDisplay();
        // display.setCursor(10, 10);
        // display.print("Found Version:");
        // display.setCursor(10, 20);
        // display.print(firmware_version);
        // display.display();
        // delay(1000);

        if (checkUpgradedVersion(arr))
        {
            Serial.println("🔍 Checking for SPIFFS update first...");

            // display.clearDisplay();
            // display.setCursor(10, 10);
            // display.print("Checking");
            // display.setCursor(10, 20);
            // display.print("SPIFFS Update...");
            // display.display();

            if (performUpdate(spiffsUrl.c_str(), U_SPIFFS))
            {
                Serial.println("✅ SPIFFS updated successfully.");
                ESPUPGRADED = true;

                display.clearDisplay();
                display.setCursor(10, 10);
                display.print("SPIFFS Updated");
                display.display();
                delay(1000);
            }
            else
            {
                Serial.println("⚠️ No SPIFFS update available.");

                display.clearDisplay();
                display.setCursor(10, 10);
                display.print("No SPIFFS");
                display.setCursor(10, 20);
                display.print("Update Found");
                display.display();
                delay(1000);
            }

            Serial.println("🔍 Checking for Firmware update...");

            display.clearDisplay();
            display.setCursor(10, 10);
            display.print("Checking");
            display.setCursor(10, 20);
            display.print("Firmware Update...");
            display.display();

            if (performUpdate(firmwareUrl.c_str(), U_FLASH))
            {
                Serial.println("✅ Firmware updated successfully.");
                ESPUPGRADED = true;

                display.clearDisplay();
                display.setCursor(10, 10);
                display.print("Firmware Updated");
                display.display();
                delay(1000);
            }
            else
            {
                Serial.println("⚠️ No firmware update available.");

                display.clearDisplay();
                display.setCursor(10, 10);
                display.print("No Firmware");
                display.setCursor(10, 20);
                display.print("Update Found");
                display.display();
                delay(1000);
            }

            if (ESPUPGRADED)
            {
                Serial.println("🔄 Rebooting ESP32 to apply updates...");

                display.clearDisplay();
                display.setCursor(10, 10);
                display.print("Rebooting...");
                display.display();
                delay(1000);

                ESP.restart();
            }
            else
            {
                Serial.println("✅ Everything is already up-to-date.");

                display.clearDisplay();
                display.setCursor(10, 10);
                display.print("Already");
                display.setCursor(10, 20);
                display.print("Up-to-date");
                display.display();
                delay(2000);
            }
        }
    }
    else
    {
        Serial.println("❌ Failed to fetch version info.");

        display.clearDisplay();
        display.setCursor(10, 10);
        display.print("Update Error");
        display.setCursor(10, 20);
        display.print("Network Failed");
        display.display();
        delay(2000);
    }
    http.end();
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
void OTAUpdate::setupManualOTA(WebServer &server)
{
    server.on("/update", HTTP_GET, [this, &server]()
              { handleUpdateGet(server); });
    server.on("/update", HTTP_POST, [this, &server]()
              { handleUpdatePost(server); }, [this, &server]()
              { handleUpdateUpload(server); });
}

void OTAUpdate::handleUpdateGet(WebServer &server)
{
    String updateForm = R"rawliteral(
        <!DOCTYPE html>
        <html lang="en">
        <head>
            <meta charset="UTF-8">
            <meta name="viewport" content="width=device-width, initial-scale=1">
            <title>ESP32 OTA Update</title>
            <link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/css/bootstrap.min.css">
            <script>
                function startUpload(type) {
                    document.getElementById("status").innerHTML = "Uploading " + type + "...";
                    document.getElementById("progress").style.width = "0%";
                    document.getElementById("progress").innerHTML = "0%";
                }

                function updateProgress(event) {
                    if (event.lengthComputable) {
                        let percent = Math.round((event.loaded / event.total) * 100);
                        document.getElementById("progress").style.width = percent + "%";
                        document.getElementById("progress").innerHTML = percent + "%";
                    }
                }

                function uploadFile(type) {
                    let formData = new FormData();
                    let fileInput = document.getElementById(type);
                    if (!fileInput.files.length) {
                        alert("Please select a file for " + type + " update.");
                        return;
                    }
                    formData.append("update", fileInput.files[0]);

                    let xhr = new XMLHttpRequest();
                    xhr.open("POST", "/update", true);
                    xhr.upload.addEventListener("progress", updateProgress);
                    xhr.onload = function () {
                        if (xhr.status === 200) {
                            document.getElementById("status").innerHTML = "Update successful! Rebooting...";
                            setTimeout(() => location.reload(), 2000);
                        } else {
                            document.getElementById("status").innerHTML = "Update failed!";
                        }
                    };
                    startUpload(type);
                    xhr.send(formData);
                }
            </script>
        </head>
        <body class="container mt-5">
            <h2 class="text-center">ESP32 OTA Update</h2>
            <div class="card p-4 shadow">
                <h5>Firmware Update</h5>
                <input type="file" id="firmware" class="form-control mb-2">
                <button class="btn btn-primary w-100" onclick="uploadFile('firmware')">Upload Firmware</button>

                <hr>
                <h5>SPIFFS Update</h5>
                <input type="file" id="spiffs" class="form-control mb-2">
                <button class="btn btn-success w-100" onclick="uploadFile('spiffs')">Upload SPIFFS</button>

                <div class="progress mt-3">
                    <div id="progress" class="progress-bar" role="progressbar" style="width: 0%;">0%</div>
                </div>
                <p id="status" class="mt-2 text-center text-info"></p>
            </div>
        </body>
        </html>
    )rawliteral";
    server.send(200, "text/html", updateForm);
}

void OTAUpdate::handleUpdatePost(WebServer &server)
{
    if (!Update.hasError())
    {
        server.send(200, "text/plain", "Update Successful! Rebooting...");
        delay(1000);
        ESP.restart();
    }
    else
    {
        server.send(500, "text/plain", "Update Failed!");
    }
}

void OTAUpdate::handleUpdateUpload(WebServer &server)
{
    HTTPUpload &upload = server.upload();
    int partitionType = (server.arg("update") == "spiffs") ? U_SPIFFS : U_FLASH;

    if (upload.status == UPLOAD_FILE_START)
    {
        Serial.printf("Update: %s\n", upload.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, partitionType))
        {
            Update.printError(Serial);
        }
    }
    else if (upload.status == UPLOAD_FILE_WRITE)
    {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
        {
            Update.printError(Serial);
        }
    }
    else if (upload.status == UPLOAD_FILE_END)
    {
        if (Update.end(true))
        {
            Serial.println("Update Successful");
        }
        else
        {
            Update.printError(Serial);
        }
    }
}

bool OTAUpdate::performUpdateFromFile(File &updateFile, size_t contentLength, int partitionType)
{
    if (!updateFile || contentLength <= 0)
    {
        Serial.println("❌ Invalid update file.");
        return false;
    }

    if (!Update.begin(contentLength, partitionType))
    {
        Serial.println("❌ Not enough space for update.");
        return false;
    }

    Serial.println("⬇️ Applying update from file...");
    size_t written = 0;
    uint8_t buffer[128];
    int lastProgress = -1;
    String heading = (partitionType == U_FLASH) ? "Firmware OTA" : "SPIFFS OTA";

    while (updateFile.available())
    {
        size_t bytesRead = updateFile.read(buffer, sizeof(buffer));
        if (bytesRead > 0)
        {
            Update.write(buffer, bytesRead);
            written += bytesRead;

            int progress = (written * 100) / contentLength;
            if (progress > lastProgress)
            {
                Serial.printf("📊 Progress: %d%%\n", progress);
                lastProgress = progress;
                updateDisplayProgress(heading, progress);
                display.display();
            }
        }
    }

    updateFile.close();
    Serial.println("✅ File update complete. Finalizing...");

    if (!Update.end() || Update.hasError())
    {
        Serial.printf("❌ Update error: %s\n", Update.errorString());
        return false;
    }

    Serial.println("✅ Update successful!");
    return true;
}

// void OTAUpdate::handleUpdateUpload(WebServer &server)
// {
//     HTTPUpload &upload = server.upload();
//     int partitionType = (server.arg("update") == "spiffs") ? U_SPIFFS : U_FLASH;

//     static File updateFile;
//     static size_t contentLength = 0;

//     if (upload.status == UPLOAD_FILE_START)
//     {
//         Serial.printf("📂 Starting update: %s\n", upload.filename.c_str());
//         contentLength = upload.totalSize; // Store the total file size

//         // Open a temporary file in SPIFFS for writing
//         updateFile = SPIFFS.open("/update.bin", "w");
//         if (!updateFile)
//         {
//             Serial.println("❌ Failed to create update file!");
//             return;
//         }
//     }
//     else if (upload.status == UPLOAD_FILE_WRITE)
//     {
//         if (updateFile)
//         {
//             updateFile.write(upload.buf, upload.currentSize);
//         }
//     }
//     else if (upload.status == UPLOAD_FILE_END)
//     {
//         if (updateFile)
//         {
//             updateFile.close();
//             Serial.println("✅ Upload complete. Starting update...");

//             // Open the file for reading and perform OTA update
//             updateFile = SPIFFS.open("/update.bin", "r");
//             if (updateFile)
//             {
//                 performUpdateFromFile(updateFile, contentLength, partitionType);
//                 updateFile.close();
//                 SPIFFS.remove("/update.bin"); // Clean up after update
//             }
//             else
//             {
//                 Serial.println("❌ Failed to reopen update file!");
//             }
//         }
//     }
// }
