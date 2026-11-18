#include <Arduino.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <atomic>
#include "CommandEngine.h"

#ifndef LED_PIN
#define LED_PIN 2
#endif
#ifndef LED_ACTIVE_HIGH
#define LED_ACTIVE_HIGH 1
#endif

namespace {
constexpr char SERVICE_UUID[] = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr char RX_UUID[] = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr char TX_UUID[] = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr size_t MAX_FRAME = 20;

WebServer http(80);
CommandEngine engine;
BLECharacteristic* tx = nullptr;
String deviceName;
String deviceToken;
QueueHandle_t bleQueue = nullptr;

// BLE callbacks run in another task. Only immutable credentials and atomics cross tasks.
std::atomic<uint32_t> bleEpoch{0};
std::atomic<bool> bleConnected{false};
std::atomic<bool> restartAdvertising{false};
uint32_t authenticatedEpoch = 0;  // Accessed only by loop().
uint32_t blinkRecipientEpoch = 0;

struct BLEFrame {
    uint32_t epoch;
    char text[MAX_FRAME + 1];
};

bool authenticated() {
    return bleConnected.load() && authenticatedEpoch != 0 && authenticatedEpoch == bleEpoch.load();
}

void notify(const char* reply, uint32_t epoch) {
    if (!bleConnected.load() || epoch != bleEpoch.load()) return;
    tx->setValue(reinterpret_cast<uint8_t*>(const_cast<char*>(reply)), std::strlen(reply));
    tx->notify();
}

class ServerCallbacks final : public BLEServerCallbacks {
    void onConnect(BLEServer*) override {
        bleEpoch.fetch_add(1);
        bleConnected.store(true);
    }
    void onDisconnect(BLEServer*) override {
        bleConnected.store(false);
        bleEpoch.fetch_add(1);
        restartAdvertising.store(true);
    }
};

class CommandCallbacks final : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* characteristic) override {
        const std::string value = characteristic->getValue();
        if (value.empty() || value.size() > MAX_FRAME) return;
        for (unsigned char c : value) if (c < 32 || c > 126) return;
        BLEFrame frame{};
        frame.epoch = bleEpoch.load();
        std::memcpy(frame.text, value.data(), value.size());
        // A saturated queue drops writes. The app reports a timeout and never retries automatically.
        xQueueSend(bleQueue, &frame, 0);
    }
};

ServerCallbacks serverCallbacks;
CommandCallbacks commandCallbacks;

const char* replyFor(CommandEngine::Reply reply) {
    switch (reply) {
        case CommandEngine::Reply::Accepted: return "accepted:1";
        case CommandEngine::Reply::On: return "ok:on";
        case CommandEngine::Reply::Off: return "ok:off";
        case CommandEngine::Reply::Pong: return "pong";
        case CommandEngine::Reply::Busy: return "error:busy";
        case CommandEngine::Reply::Unknown: return "error:command";
        case CommandEngine::Reply::Status:
            return engine.busy() ? "status:blinking" : (engine.led() ? "status:on" : "status:off");
    }
    return "error:command";
}

void processBLE() {
    BLEFrame frame{};
    // Bound the work per iteration so HTTP and LED timing remain responsive.
    for (int i = 0; i < 4 && xQueueReceive(bleQueue, &frame, 0) == pdTRUE; ++i) {
        if (!bleConnected.load() || frame.epoch != bleEpoch.load()) continue;
        if (std::strncmp(frame.text, "auth:", 5) == 0) {
            if (deviceToken == frame.text + 5) {
                authenticatedEpoch = frame.epoch;
                notify("ready", frame.epoch);
            } else {
                authenticatedEpoch = 0;
                notify("error:auth", frame.epoch);
            }
            continue;
        }
        if (!authenticated()) { notify("error:auth", frame.epoch); continue; }
        const auto result = engine.execute(frame.text, millis());
        if (result == CommandEngine::Reply::Accepted) blinkRecipientEpoch = frame.epoch;
        notify(replyFor(result), frame.epoch);
    }
}

bool authorizeHTTP() {
    if (http.header("X-Device-Token") == deviceToken) return true;
    http.send(401, "application/json", "{\"reply\":\"error:auth\"}");
    return false;
}

void setupHTTP() {
    const char* headers[] = {"X-Device-Token"};
    http.collectHeaders(headers, 1);
    http.on("/api/status", HTTP_GET, [] {
        if (!authorizeHTTP()) return;
        String json = "{\"name\":\"" + deviceName + "\",\"protocolVersion\":1,\"busy\":";
        json += engine.busy() ? "true" : "false";
        json += ",\"led\":";
        json += engine.led() ? "true" : "false";
        json += ",\"completedJob\":" + String(engine.completedJob()) + "}";
        http.sendHeader("Cache-Control", "no-store");
        http.send(200, "application/json", json);
    });
    http.on("/api/command", HTTP_POST, [] {
        if (!authorizeHTTP()) return;
        String command = http.arg("plain");
        if (command.length() == 0 || command.length() > MAX_FRAME) {
            http.send(400, "application/json", "{\"reply\":\"error:command\"}");
            return;
        }
        for (size_t i = 0; i < command.length(); ++i) {
            if (command[i] < 32 || command[i] > 126) {
                http.send(400, "application/json", "{\"reply\":\"error:command\"}");
                return;
            }
        }
        const auto result = engine.execute(command.c_str(), millis());
        int code = 200;
        if (result == CommandEngine::Reply::Accepted) { code = 202; blinkRecipientEpoch = 0; }
        if (result == CommandEngine::Reply::Busy) code = 409;
        if (result == CommandEngine::Reply::Unknown) code = 400;
        String json = String("{\"reply\":\"") + replyFor(result) + "\"";
        if (result == CommandEngine::Reply::Accepted) json += ",\"job\":" + String(engine.activeJob());
        json += "}";
        http.sendHeader("Cache-Control", "no-store");
        http.send(code, "application/json", json);
    });
    http.onNotFound([] { http.send(404, "application/json", "{\"reply\":\"error:not-found\"}"); });
    http.begin();
}

void loadIdentity() {
    Preferences preferences;
    preferences.begin("esp-link", false);
    deviceToken = preferences.getString("token", "");
    if (deviceToken.length() != 12) {
        char token[13];
        const uint64_t random = (static_cast<uint64_t>(esp_random()) << 32) | esp_random();
        snprintf(token, sizeof(token), "%012llx", static_cast<unsigned long long>(random & 0xFFFFFFFFFFFFULL));
        deviceToken = token;
        if (preferences.putString("token", deviceToken) == 0) {
            Serial.println("WARNING: token was not saved to NVS.");
        }
    }
    preferences.end();
    char suffix[5];
    snprintf(suffix, sizeof(suffix), "%04X", static_cast<unsigned>(ESP.getEfuseMac() & 0xFFFF));
    deviceName = String("ESP32-Link-") + suffix;
}

void writeLED() {
    digitalWrite(LED_PIN, engine.led() == static_cast<bool>(LED_ACTIVE_HIGH) ? HIGH : LOW);
}
}  // namespace

void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    writeLED();
    bleQueue = xQueueCreate(8, sizeof(BLEFrame));
    if (!bleQueue) { Serial.println("FATAL: cannot create BLE queue"); while (true) delay(1000); }
    WiFi.mode(WIFI_AP);
    loadIdentity();
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
    const bool wifiOK = WiFi.softAP(deviceName.c_str(), deviceToken.c_str(), 1, false, 2);
    setupHTTP();

    BLEDevice::init(deviceName.c_str());
    BLEServer* server = BLEDevice::createServer();
    server->setCallbacks(&serverCallbacks);
    BLEService* service = server->createService(SERVICE_UUID);
    BLECharacteristic* rx = service->createCharacteristic(RX_UUID, BLECharacteristic::PROPERTY_WRITE);
    rx->setCallbacks(&commandCallbacks);
    tx = service->createCharacteristic(TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
    tx->addDescriptor(new BLE2902());
    service->start();
    BLEAdvertising* advertising = BLEDevice::getAdvertising();
    advertising->addServiceUUID(SERVICE_UUID);
    advertising->setScanResponse(true);
    advertising->setMinPreferred(0x06);
    advertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();

    Serial.println("\n=== ESP Link / uPesy ESP32 Wroom DevKit ===");
    Serial.println("Device / Wi-Fi SSID: " + deviceName);
    Serial.println("Device token / Wi-Fi password: " + deviceToken);
    Serial.println("HTTP address: http://" + WiFi.softAPIP().toString());
    Serial.println(wifiOK ? "Wi-Fi AP ready. BLE ready." : "WARNING: Wi-Fi AP failed. BLE ready.");
    Serial.printf("LED: GPIO %d. Commands: 1, on, off, status, ping\n", LED_PIN);
    Serial.println("Keep the token private. USB Serial prints it again after RESET.");
}

void loop() {
    if (restartAdvertising.exchange(false)) {
        authenticatedEpoch = 0;
        BLEDevice::startAdvertising();
    }
    processBLE();
    http.handleClient();
    if (engine.tick(millis())) {
        if (blinkRecipientEpoch != 0 && authenticated() && blinkRecipientEpoch == bleEpoch.load()) {
            notify("done:1", blinkRecipientEpoch);
        }
        blinkRecipientEpoch = 0;
    }
    writeLED();
    delay(1);
}
