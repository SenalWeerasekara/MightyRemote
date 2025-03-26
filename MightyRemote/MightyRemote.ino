#include <Arduino.h>
#include <IRrecv.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRac.h>
#include <IRutils.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>  // WiFiManager library
#include <FS.h>  
#include <CuteBuzzerSounds.h>
#include <ESP8266mDNS.h>

// Pin configuration
const uint16_t kRecvPin = 14;  // D5 on D1 Mini (GPIO 14)
const uint16_t kIrLedPin = 12; // D6 on D1 Mini (GPIO 12)
#define BUZZER_PIN 13
#define LED_PIN 2  // D4 on NodeMCU (GPIO 2)
#define BUTTON_PIN 5  // D1 on NodeMCU (GPIO 5)
int currentCaptureSlot = -1;

const String backgroundColor = "#1a1a1a"; // Almost Black
const String containerColor = "#262626"; // Dark Grey
const String buttonColor = "#00bcd4"; // Bright Teal
const String buttonHoverColor = "#0097a7"; // Darker Teal
const String buttonActiveColor = "#00838f"; // Even Darker Teal
const String settingsButtonColor = "#424242"; // Medium Grey
const String settingsButtonHoverColor = "#373737"; // Darker Grey
const String settingsButtonActiveColor = "#2e2e2e"; // Even Darker Grey
const String textColor = "#ffffff"; // White
const String inputBackgroundColor = "#2a2a3f"; // Darker Grey for input fields
const String inputBorderColor = "#444"; // Grey for input borders
const String inputFocusBorderColor = "#00bcd4"; // Teal for input focus

const int MAX_SIGNALS = 20;  // Maximum number of signals to store

WiFiManager wifiManager; 

// IR configuration
const uint32_t kBaudRate = 115200;
const uint16_t kCaptureBufferSize = 1024;
const uint8_t kTimeout = 15;
const uint16_t kMinUnknownSize = 12;
const uint8_t kTolerancePercentage = kTolerance;  // kTolerance is normally 25%

// Static IP configuration
IPAddress staticIP(192, 168, 1, 100);  // Set your desired static IP address
IPAddress gateway(192, 168, 1, 1);     // Set your gateway IP address
IPAddress subnet(255, 255, 255, 0);    // Set your subnet mask
IPAddress dns(8, 8, 8, 8);          

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
  char captureButtonNames[MAX_SIGNALS][32];
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
  cute.play(S_MODE1);

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
    String defaultName = "btn " + String(i + 1);  // Create a String object
    defaultName.toCharArray(buttonNames.captureButtonNames[i], 32);
  }

  loadButtonNamesFromSPIFFS();

  // Set static IP configuration
  wifiManager.setSTAStaticIPConfig(staticIP, gateway, subnet, dns);

   // Connect to Wi-Fi using WiFiManager
  Serial.println("Connecting to WiFi...");
  wifiManager.autoConnect("MightyRemoteAP");  // Create an access point with the name "IRCaptureReplayAP"

  if (!MDNS.begin("mightyremote")) {
    Serial.println("Error setting up MDNS responder!");
  } else {
    Serial.println("mDNS responder started. Access at mightyremote.local");
  }

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
  MDNS.update();
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

    // Check if a capture slot is active
    if (currentCaptureSlot != -1) {
      // Store the captured signal details in the current capture slot
      capturedSignals[currentCaptureSlot].protocol = results.decode_type;
      capturedSignals[currentCaptureSlot].value = results.value;
      capturedSignals[currentCaptureSlot].bits = results.bits;
      signalCaptured[currentCaptureSlot] = true;  // Set the flag to indicate a signal has been captured

      // Save the signal to SPIFFS
      saveSignalToSPIFFS(currentCaptureSlot);

      // Print the details of the captured signal
      Serial.print("Signal stored in slot ");
      Serial.println(currentCaptureSlot);
      Serial.print(resultToHumanReadableBasic(&results));

      // Output the results as source code
      Serial.println(resultToSourceCode(&results));
      Serial.println();  // Blank line between entries

      // Reset the current capture slot
      currentCaptureSlot = -1;
    }

    // Disable the IR receiver after capturing a signal
    irrecv.disableIRIn();
  }
}

void handleRoot() {
  // HTML for the web UI with a modern and beautiful design
  String html = "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">";
  html += "<title>𓃠 MightyRemote</title>";
  html += "<style>";
  html += "body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background-color: " + backgroundColor + "; margin: 0; padding: 20px; display: flex; flex-direction: column; align-items: center; justify-content: center; min-height: 10vh; }";
  html += "h2 { color: " + textColor + "; margin-bottom: 20px; font-size: 2rem; font-weight: 600; }";
  html += ".remote-container { background-color: " + containerColor + "; padding: 25px; border-radius: 20px; box-shadow: 0 8px 16px rgba(0, 0, 0, 0.3); width: 100%; max-width: 600px; }";
  html += ".remote-grid { display: grid; grid-template-columns: repeat(3, 1fr); gap: 15px; }";
  html += ".remote-topBar { display: flex; justify-content: space-between; align-items: center; margin-bottom: 20px; }"; 
  html += "a { text-decoration: none; }";
  html += "button { background-color: " + buttonColor + "; color: " + textColor + "; border: none; padding: 20px 10px; text-align: center; font-size: 16px; font-weight: 500; cursor: pointer; border-radius: 12px; width: 100%; transition: background-color 0.3s ease, transform 0.1s ease; }";
  html += "button:hover { background-color: " + buttonHoverColor + "; }";
  html += "button:active { background-color: " + buttonActiveColor + "; transform: scale(0.95); }";
  html += ".settings-button { grid-column: span 3; background-color: " + settingsButtonColor + "; color: " + textColor + "; border: none; padding: 20px 10px; text-align: center; font-size: 16px; font-weight: 500; cursor: pointer; border-radius: 12px; width: 100%; transition: background-color 0.3s ease, transform 0.1s ease; }";
  html += ".settings-button:hover { background-color: " + settingsButtonHoverColor + "; }";
  html += ".settings-button:active { background-color: " + settingsButtonActiveColor + "; transform: scale(0.95); }";
  html += "@media (max-width: 600px) { .remote-grid { grid-template-columns: repeat(2, 1fr); } .settings-button { grid-column: span 2; } }";
  html += "</style>";
  html += "</head><body>";
  html += "<div class=\"remote-container\">";
  html += "<div class=\"remote-topBar\">";
  html += "<h2>MightyRemote</h2>";
  html += "<h2>𓃠</h2>";
  html += "</div>";
  html += "<div class=\"remote-grid\">";
  for (int i = 0; i < MAX_SIGNALS; i++) {
    html += "<a href=\"/replay" + String(i + 1) + "\"><button>" + String(buttonNames.captureButtonNames[i]) + "</button></a>";
  }
  html += "<a href=\"/settings\"><button class=\"settings-button\">Settings</button></a>";
  html += "</div>";
  html += "</div>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

void handleSettings() {
  // SPIFFS calculation part
  FSInfo fs_info;
  SPIFFS.info(fs_info);
  int totalSPIFFS = fs_info.totalBytes;
  int usedSPIFFS = fs_info.usedBytes;
  int availableSPIFFS = totalSPIFFS - usedSPIFFS;

  String html = "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">";
  html += "<title>𓃠 MightyRemote - Settings</title>";
  html += "<style>";
  html += "body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background-color: " + backgroundColor + "; margin: 0; padding: 20px; display: flex; flex-direction: column; align-items: center; justify-content: center; min-height: 100vh; color: " + textColor + "; }";
  html += "h1 { font-size: 2rem; font-weight: 600; margin-bottom: 20px; }";
  html += "h2 { font-size: 1.5rem; font-weight: 500; margin-bottom: 15px; }";
  html += "p { margin: 10px 0; }";
  html += "ul { list-style-type: none; padding: 0; margin: 0 0 20px 0; }";
  html += "li { margin: 5px 0; }";
  html += "a { text-decoration: none; }";
  html += "button { background-color: " + buttonColor + "; color: " + textColor + "; border: none; padding: 10px 20px; text-align: center; font-size: 14px; font-weight: 500; cursor: pointer; border-radius: 8px; transition: background-color 0.3s ease, transform 0.1s ease; }";
  html += "button:hover { background-color: " + buttonHoverColor + "; }";
  html += "button:active { background-color: " + buttonActiveColor + "; transform: scale(0.95); }";
  html += "button[style*=\"background-color:red\"] { background-color: #ff4757; }";
  html += "button[style*=\"background-color:red\"]:hover { background-color: #ff6b81; }";
  html += "button[style*=\"background-color:red\"]:active { background-color: #ff4757; }";
  html += "form { display: flex; align-items: center; gap: 10px; margin: 10px 0; }";
  html += "input[type=\"text\"] { padding: 8px; border: 1px solid " + inputBorderColor + "; border-radius: 8px; background-color: " + inputBackgroundColor + "; color: " + textColor + "; font-size: 14px; width: 200px; }";
  html += "input[type=\"text\"]:focus { outline: none; border-color: " + inputFocusBorderColor + "; }";
  html += ".container { background-color: " + containerColor + "; padding: 25px; border-radius: 20px; box-shadow: 0 8px 16px rgba(0, 0, 0, 0.3); width: 100%; max-width: 600px; }";
  html += ".button-group { display: flex; gap: 10px; margin: 10px 0; }"; // Added CSS for button group
  html += "</style>";
  html += "</head><body>";
  html += "<div class=\"container\">";
  html += "<h1>𓃠 MightyRemote - Settings</h1>";
  html += "<h2>SPIFFS Memory Usage</h2>";
  html += "<ul>";
  html += "<li>Total SPIFFS: " + String(totalSPIFFS) + " bytes</li>";
  html += "<li>Used SPIFFS: " + String(usedSPIFFS) + " bytes</li>";
  html += "<li>Available SPIFFS: " + String(availableSPIFFS) + " bytes</li>";
  html += "</ul>";

  // Wrap buttons in a div with class "button-group"
  html += "<div class=\"button-group\">";
  html += "<a href=\"/\"><button>Back to Main</button></a>";
  html += "<a href=\"/clear\"><button style=\"background-color:red;\">Clear SPIFFS</button></a>";
  html += "</div>";

  html += "<h2>Capture IR Signals</h2>";
  for (int i = 0; i < MAX_SIGNALS; i++) {
    html += "<form action=\"/saveButtonName\" method=\"POST\">";
    html += "<input type=\"hidden\" name=\"index\" value=\"" + String(i) + "\">";
    html += "<label for=\"buttonName" + String(i) + "\">Button " + String(i + 1) + " Name:</label>";
    html += "<input type=\"text\" id=\"buttonName" + String(i) + "\" name=\"buttonName\" value=\"" + String(buttonNames.captureButtonNames[i]) + "\">";
    html += "<button type=\"submit\">Save</button>";
    html += "</form>";
    html += "<p><a href=\"/capture" + String(i + 1) + "\"><button>Capture Signal " + String(i + 1) + "</button></a></p>";
  }
  html += "</div>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

void handleCapture(int index) {
  Serial.print("Listening for IR signal for slot ");
  Serial.println(index);
  currentCaptureSlot = index;  // Set the current capture slot
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
      case decode_type_t::COOLIX:
        irsend.sendCOOLIX(capturedSignals[index].value, capturedSignals[index].bits);
        break;
      case decode_type_t::LG:
        irsend.sendLG(capturedSignals[index].value, capturedSignals[index].bits);
        break;
      case decode_type_t::KELON:
        irsend.sendKelon(capturedSignals[index].value, capturedSignals[index].bits);
        break;
      case decode_type_t::SONY:
        irsend.sendSony(capturedSignals[index].value, capturedSignals[index].bits);
        break;
      case decode_type_t::PANASONIC:
        irsend.sendPanasonic(capturedSignals[index].value, capturedSignals[index].bits);
        break;
      case decode_type_t::MITSUBISHI:
        irsend.sendMitsubishi(capturedSignals[index].value, capturedSignals[index].bits);
        break;
      case decode_type_t::SHARP:
        irsend.sendSharp(capturedSignals[index].value, capturedSignals[index].bits);
        break;
      case decode_type_t::RC5:
        irsend.sendRC5(capturedSignals[index].value, capturedSignals[index].bits);
        break;
      case decode_type_t::NEC:
        irsend.sendNEC(capturedSignals[index].value, capturedSignals[index].bits);
        break;
      case decode_type_t::NIKAI:
        irsend.sendNikai(capturedSignals[index].value, capturedSignals[index].bits);
        break;
      default:
        Serial.println("Unsupported protocol for replay.");
        playCuteErrorChirp();
        break;
    }
    Serial.println("Signal replayed!");
    cute.play(S_CONNECTION);
    server.send(204);  // Send an empty response to the browser
  } else {
    String html = "<script>alert('No saved signal in slot " + String(index + 1) + "!'); window.location.href='/';</script>";
    Serial.print("No IR signal captured yet in slot ");
    playCuteErrorChirp();
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

void handleSaveButtonName() {
  if (server.method() == HTTP_POST) {
    int index = server.arg("index").toInt();
    String newName = server.arg("buttonName");
    if (index >= 0 && index < MAX_SIGNALS && newName.length() < 32) {  // Ensure the name fits
      newName.toCharArray(buttonNames.captureButtonNames[index], 32);  // Copy the string into the char array
      saveButtonNamesToSPIFFS();
      Serial.println("Button name saved: " + newName);
    } else {
      Serial.println("Invalid button name or index.");
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
    Serial.println("Button names loaded from SPIFFS.");
  } else {
    Serial.println("No button names file found. Initializing default names.");
    for (int i = 0; i < MAX_SIGNALS; i++) {
      String defaultName = "btn " + String(i + 1);
      defaultName.toCharArray(buttonNames.captureButtonNames[i], 32);  // Copy the default name into the char array
    }
    saveButtonNamesToSPIFFS();  // Save the default names to SPIFFS
  }
}

void playCuteErrorChirp() {
  for (int i = 0; i < 5; i++) {
    tone(BUZZER_PIN, 1200, 50); // 1200 Hz for 50 ms
    delay(100); // 50 ms tone + 50 ms gap
  }
  noTone(BUZZER_PIN);
}

// Startup Sounds
void playHappyChime() {
  tone(BUZZER_PIN, 784, 100); // G5
  delay(150);
  tone(BUZZER_PIN, 988, 100); // B5
  delay(150);
  tone(BUZZER_PIN, 1319, 100); // E6
  delay(100);
  noTone(BUZZER_PIN);
}

