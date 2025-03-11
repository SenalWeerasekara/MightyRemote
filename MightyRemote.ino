#include <Arduino.h>
#include <assert.h>
#include <IRrecv.h>
#include <IRremoteESP8266.h>
#include <IRac.h>
#include <IRtext.h>
#include <IRutils.h>
#include <IRsend.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <ESP8266WebServer.h>

// Configuration parameters
const uint16_t kRecvPin = 14;  // IR receive pin
#define IR_LED_PIN 12 
IRsend irsend(IR_LED_PIN);  
#define EEPROM_SIZE 512
const int rawDataStartAddress = 0;  // Starting address in EEPROM to store the raw data

// Communication settings
const uint32_t kBaudRate = 115200;
const uint16_t kCaptureBufferSize = 1024;
const int maxRawDataLength = 100;  // Maximum number of raw data values per IR code

// Protocol-specific timeouts
#if DECODE_AC
const uint8_t kTimeout = 50;
#else  
const uint8_t kTimeout = 15;
#endif  // DECODE_AC

// IR protocol settings
const uint16_t kMinUnknownSize = 12;
const uint8_t kTolerancePercentage = 25;  // Changed from undefined kTolerance

// Initialize IR objects
IRrecv irrecv(kRecvPin, kCaptureBufferSize, kTimeout, true);
decode_results results;

// Web server and WiFiManager
ESP8266WebServer server(80);
WiFiManager wifiManager;

void setup() {
    irsend.begin();
    Serial.begin(115200);
    EEPROM.begin(EEPROM_SIZE);  // Initialize EEPROM with the defined size

    #ifdef ESP8266
    // Serial.begin(kBaudRate, SERIAL_8N1, SERIAL_TX_ONLY);
    #endif  // ESP8266
    
    while (!Serial) delay(50);
    assert(irutils::lowLevelSanityCheck() == 0);
    
    #if DECODE_HASH
    irrecv.setUnknownThreshold(kMinUnknownSize);
    #endif  // DECODE_HASH
    
    irrecv.setTolerance(kTolerancePercentage);
    irrecv.enableIRIn();

    // Connect to Wi-Fi using WiFiManager
    wifiManager.autoConnect("IR_Remote_AP");

    // Start the web server
    server.on("/", handleRoot);
    server.on("/sendIR", handleSendIR);
    server.begin();
    Serial.println("HTTP server started");
}

void loop() {
    server.handleClient();
    
    if (irrecv.decode(&results)) {
        if (results.overflow) {
            Serial.print("semora 1 - ");
            Serial.printf(D_WARN_BUFFERFULL "\n", kCaptureBufferSize);
            Serial.println();
        }
        
        Serial.print("semora 3 - ");
        Serial.print(resultToHumanReadableBasic(&results));
        Serial.println();
        
        String description = IRAcUtils::resultAcToString(&results);
        if (description.length()) {
            Serial.print("semora 4 - ");
            Serial.println(D_STR_MESGDESC ": " + description);
            Serial.println();
        }
        
        yield();
        
        #ifdef LEGACY_TIMING_INFO
        Serial.print("semora 5- ");
        Serial.println(resultToTimingInfo(&results));
        Serial.println();
        yield();  // Feed WDT
        #endif
        
        Serial.print("semora 6 - ");
        String rawDataStringTest = resultToSourceCode(&results);
        Serial.println(rawDataStringTest);

        // Extract and store only the raw data values
        int startIndex = rawDataStringTest.indexOf('{') + 1;
        int endIndex = rawDataStringTest.indexOf('}');
        String dataValues = rawDataStringTest.substring(startIndex, endIndex);
        storeRawDataToEEPROM(dataValues);

        Serial.println();
        Serial.println();
        irrecv.resume();  // Resume IR receiver
    }
}

// Function to store raw data values as uint16_t in EEPROM
void storeRawDataToEEPROM(String data) {
    // Count the number of raw data values
    int count = 0;
    for (int i = 0; i < data.length(); i++) {
        if (data[i] == ',') count++;
    }
    count++;  // Add 1 for the last value

    // Dynamically allocate memory for rawData
    uint16_t* rawData = new uint16_t[count];
    if (rawData == nullptr) {
        Serial.println("Error: Failed to allocate memory!");
        return;
    }

    int index = 0;

    // Convert the String to a mutable char array
    char rawDataArray[data.length() + 1];
    data.toCharArray(rawDataArray, sizeof(rawDataArray));

    // Parse the raw data values
    char* token = strtok(rawDataArray, ",");
    while (token != nullptr) {
        if (strlen(token) > 0) {
            rawData[index++] = atoi(token);  // Convert token to uint16_t
        }
        token = strtok(nullptr, ",");
    }

    // Check if there's enough space in EEPROM
    if (rawDataStartAddress + (index * 2) > EEPROM_SIZE) {
        Serial.println("Error: Not enough space in EEPROM!");
        delete[] rawData;  // Free the allocated memory
        return;
    }

    // Store raw data in EEPROM
    for (int i = 0; i < index; i++) {
        EEPROM.put(rawDataStartAddress + (i * 2), rawData[i]);  // Store each uint16_t value
    }
    EEPROM.put(rawDataStartAddress + (index * 2), (uint16_t)0xFFFF);  // End marker
    EEPROM.commit();
    Serial.println("Data stored in EEPROM.");

    // Free the allocated memory
    delete[] rawData;
}

// Function to load raw data values from EEPROM
void loadRawDataFromEEPROM(uint16_t*& rawData, int& length) {
    length = 0;
    uint16_t value;
    int address = rawDataStartAddress;

    // Count the number of values in EEPROM
    while (true) {
        EEPROM.get(address, value);  // Read a uint16_t value
        if (value == 0xFFFF) break;  // Stop at end marker
        length++;
        address += 2;  // Move to the next uint16_t value
    }

    // Dynamically allocate memory for rawData
    rawData = new uint16_t[length];
    if (rawData == nullptr) {
        Serial.println("Error: Failed to allocate memory!");
        return;
    }

    // Read values into rawData
    address = rawDataStartAddress;
    for (int i = 0; i < length; i++) {
        EEPROM.get(address, rawData[i]);
        address += 2;
    }
}

// Web server handlers
void handleRoot() {
    String html = "<!DOCTYPE html><html><head><title>IR Remote Control</title></head><body>";
    html += "<h1>IR Remote Control</h1>";
    html += "<button onclick=\"sendIR()\">Send IR Signal</button>";
    html += "<script>function sendIR() { fetch('/sendIR').then(response => response.text()).then(data => console.log(data)); }</script>";
    html += "</body></html>";
    server.send(200, "text/html", html);
}

void handleSendIR() {
    Serial.println("Sending IR signal...");

    // Load raw data from EEPROM
    uint16_t* rawData = nullptr;
    int length;
    loadRawDataFromEEPROM(rawData, length);

    if (rawData != nullptr) {
        // Print the raw data for debugging
        Serial.println("Raw Data: ");
        for (int i = 0; i < length; i++) {
            Serial.print(rawData[i]);
            Serial.print(" ");
        }
        Serial.println();

        // Send the raw IR data
        irsend.sendRaw(rawData, length, 38);

        // Free the allocated memory
        delete[] rawData;

        server.send(200, "text/plain", "IR Signal Sent");
    } else {
        server.send(500, "text/plain", "Error: Failed to load raw data from EEPROM!");
    }
}