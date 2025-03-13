#include <Arduino.h>
#include <assert.h>
#include <IRrecv.h>
#include <IRremoteESP8266.h>
#include <IRac.h>
#include <IRtext.h>
#include <IRutils.h>
#include <IRsend.h>
#include <FS.h>  // Include the SPIFFS library

// Configuration parameters
const uint16_t kRecvPin = 14;  // IR receive pin
#define IR_LED_PIN 12 
IRsend irsend(IR_LED_PIN);  

// Communication settings
const uint32_t kBaudRate = 115200;
const uint16_t kCaptureBufferSize = 1024;
const int maxRawDataLength = 300;  // Maximum number of raw data values per IR code

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

void setup() {
    irsend.begin();
    Serial.begin(115200);

    // Initialize SPIFFS
    if (!SPIFFS.begin()) {
        Serial.println("Failed to mount SPIFFS");
        return;
    }

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
}

void loop() {
    checkButton();
    
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
        storeRawDataToSPIFFS(dataValues);

        Serial.println();
        Serial.println();
        irrecv.resume();  // Resume IR receiver
    }
}

// Function to store raw data values as uint16_t in SPIFFS
void storeRawDataToSPIFFS(String data) {
    File file = SPIFFS.open("/raw_data.txt", "w");
    if (!file) {
        Serial.println("Failed to open file for writing");
        return;
    }

    file.print(data);
    file.close();
    Serial.println("Data stored in SPIFFS.");
}

// Function to load raw data values from SPIFFS
void loadRawDataFromSPIFFS(uint16_t*& rawData, int& length) {
    File file = SPIFFS.open("/raw_data.txt", "r");
    if (!file) {
        Serial.println("Failed to open file for reading");
        return;
    }

    String data = file.readString();
    file.close();

    // Count the number of raw data values
    int count = 0;
    for (int i = 0; i < data.length(); i++) {
        if (data[i] == ',') count++;
    }
    count++;  // Add 1 for the last value

    // Dynamically allocate memory for rawData
    rawData = new uint16_t[count];
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

    length = index;
}

void checkButton() {
    if (Serial.available()) {
        char input = Serial.read();
        Serial.print("Received character: ");
        Serial.println(input);
        if (input == 'p') {
            Serial.println("Sending IR signal...");

            // Load raw data from SPIFFS
            uint16_t* rawData = nullptr;
            int length;
            loadRawDataFromSPIFFS(rawData, length);

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
            } else {
                Serial.println("Error: Failed to load raw data from SPIFFS!");
            }
        }
    }
}