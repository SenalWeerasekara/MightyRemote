#include <Arduino.h>
#include <IRrecv.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRac.h>
#include <IRutils.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>  // Include WiFiManager library
#include <FS.h>  // Include the SPIFFS library
#include <CuteBuzzerSounds.h>

// Pin configuration
const uint16_t kRecvPin = 14;  // D5 on D1 Mini (GPIO 14)
const uint16_t kIrLedPin = 12; // D6 on D1 Mini (GPIO 12)
const int MAX_SIGNALS = 20;  // Maximum number of signals to store

#define BUZZER_PIN 13
#define LED_PIN 2  // D4 on NodeMCU (GPIO 2)
#define BUTTON_PIN 5  // D1 on NodeMCU (GPIO 5)

WiFiManager wifiManager; 

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

struct ButtonNames {
  String captureButtonNames[MAX_SIGNALS];
};

ButtonNames buttonNames;  // Global variable to store button names

IRSignal capturedSignals[MAX_SIGNALS];  // Array to store multiple signals
bool signalCaptured[MAX_SIGNALS] = {false};  // Flags to indicate if signals are captured

ESP8266WebServer server(80);  // Create a web server on port 80

void setup() {
  Serial.begin(kBaudRate, SERIAL_8N1);
  while (!Serial)  // Wait for the serial connection to be established.
    delay(50);

  Serial.println("IR Capture and Replay Ready");
  cute.init(BUZZER_PIN);

  // Initialize LED pin
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);  

  pinMode(BUTTON_PIN, INPUT_PULLUP);  // Use internal pull-up resistor

  // Initialize SPIFFS
  if (!SPIFFS.begin()) {
    Serial.println("Failed to mount SPIFFS");
    return;
  }
  Serial.println("SPIFFS mounted");

  // Load saved signals from SPIFFS
  loadSignalsFromSPIFFS();

    // Initialize default button names if not already set
  for (int i = 0; i < MAX_SIGNALS; i++) {
    buttonNames.captureButtonNames[i] = "btn " + String(i + 1);  // Default names: btn 1, btn 2, etc.
  }

  loadButtonNamesFromSPIFFS();

   // Connect to Wi-Fi using WiFiManager
  Serial.println("Connecting to WiFi...");
  wifiManager.autoConnect("MightyRemoteAP");  // Create an access point with the name "IRCaptureReplayAP"

  Serial.println("Connected to WiFi");
  Serial.println(WiFi.localIP());
  digitalWrite(LED_PIN, LOW); 
  cute.play(S_MODE3);

  // Start the IR sender
  irsend.begin();

  // Disable the IR receiver by default
  irrecv.disableIRIn();

  // Define web server routes
  server.on("/", handleRoot);
  // Define web server routes using a loop
  for (int i = 0; i < MAX_SIGNALS; i++) {
    // Capture routes
    server.on("/capture" + String(i + 1), [i]() { handleCapture(i); });
    // Replay routes
    server.on("/replay" + String(i + 1), [i]() { handleReplay(i); });
  }
  server.on("/clear", handleClear);  // Route to clear SPIFFS
  server.on("/settings", handleSettings);
  server.on("/settings", handleSettings);
  server.on("/saveButtonName", handleSaveButtonName);  

  // Start the web server
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();  // Handle client requests
  getSignal();  // Check for IR signals

  // Check WiFi connection status and control LED
  if (WiFi.status() != WL_CONNECTED) {
    digitalWrite(LED_PIN, HIGH);  // Turn on LED if not connected to WiFi
    Serial.println("WiFi disconnected. Attempting to reconnect...");
    wifiManager.autoConnect("MightyRemoteAP");  // Attempt to reconnect
    if (WiFi.status() == WL_CONNECTED) {
      digitalWrite(LED_PIN, LOW);  // Turn off LED if reconnected
      Serial.println("Reconnected to WiFi");
    }
  } else {
    digitalWrite(LED_PIN, LOW);  
  }

  // Check if the button is pressed to start AP mode
  if (digitalRead(BUTTON_PIN) == LOW) {  // Button is pressed (LOW because of pull-up)
    delay(50);  // Simple debounce delay
    if (digitalRead(BUTTON_PIN) == LOW) {  // Confirm button is still pressed
      Serial.println("Button pressed. Starting AP mode...");
      digitalWrite(LED_PIN, HIGH);
      cute.play(S_SAD);
      wifiManager.resetSettings();  // Reset saved WiFi settings
      wifiManager.startConfigPortal("MightyRemoteAP");  // Start the configuration portal
      Serial.println("AP mode started. Connect to 'MightyRemoteAP' to configure WiFi.");
    }
  }
}

void getSignal() {
  // Check if an IR signal has been captured
  if (irrecv.decode(&results)) {
    Serial.println("IR CAPTURED");
    cute.play(S_DISCONNECTION);

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
  // HTML for the web UI
  String html = "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>MightyRemote</title></head><body>";
  html += "<h1>≽^•⩊•^≼ Mighty Remote </h1>";
  for (int i = 0; i < MAX_SIGNALS; i++) {
    html += "<p><a href=\"/replay" + String(i + 1) + "\"><button>" + buttonNames.captureButtonNames[i] + "</button></a></p>";
  }
  html += "<p><a href=\"/settings\"><button>Go to Settings</button></a></p>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

void handleSettings() {
    // Calculate SPIFFS usage
  FSInfo fs_info;
  SPIFFS.info(fs_info);
  int totalSPIFFS = fs_info.totalBytes;
  int usedSPIFFS = fs_info.usedBytes;
  int availableSPIFFS = totalSPIFFS - usedSPIFFS;

  // HTML for the settings page
  String html = "<!DOCTYPE html><html><head><title>Settings - IR Capture and Replay</title></head><body>";
  html += "<h1>Settings - IR Capture and Replay</h1>";
  html += "<p>SPIFFS Usage:</p>";
  html += "<ul>";
  html += "<li>Total SPIFFS: " + String(totalSPIFFS) + " bytes</li>";
  html += "<li>Used SPIFFS: " + String(usedSPIFFS) + " bytes</li>";
  html += "<li>Available SPIFFS: " + String(availableSPIFFS) + " bytes</li>";
  html += "</ul>";
  html += "<h2>Capture IR Signals</h2>";
  html += "<p><a href=\"/clear\"><button style=\"background-color:red;color:white;\">Clear SPIFFS</button></a></p>";
  html += "<p>Please perform a power cycle once the storage is cleared!</p>";
  
  for (int i = 0; i < MAX_SIGNALS; i++) {
    html += "<p>";
    html += "<form action=\"/saveButtonName\" method=\"POST\">";
    html += "<input type=\"hidden\" name=\"index\" value=\"" + String(i) + "\">";
    html += "<label for=\"buttonName" + String(i) + "\">Capture Signal " + String(i + 1) + " Name:</label>";
    html += "<input type=\"text\" id=\"buttonName" + String(i) + "\" name=\"buttonName\" value=\"" + buttonNames.captureButtonNames[i] + "\">";
    html += "<button type=\"submit\">Save</button>";
    html += "</form>";
    html += "<a href=\"/capture" + String(i + 1) + "\"><button>Capture Signal " + String(i + 1) + "</button></a>";
    html += "</p>";
  }
  
  html += "<p><a href=\"/\"><button>Back to Main</button></a></p>";
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
    cute.play(S_CONNECTION);
    server.send(204);  // Send an empty response to the browser
  } else {
    String html = "<script>alert('No saved signal in slot " + String(index + 1) + "!'); window.location.href='/';</script>";
    Serial.print("No IR signal captured yet in slot ");
    cute.play(S_SLEEPING); 
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
  cute.play(S_SAD);
  server.send(200, "text/html", html);
}

void saveSignalToSPIFFS(int index) {
  String filename = "/signalFile" + String(index) + ".bin";
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
    String filename = "/signalFile" + String(i) + ".bin";
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

void handleSaveButtonName() {
  if (server.method() == HTTP_POST) {
    int index = server.arg("index").toInt();
    String newName = server.arg("buttonName");
    if (index >= 0 && index < MAX_SIGNALS) {
      buttonNames.captureButtonNames[index] = newName;
      saveButtonNamesToSPIFFS();
      Serial.println("Button name saved: " + newName);
    }
  }
  server.sendHeader("Location", "/settings");
  server.send(303);
}

void saveButtonNamesToSPIFFS() {
  File file = SPIFFS.open("/buttonNames.bin", "w");
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }
  file.write((uint8_t*)&buttonNames, sizeof(ButtonNames));
  file.close();
}

void loadButtonNamesFromSPIFFS() {
  if (SPIFFS.exists("/buttonNames.bin")) {
    File file = SPIFFS.open("/buttonNames.bin", "r");
    if (!file) {
      Serial.println("Failed to open file for reading");
      return;
    }
    file.read((uint8_t*)&buttonNames, sizeof(ButtonNames));
    file.close();
  }
}