#include <Arduino.h>
#include <IRrecv.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRac.h>
#include <IRutils.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>

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

IRSignal capturedSignal;   // Variable to store the captured signal
bool signalCaptured = false;  // Flag to indicate if a signal has been captured

// Wi-Fi configuration
const char* ssid = "SLT-Fiber-An8kf-2.4G";
const char* password = "TqPPsC49";

ESP8266WebServer server(80);  // Create a web server on port 80

void setup() {
  Serial.begin(kBaudRate, SERIAL_8N1);
  while (!Serial)  // Wait for the serial connection to be established.
    delay(50);

  Serial.println("IR Capture and Replay Ready");

  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");
  Serial.println(WiFi.localIP());

  // Start the IR receiver and sender
  irrecv.enableIRIn();
  irsend.begin();

  // Disable the IR receiver by default
  irrecv.disableIRIn();

  // Define web server routes
  server.on("/", handleRoot);
  server.on("/capture", handleCapture);
  server.on("/replay", handleReplay);

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
    signalCaptured = true;  // Set the flag to indicate a signal has been captured

    // Store the captured signal details in the struct
    capturedSignal.protocol = results.decode_type;
    capturedSignal.value = results.value;
    capturedSignal.bits = results.bits;

    // Print the details of the captured signal
    Serial.print(resultToHumanReadableBasic(&results));

    // Output the results as source code
    Serial.println(resultToSourceCode(&results));
    Serial.println();  // Blank line between entries

    // irrecv.resume();  // Resume listening for the next signal
     // Disable the IR receiver after capturing a signal
    irrecv.disableIRIn();
  }
}

void handleRoot() {
  // HTML for the web UI
  String html = "<!DOCTYPE html><html><head><title>IR Capture and Replay</title></head><body>";
  html += "<h1>IR Capture and Replay</h1>";
  html += "<p><a href=\"/capture\"><button>Capture IR Signal</button></a></p>";
  html += "<p><a href=\"/replay\"><button>Replay IR Signal</button></a></p>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

void handleCapture() {
  Serial.println("Listening for IR signal...");
  // irrecv.resume();  // Restart the IR receiver
  signalCaptured = false;  // Reset the flag
  irrecv.enableIRIn();  // Enable the IR receiver
  String html = "<script>alert('Listening for IR signal...'); window.location.href='/';</script>";
  server.send(200, "text/html", html);
}

void handleReplay() {
  if (signalCaptured) {
    Serial.println("Replaying captured IR signal...");
    // Replay the signal based on the captured protocol
    switch (capturedSignal.protocol) {
      case decode_type_t::GOODWEATHER:
        irsend.sendGoodweather(capturedSignal.value, capturedSignal.bits);
        break;
      case decode_type_t::SAMSUNG:
        irsend.sendSAMSUNG(capturedSignal.value, capturedSignal.bits);
        break;
      default:
        Serial.println("Unsupported protocol for replay.");
        break;
    }
    Serial.println("Signal replayed!");
    server.send(204);
  } else {
    String html = "<script>alert('No saved signal!'); window.location.href='/';</script>";
    Serial.println("No IR signal captured yet. Press 'R' to capture one.");
    server.send(200, "text/html", html);
  }
}