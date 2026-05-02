#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// Device name that will appear when the ESP32 advertises over BLE.
const char* DEVICE_NAME = "EdgeHax-S3";

// This structure holds the GPIO pin and whether the LED is active-low.
// activeLow=true means the LED turns ON when the pin is LOW.
struct LedConfig {
    int pin;
    bool activeLow;
};

// We are using five LEDs on pins 40-44.
// GPIO39 is omitted because it is input-only on the ESP32-S3.
const LedConfig ledPins[] = {
    {40, false},
    {41, false},
    {42, false},
    {43, true},
    {44, true}
};
const size_t ledCount = sizeof(ledPins) / sizeof(ledPins[0]);

// Track whether each LED is currently ON or OFF.
bool ledStates[ledCount] = {false};

// BLE objects. These are stored globally so the callbacks can use them.
static BLEServer* pServer = nullptr;
static BLECharacteristic* pROChar = nullptr;
static BLECharacteristic* pRWChar = nullptr;
static BLECharacteristic* pNtfChar = nullptr;
static BLECharacteristic* pCmdChar = nullptr;
static BLECharacteristic* pStsChar = nullptr;
static BLECharacteristic* pBlinkCtrlChar = nullptr;
static BLECharacteristic* pBlinkStatusChar = nullptr;

// State values used by the application.
static bool deviceConnected = false;  // true when a BLE client is connected
static String statusValue = "idle"; // text shown by the status characteristic
static bool blinkEnabled = true;     // controls whether LED blink feedback is active
static size_t notifyBlinkIndex = 0;  // cycles through LEDs for notifications

// How often BLE notifications are sent while connected.
const unsigned long notifyInterval = 2000;

// Helper to return the correct HIGH/LOW state based on activeLow wiring.
inline int ledOnState(bool activeLow) {
    return activeLow ? LOW : HIGH;
}

inline int ledOffState(bool activeLow) {
    return activeLow ? HIGH : LOW;
}

// Blink a single LED for a short duration and then restore its previous state.
// If blink feedback is disabled, this function does nothing.
void blinkLed(size_t index, unsigned long durationMs = 100) {
    if (!blinkEnabled || index >= ledCount) {
        return;
    }

    bool previousOn = ledStates[index];
    int blinkState = ledOnState(ledPins[index].activeLow);
    int restoreState = previousOn ? ledOnState(ledPins[index].activeLow) : ledOffState(ledPins[index].activeLow);

    digitalWrite(ledPins[index].pin, blinkState);
    delay(durationMs);
    digitalWrite(ledPins[index].pin, restoreState);
}

// These helper functions associate read/write/notify actions with specific LEDs.
inline size_t getReadBlinkLed() {
    return 0; // use LED 0 for read events
}

inline size_t getWriteBlinkLed() {
    return 1; // use LED 1 for write events
}

inline size_t getNotifyBlinkLed() {
    return notifyBlinkIndex % ledCount;
}

// Control a single LED on or off.
// This updates the internal state and writes the physical GPIO.
void setLedState(size_t index, bool on) {
    if (index >= ledCount) {
        Serial.printf("[LED] Invalid index %u\n", (unsigned)index);
        return;
    }

    ledStates[index] = on;
    digitalWrite(ledPins[index].pin, on ? ledOnState(ledPins[index].activeLow) : ledOffState(ledPins[index].activeLow));
    Serial.printf("[LED] GPIO %d -> %s\n", ledPins[index].pin, on ? "ON" : "OFF");
}

// Turn all LEDs on or all LEDs off.
void setAllLeds(bool on) {
    for (size_t i = 0; i < ledCount; ++i) {
        setLedState(i, on);
    }
}

// Update the status string and also write it to the status characteristic.
void updateStatus(const String& value) {
    statusValue = value;
    if (pStsChar) {
        pStsChar->setValue(statusValue.c_str());
    }
}

// BLE server connection callbacks.
class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) override {
        deviceConnected = true;
        updateStatus("connected");
        Serial.println("[BLE] Client connected");
    }

    void onDisconnect(BLEServer* pServer) override {
        deviceConnected = false;
        updateStatus("idle");
        Serial.println("[BLE] Client disconnected");
        pServer->getAdvertising()->start();
        Serial.println("[BLE] Advertising...");
    }
};

// Read callback for a readable characteristic.
// This is where we perform the LED blink on read events.
class ROCallbacks : public BLECharacteristicCallbacks {
    void onRead(BLECharacteristic* pCharacteristic) override {
        blinkLed(getReadBlinkLed());
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "uptime:%lu", millis());
        pCharacteristic->setValue(buffer);
        Serial.printf("[BLE RO] %s\n", buffer);
    }
};

// Write callback for a read/write characteristic.
// This blinks the write LED and echoes data back.
class RWCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) override {
        blinkLed(getWriteBlinkLed());
        std::string value = pCharacteristic->getValue();
        Serial.print("[BLE RW] ");
        Serial.println(value.c_str());
        pCharacteristic->setValue(value);
    }
};

// Status characteristic read callback.
// Also blinks on read so read operations are visible.
class StsCallbacks : public BLECharacteristicCallbacks {
    void onRead(BLECharacteristic* pCharacteristic) override {
        blinkLed(getReadBlinkLed());
        pCharacteristic->setValue(statusValue.c_str());
        Serial.printf("[BLE STS] %s\n", statusValue.c_str());
    }
};

// BLE command format that is consumed by the write-only command characteristic.
// Commands are raw bytes.
// 0x01 - PING (no LED change)
// 0x02 + text - rename device/status (no LED change)
// 0x03 - turn ALL LEDs ON
// 0x04 - turn ALL LEDs OFF
// 0x05 + [index] - toggle LED at index
// 0x06 + [index][state] - set LED index to ON/OFF
void handleCommand(const std::string& data) {
    if (data.empty()) {
        return;
    }

    uint8_t cmdId = static_cast<uint8_t>(data[0]);
    std::string payload;
    if (data.size() > 1) {
        payload = data.substr(1);
    }

    Serial.printf("[BLE CMD] id=0x%02X payload=%s\n", cmdId, payload.c_str());

    switch (cmdId) {
        case 0x01:
            Serial.println("[CMD] PING");
            updateStatus("pong");
            break;

        case 0x02: {
            String newName(payload.c_str());
            Serial.print("[CMD] Device name = ");
            Serial.println(newName);
            updateStatus("name:" + newName);
            break;
        }

        case 0x03:
            Serial.println("[CMD] All LEDs ON");
            setAllLeds(true);
            updateStatus("leds:on");
            break;

        case 0x04:
            Serial.println("[CMD] All LEDs OFF");
            setAllLeds(false);
            updateStatus("leds:off");
            break;

        case 0x05:
            if (!payload.empty()) {
                uint8_t index = static_cast<uint8_t>(payload[0]);
                if (index < ledCount) {
                    setLedState(index, !ledStates[index]);
                    updateStatus(String("led") + index + ":" + (ledStates[index] ? "on" : "off"));
                } else {
                    updateStatus("led:invalid-index");
                }
            }
            break;

        case 0x06:
            if (payload.size() >= 2) {
                uint8_t index = static_cast<uint8_t>(payload[0]);
                bool on = payload[1] != 0;
                if (index < ledCount) {
                    setLedState(index, on);
                    updateStatus(String("led") + index + ":" + (on ? "on" : "off"));
                } else {
                    updateStatus("led:invalid-index");
                }
            }
            break;

        default:
            Serial.println("[CMD] Unknown command");
            updateStatus("unknown");
            break;
    }
}

// This callback handles writes to the command characteristic.
class CmdCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) override {
        blinkLed(getWriteBlinkLed());
        std::string value = pCharacteristic->getValue();
        handleCommand(value);
    }
};

// Blink control commands are separate from the main command service.
// This allows the app to enable/disable blink feedback without affecting LED commands.
void handleBlinkControl(const std::string& data) {
    if (data.empty()) {
        return;
    }
    uint8_t cmd = static_cast<uint8_t>(data[0]);
    switch (cmd) {
        case 0x00:
            blinkEnabled = false;
            Serial.println("[BLINK] Disabled blink feedback");
            if (pBlinkStatusChar) {
                pBlinkStatusChar->setValue("disabled");
            }
            updateStatus("blink:off");
            break;
        case 0x01:
            blinkEnabled = true;
            Serial.println("[BLINK] Enabled blink feedback");
            if (pBlinkStatusChar) {
                pBlinkStatusChar->setValue("enabled");
            }
            updateStatus("blink:on");
            break;
        case 0x02:
            blinkEnabled = !blinkEnabled;
            Serial.printf("[BLINK] Toggled blink feedback to %s\n", blinkEnabled ? "enabled" : "disabled");
            if (pBlinkStatusChar) {
                pBlinkStatusChar->setValue(blinkEnabled ? "enabled" : "disabled");
            }
            updateStatus(String("blink:") + (blinkEnabled ? "on" : "off"));
            break;
        default:
            Serial.printf("[BLINK] Unknown control byte 0x%02X\n", cmd);
            break;
    }
}

class BlinkCtrlCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) override {
        std::string value = pCharacteristic->getValue();
        handleBlinkControl(value);
    }
};

class BlinkStatusCallbacks : public BLECharacteristicCallbacks {
    void onRead(BLECharacteristic* pCharacteristic) override {
        pCharacteristic->setValue(blinkEnabled ? "enabled" : "disabled");
        Serial.printf("[BLINK STATUS] %s\n", blinkEnabled ? "enabled" : "disabled");
    }
};

// Initialize all LED GPIOs.
// We also set the pin to the off state to avoid accidental LED on during startup.
void initLeds() {
    for (size_t i = 0; i < ledCount; ++i) {
        pinMode(ledPins[i].pin, OUTPUT);
        digitalWrite(ledPins[i].pin, ledOffState(ledPins[i].activeLow));
        Serial.printf("[LED] GPIO %d initialized as OUTPUT, off state=%s\n",
                      ledPins[i].pin,
                      ledPins[i].activeLow ? "HIGH" : "LOW");
    }
}

// Initialize BLE server, services, and characteristics.
void initBle() {
    Serial.println("[BLE] Starting BLE server...");
    BLEDevice::init(DEVICE_NAME);
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());

    // Primary service with read/write and notify characteristics.
    BLEService* svc1 = pServer->createService("12345678-1234-1234-1234-123456789012");
    pROChar = svc1->createCharacteristic(
        "abcdef00-1234-1234-1234-abcdef012345",
        BLECharacteristic::PROPERTY_READ
    );
    pROChar->setCallbacks(new ROCallbacks());

    pRWChar = svc1->createCharacteristic(
        "abcdef01-1234-1234-1234-abcdef012345",
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
    );
    pRWChar->setCallbacks(new RWCallbacks());

    pNtfChar = svc1->createCharacteristic(
        "abcdef02-1234-1234-1234-abcdef012345",
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );
    pNtfChar->addDescriptor(new BLE2902());
    pNtfChar->setValue("");
    svc1->start();

    // Second service for command handling.
    BLEService* svc2 = pServer->createService("87654321-4321-4321-4321-210987654321");
    pCmdChar = svc2->createCharacteristic(
        "fedcba98-7654-3210-fedc-ba9876543210",
        BLECharacteristic::PROPERTY_WRITE
    );
    pCmdChar->setCallbacks(new CmdCallbacks());

    pStsChar = svc2->createCharacteristic(
        "fedcba99-7654-3210-fedc-ba9876543210",
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );
    pStsChar->addDescriptor(new BLE2902());
    pStsChar->setCallbacks(new StsCallbacks());
    pStsChar->setValue(statusValue.c_str());
    svc2->start();

    // Third service for blink control.
    BLEService* svc3 = pServer->createService("abcdef10-1234-1234-1234-abcdef012345");
    pBlinkCtrlChar = svc3->createCharacteristic(
        "abcdef11-1234-1234-1234-abcdef012345",
        BLECharacteristic::PROPERTY_WRITE
    );
    pBlinkCtrlChar->setCallbacks(new BlinkCtrlCallbacks());

    pBlinkStatusChar = svc3->createCharacteristic(
        "abcdef12-1234-1234-1234-abcdef012345",
        BLECharacteristic::PROPERTY_READ
    );
    pBlinkStatusChar->setCallbacks(new BlinkStatusCallbacks());
    pBlinkStatusChar->setValue("enabled");
    svc3->start();

    BLEAdvertising* advertising = BLEDevice::getAdvertising();
    advertising->addServiceUUID("12345678-1234-1234-1234-123456789012");
    advertising->addServiceUUID("abcdef10-1234-1234-1234-abcdef012345");
    advertising->setScanResponse(true);
    advertising->start();

    Serial.println("[BLE] Advertising started");
}

void setup() {
    Serial.begin(115200); // Start serial debug output.
    delay(1000);         // Allow time for the serial monitor to open.
    Serial.println("\nESP32-S3 BLE LED controller starting...");

    initLeds();          // Initialize LED pins.
    initBle();           // Initialize BLE stack and services.
    updateStatus("ready");
}

void loop() {
    static unsigned long lastNotify = 0;
    static unsigned long lastAppPrint = 0;
    static uint32_t tickCounter = 0;
    unsigned long now = millis();

    // Send periodic BLE notifications when a client is connected.
    if (deviceConnected) {
        if (now - lastNotify >= notifyInterval) {
            lastNotify = now;
            blinkLed(getNotifyBlinkLed());
            notifyBlinkIndex = (notifyBlinkIndex + 1) % ledCount;

            char message[128];
            snprintf(message, sizeof(message), "tick:%u status:%s",
                     ++tickCounter,
                     statusValue.c_str());
            if (pNtfChar) {
                pNtfChar->setValue(message);
                pNtfChar->notify();
            }
            Serial.print("[BLE NTF] ");
            Serial.println(message);
        }
    } else {
        // No client connected, print a heartbeat every 2 seconds.
        if (now - lastAppPrint >= 2000) {
            lastAppPrint = now;
            Serial.println("[APP] Waiting for BLE client...");
        }
    }
}
