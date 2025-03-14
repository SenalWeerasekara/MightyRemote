#include <Arduino.h>
#include <IRrecv.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRac.h>
#include <IRutils.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <FS.h>  // Include the SPIFFS library

// Pin configuration
const uint16_t kRecvPin = 14;  // D5 on D1 Mini (GPIO 14)
const uint16_t kIrLedPin = 12; // D6 on D1 Mini (GPIO 12)

// IR configuration
const uint32_t kBaudRate = 115200;
const uint16_t kCaptureBufferSize = 1024;
const uint8_t kTimeout = 15;
const uint16_t kMinUnknownSize = 12;
const uint8_t kTolerancePercentage = kTolerance;  // kTolerance is normally 25%

IRrecv irrecv(kRecvPin, kCaptureBufferSize, kTimeout, true);  // IR receiver
IRsend irsend(kIrLedPin);  // IR sender
decode_results results;    // Stores the captured IR signal

// Struct to store captured IR signal details
struct IRSignal {
  decode_type_t protocol;  // Protocol type
  uint64_t value;          // Hex value
  uint16_t bits;           // Number of bits
};

const int MAX_SIGNALS = 5;  // Maximum number of signals to store
IRSignal capturedSignals[MAX_SIGNALS];  // Array to store multiple signals
bool signalCaptured[MAX_SIGNALS] = {false};  // Flags to indicate if signals are captured

// Wi-Fi configuration
const char* ssid = "SLT-Fiber-An8kf-2.4G";
const char* password = "TqPPsC49";

ESP8266WebServer server(80);  // Create a web server on port 80

void setup() {
  Serial.begin(kBaudRate, SERIAL_8N1);
  while (!Serial)  // Wait for the serial connection to be established.
    delay(50);

  Serial.println("IR Capture and Replay Ready");

  // Initialize SPIFFS
  if (!SPIFFS.begin()) {
    Serial.println("Failed to mount SPIFFS");
    return;
  }
  Serial.println("SPIFFS mounted");

  // Load saved signals from SPIFFS
  loadSignalsFromSPIFFS();

  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");
  Serial.println(WiFi.localIP());

  // Start the IR sender
  irsend.begin();

  // Disable the IR receiver by default
  irrecv.disableIRIn();

  // Define web server routes
  server.on("/", handleRoot);
  server.on("/capture1", []() { handleCapture(0); });
  server.on("/capture2", []() { handleCapture(1); });
  server.on("/capture3", []() { handleCapture(2); });
  server.on("/capture4", []() { handleCapture(3); });
  server.on("/capture5", []() { handleCapture(4); });
  server.on("/replay1", []() { handleReplay(0); });
  server.on("/replay2", []() { handleReplay(1); });
  server.on("/replay3", []() { handleReplay(2); });
  server.on("/replay4", []() { handleReplay(3); });
  server.on("/replay5", []() { handleReplay(4); });
  server.on("/clear", handleClear);  // Route to clear SPIFFS

  // Start the web server
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();  // Handle client requests
  getSignal();  // Check for IR signals
}

void getSignal() {
  // Check if an IR signal has been captured
  if (irrecv.decode(&results)) {
    Serial.println("IR CAPTURED");

    // Find the first available slot to store the signal
    for (int i = 0; i < MAX_SIGNALS; i++) {
      if (!signalCaptured[i]) {
        // Store the captured signal details in the array
        capturedSignals[i].protocol = results.decode_type;
        capturedSignals[i].value = results.value;
        capturedSignals[i].bits = results.bits;
        signalCaptured[i] = true;  // Set the flag to indicate a signal has been captured

        // Save the signal to SPIFFS
        saveSignalToSPIFFS(i);

        // Print the details of the captured signal
        Serial.print("Signal stored in slot ");
        Serial.println(i);
        Serial.print(resultToHumanReadableBasic(&results));

        // Output the results as source code
        Serial.println(resultToSourceCode(&results));
        Serial.println();  // Blank line between entries

        // Disable the IR receiver after capturing a signal
        irrecv.disableIRIn();
        break;
      }
    }
  }
}

void handleRoot() {
  // Calculate SPIFFS usage
  FSInfo fs_info;
  SPIFFS.info(fs_info);
  int totalSPIFFS = fs_info.totalBytes;
  int usedSPIFFS = fs_info.usedBytes;
  int availableSPIFFS = totalSPIFFS - usedSPIFFS;

  // HTML for the web UI
  String html = "<!DOCTYPE html><html><head><title>IR Capture and Replay</title></head><body>";
  html += "<h1>IR Capture and Replay</h1>";
  html += "<p>SPIFFS Usage:</p>";
  html += "<ul>";
  html += "<li>Total SPIFFS: " + String(totalSPIFFS) + " bytes</li>";
  html += "<li>Used SPIFFS: " + String(usedSPIFFS) + " bytes</li>";
  html += "<li>Available SPIFFS: " + String(availableSPIFFS) + " bytes</li>";
  html += "</ul>";
  html += "<p><a href=\"/capture1\"><button>Capture Signal 1</button></a></p>";
  html += "<p><a href=\"/capture2\"><button>Capture Signal 2</button></a></p>";
  html += "<p><a href=\"/capture3\"><button>Capture Signal 3</button></a></p>";
  html += "<p><a href=\"/capture4\"><button>Capture Signal 4</button></a></p>";
  html += "<p><a href=\"/capture5\"><button>Capture Signal 5</button></a></p>";
  html += "<p><a href=\"/replay1\"><button>Replay Signal 1</button></a></p>";
  html += "<p><a href=\"/replay2\"><button>Replay Signal 2</button></a></p>";
  html += "<p><a href=\"/replay3\"><button>Replay Signal 3</button></a></p>";
  html += "<p><a href=\"/replay4\"><button>Replay Signal 4</button></a></p>";
  html += "<p><a href=\"/replay5\"><button>Replay Signal 5</button></a></p>";
  html += "<p><a href=\"/clear\"><button style=\"background-color:red;color:white;\">Clear SPIFFS</button></a></p>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

void handleCapture(int index) {
  Serial.print("Listening for IR signal for slot ");
  Serial.println(index);
  signalCaptured[index] = false;  // Reset the flag for this slot
  irrecv.enableIRIn();  // Enable the IR receiver
  String html = "<script>alert('Listening for IR signal for slot " + String(index + 1) + "...'); window.location.href='/';</script>";
  server.send(200, "text/html", html);
}

void handleReplay(int index) {
  if (signalCaptured[index]) {
    Serial.print("Replaying captured IR signal from slot ");
    Serial.println(index);
    // Replay the signal based on the captured protocol
    switch (capturedSignals[index].protocol) {
      case decode_type_t::GOODWEATHER:
        irsend.sendGoodweather(capturedSignals[index].value, capturedSignals[index].bits);
        break;
      case decode_type_t::SAMSUNG:
        irsend.sendSAMSUNG(capturedSignals[index].value, capturedSignals[index].bits);
        break;
      default:
        Serial.println("Unsupported protocol for replay.");
        break;
    }
    Serial.println("Signal replayed!");
    server.send(204);  // Send an empty response to the browser
  } else {
    String html = "<script>alert('No saved signal in slot " + String(index + 1) + "!'); window.location.href='/';</script>";
    Serial.print("No IR signal captured yet in slot ");
    Serial.println(index);
    server.send(200, "text/html", html);
  }
}

void handleClear() {
  // Clear all files in SPIFFS
  Dir dir = SPIFFS.openDir("/");
  while (dir.next()) {
    SPIFFS.remove(dir.fileName());
  }

  // Reset the capturedSignals array and flags
  for (int i = 0; i < MAX_SIGNALS; i++) {
    capturedSignals[i] = {};  // Clear the struct
    signalCaptured[i] = false;  // Reset the flag
  }

  Serial.println("SPIFFS cleared!");
  String html = "<script>alert('SPIFFS cleared!'); window.location.href='/';</script>";
  server.send(200, "text/html", html);
}

void saveSignalToSPIFFS(int index) {
  String filename = "/signal" + String(index) + ".bin";
  File file = SPIFFS.open(filename, "w");
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }
  file.write((uint8_t*)&capturedSignals[index], sizeof(IRSignal));
  file.close();
}

void loadSignalsFromSPIFFS() {
  for (int i = 0; i < MAX_SIGNALS; i++) {
    String filename = "/signal" + String(i) + ".bin";
    if (SPIFFS.exists(filename)) {
      File file = SPIFFS.open(filename, "r");
      if (!file) {
        Serial.println("Failed to open file for reading");
        return;
      }
      file.read((uint8_t*)&capturedSignals[i], sizeof(IRSignal));
      file.close();
      signalCaptured[i] = true;  // Mark the slot as captured
    }
  }
}