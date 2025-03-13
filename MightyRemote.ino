#include <Arduino.h>
#include <IRrecv.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRac.h>
#include <IRutils.h>

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

void setup() {
  Serial.begin(kBaudRate, SERIAL_8N1);
  while (!Serial)  // Wait for the serial connection to be established.
    delay(50);

  Serial.println("IR Capture and Replay Ready");
  Serial.println("Press 'R' to capture an IR signal.");
  Serial.println("Press 'P' to replay the captured signal.");

  irrecv.enableIRIn();  // Start the IR receiver
  irsend.begin();       // Start the IR sender
}

void loop() {
  getSignal();
  // Check for serial input
  if (Serial.available()) {
    char input = Serial.read();
    input = toupper(input);  // Convert to uppercase for case-insensitivity
    if (input == 'R') {
      // Start capturing IR signal
      Serial.println("Listening for IR signal...");
      irrecv.resume();  // Restart the IR receiver
      signalCaptured = false;  // Reset the flag
    } else if (input == 'P') {
      // Replay the captured IR signal
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
      } else {
        Serial.println("No IR signal captured yet. Press 'R' to capture one.");
      }
    }
  }
}

void getSignal(){
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

      // Display any extra A/C info if available
      String description = IRAcUtils::resultAcToString(&results);
      // if (description.length()) Serial.println("Description: " + description);

      // Output the results as source code
      Serial.println(resultToSourceCode(&results));
      Serial.println();  // Blank line between entries

      irrecv.resume();  // Resume listening for the next signal
    }
}