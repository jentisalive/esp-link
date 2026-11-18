#include <Arduino.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_system.h>
#include <esp_bt.h>
#include <esp32-hal-bt.h>
#include <atomic>
#include "CommandEngine.h"
#include "MotorATest.h"
#include "PasswordPolicy.h"

#ifndef LED_PIN
#define LED_PIN 2
#endif
#ifndef LED_ACTIVE_HIGH
#define LED_ACTIVE_HIGH 1
#endif

namespace {
constexpr size_t MAX_COMMAND = 20;
constexpr uint8_t MOTOR_A_IN1 = 25;
constexpr uint8_t MOTOR_A_IN2 = 26;
enum class MotorState : uint8_t { Idle, Requested, Running, Stopping };
std::atomic<MotorState> motorState{MotorState::Idle};
bool motorAvailable = false;
WebServer http(80);
CommandEngine engine;
String deviceName;
String deviceToken;
String wifiPassword;
bool restartAP = false;
char serialLine[96]{};
size_t serialLength = 0;
bool discardSerialLine = false;
uint32_t serialLastByte = 0;

void requestMotorStop() {
    auto state = motorState.load();
    while (state != MotorState::Idle && state != MotorState::Stopping &&
           !motorState.compare_exchange_weak(state, MotorState::Stopping)) {}
}

void writeMotorA(int duty) {
    // Drop the opposite input first. Both LOW is the inactive state.
    if (duty > 0) {
        analogWrite(MOTOR_A_IN2, 0);
        analogWrite(MOTOR_A_IN1, duty);
    } else {
        analogWrite(MOTOR_A_IN1, 0);
        analogWrite(MOTOR_A_IN2, -duty);
    }
}

// Keep the cutoff independent of blocking HTTP/Serial work in loop().
void motorTask(void*) {
    uint32_t started = 0;
    int previousDuty = 0;
    for (;;) {
        auto state = motorState.load();
        if (state == MotorState::Requested) {
            if (motorState.compare_exchange_strong(state, MotorState::Running)) started = millis();
            state = motorState.load();
        }
        const uint32_t elapsed = millis() - started;
        if (state == MotorState::Running && elapsed >= 5000) {
            requestMotorStop();
            state = MotorState::Stopping;
        }
        int duty = state == MotorState::Running ? motorATestDuty(elapsed) : 0;
        if (previousDuty > 0 && duty < 0) {
            // A stalled task missed the neutral phase: abort instead of reversing abruptly.
            requestMotorStop();
            state = MotorState::Stopping;
            duty = 0;
        }
        if (duty != previousDuty) {
            writeMotorA(duty);
            previousDuty = duty;
        }
        if (state == MotorState::Stopping) motorState.store(MotorState::Idle);
        vTaskDelay(pdMS_TO_TICKS(10) > 0 ? pdMS_TO_TICKS(10) : 1);
    }
}

String randomHex(size_t length) {
    String result;
    result.reserve(length);
    while (result.length() < length) {
        char block[9];
        snprintf(block, sizeof(block), "%08lx", static_cast<unsigned long>(esp_random()));
        result += block;
    }
    return result.substring(0, length);
}

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

// This is an application token, separate from the WPA2 password. No credentials in URLs.
bool authorizeHTTP() {
    const String supplied = http.header("X-Device-Token");
    uint8_t difference = supplied.length() == deviceToken.length() ? 0 : 1;
    for (size_t i = 0; i < deviceToken.length(); ++i) {
        const uint8_t value = i < supplied.length() ? supplied[i] : 0;
        difference |= value ^ static_cast<uint8_t>(deviceToken[i]);
    }
    if (difference == 0) return true;
    http.send(401, "application/json", "{\"reply\":\"error:auth\"}");
    return false;
}

void setupHTTP() {
    const char* headers[] = {"X-Device-Token"};
    http.collectHeaders(headers, 1);
    http.on("/api/status", HTTP_GET, [] {
        if (!authorizeHTTP()) return;
        String json = "{\"name\":\"" + deviceName + "\",\"protocolVersion\":1,\"transport\":\"wifi-only\",\"busy\":";
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
        if (command.length() == 0 || command.length() > MAX_COMMAND) {
            http.send(400, "application/json", "{\"reply\":\"error:command\"}"); return;
        }
        for (size_t i = 0; i < command.length(); ++i) {
            if (command[i] < 32 || command[i] > 126) {
                http.send(400, "application/json", "{\"reply\":\"error:command\"}"); return;
            }
        }
        if (command == "m1stop") {
            requestMotorStop();
            http.send(200, "application/json", "{\"reply\":\"ok:m1stop\"}");
            return;
        }
        if (command == "m1test") {
            if (!motorAvailable || WiFi.softAPgetStationNum() == 0) {
                http.send(503, "application/json", "{\"reply\":\"error:m1-unavailable\"}");
                return;
            }
            auto idle = MotorState::Idle;
            if (!motorState.compare_exchange_strong(idle, MotorState::Requested)) {
                http.send(409, "application/json", "{\"reply\":\"error:m1-busy\"}");
                return;
            }
            // Cover a station disconnect racing with the state change above.
            if (WiFi.softAPgetStationNum() == 0) requestMotorStop();
            http.send(202, "application/json", "{\"reply\":\"ok:m1test\"}");
            return;
        }
        const auto result = engine.execute(command.c_str(), millis());
        int code = 200;
        if (result == CommandEngine::Reply::Accepted) code = 202;
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

bool validDeviceToken(const String& token) {
    if (token.length() < 8 || token.length() > 64) return false;
    for (size_t i = 0; i < token.length(); ++i) {
        const char c = token[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z'))) return false;
    }
    return true;
}

void loadIdentity() {
    Preferences preferences;
    if (!preferences.begin("esp-link", false)) {
        Serial.println("FATAL: cannot open NVS"); while (true) delay(1000);
    }
    deviceToken = preferences.getString("token", "");
    if (!validDeviceToken(deviceToken)) {
        deviceToken = randomHex(12);
        if (preferences.putString("token", deviceToken) == 0) {
            Serial.println("FATAL: cannot save token"); while (true) delay(1000);
        }
    }
    wifiPassword = preferences.getString("wifi-pass", "");
    if (!validWifiPassword(wifiPassword.c_str())) {
        // New and upgraded boards receive an independent random WPA2 password.
        wifiPassword = randomHex(20);
        if (preferences.putString("wifi-pass", wifiPassword) == 0) {
            Serial.println("FATAL: cannot save Wi-Fi password"); while (true) delay(1000);
        }
    }
    preferences.end();
    char suffix[5];
    snprintf(suffix, sizeof(suffix), "%04X", static_cast<unsigned>(ESP.getEfuseMac() & 0xFFFF));
    deviceName = String("ESP32-Link-") + suffix;
}

void startAP() {
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
    // One connected station: this controller is intended for one robot operator.
    const bool ok = WiFi.softAP(deviceName.c_str(), wifiPassword.c_str(), 1, false, 1);
    Serial.println(ok ? "Wi-Fi AP ready. Bluetooth controller: OFF." : "ERROR: Wi-Fi AP failed.");
}

void printIdentity() {
    Serial.println("\n=== ESP Link 1.2.5 / Wi-Fi ONLY ===");
    Serial.println("Device / Wi-Fi SSID: " + deviceName);
    Serial.println("Device token: " + deviceToken);
    Serial.println("Wi-Fi password: " + wifiPassword);
    Serial.println("HTTP address: http://192.168.4.1");
    Serial.println("Token and Wi-Fi password are DIFFERENT. Keep both private.");
    Serial.println("Change Wi-Fi password over USB only: change_wifi_password.bat");
    Serial.println("Rotate device token over USB only: change_device_token.bat");
}

void saveDeviceToken(const String& next) {
    if (next == deviceToken || next.equalsIgnoreCase(wifiPassword)) {
        Serial.println("ESPLINK:ERROR:TOKEN-REUSED"); return;
    }
    if (!validDeviceToken(next)) { Serial.println("ESPLINK:ERROR:TOKEN-FORMAT"); return; }
    Preferences preferences;
    if (!preferences.begin("esp-link", false)) { Serial.println("ESPLINK:ERROR:NVS"); return; }
    const size_t saved = preferences.putString("token", next);
    preferences.end();
    if (saved == 0) { Serial.println("ESPLINK:ERROR:NVS"); return; }
    deviceToken = next;
    requestMotorStop();
    Serial.println("ESPLINK:TOKEN-SAVED");
}

void rotateDeviceToken() {
    String next;
    do { next = randomHex(12); } while (next == deviceToken || next.equalsIgnoreCase(wifiPassword));
    saveDeviceToken(next);
}

void processSerialCommand() {
    constexpr char tokenPrefix[] = "esplink:set-token:";
    if (strcmp(serialLine, "esplink:hello") == 0) {
        Serial.println("ESPLINK:WIFI-ONLY:1");
    } else if (strcmp(serialLine, "esplink:info") == 0) {
        printIdentity();
    } else if (strcmp(serialLine, "esplink:token-capability") == 0) {
        Serial.println("ESPLINK:TOKEN-ROTATION:1");
    } else if (strcmp(serialLine, "esplink:manual-token-capability") == 0) {
        Serial.println("ESPLINK:MANUAL-TOKEN:1");
    } else if (strcmp(serialLine, "esplink:alphanumeric-token-capability") == 0) {
        Serial.println("ESPLINK:ALPHANUMERIC-TOKEN:2");
    } else if (strncmp(serialLine, tokenPrefix, sizeof(tokenPrefix) - 1) == 0) {
        String next = serialLine + sizeof(tokenPrefix) - 1;
        next.toLowerCase();
        saveDeviceToken(next);
    } else if (strcmp(serialLine, "esplink:rotate-token") == 0) {
        rotateDeviceToken();
    } else {
        constexpr char prefix[] = "esplink:wifi-password:";
        if (strncmp(serialLine, prefix, sizeof(prefix) - 1) != 0) {
            Serial.println("ESPLINK:ERROR:COMMAND"); return;
        }
        const char* password = serialLine + sizeof(prefix) - 1;
        if (!validWifiPassword(password)) { Serial.println("ESPLINK:ERROR:PASSWORD"); return; }
        Preferences preferences;
        if (!preferences.begin("esp-link", false)) { Serial.println("ESPLINK:ERROR:NVS"); return; }
        const size_t saved = preferences.putString("wifi-pass", password);
        preferences.end();
        if (saved == 0) { Serial.println("ESPLINK:ERROR:NVS"); return; }
        wifiPassword = password;
        // Never echo the submitted password. It is not accepted through any HTTP endpoint.
        Serial.println("ESPLINK:PASSWORD-SAVED");
        Serial.flush();
        restartAP = true;
    }
}

void processSerial() {
    if (serialLength && static_cast<uint32_t>(millis() - serialLastByte) > 2000) {
        memset(serialLine, 0, sizeof(serialLine));
        serialLength = 0;
        discardSerialLine = true;  // Ignore the tail of a timed-out frame until its newline.
    }
    for (int i = 0; i < 128 && Serial.available(); ++i) {
        const char value = static_cast<char>(Serial.read());
        serialLastByte = millis();
        if (value == '\r') continue;
        if (value == '\n') {
            if (discardSerialLine) Serial.println("ESPLINK:ERROR:FRAME");
            else if (serialLength) { serialLine[serialLength] = '\0'; processSerialCommand(); }
            memset(serialLine, 0, sizeof(serialLine));
            serialLength = 0;
            discardSerialLine = false;
        } else if (!discardSerialLine) {
            if (value < 32 || value > 126 || serialLength >= sizeof(serialLine) - 1) {
                memset(serialLine, 0, sizeof(serialLine)); serialLength = 0; discardSerialLine = true;
            } else serialLine[serialLength++] = value;
        }
    }
}

void writeLED() {
    digitalWrite(LED_PIN, engine.led() == static_cast<bool>(LED_ACTIVE_HIGH) ? HIGH : LOW);
}
}  // namespace

void setup() {
    Serial.begin(115200);
    pinMode(MOTOR_A_IN1, OUTPUT);
    pinMode(MOTOR_A_IN2, OUTPUT);
    digitalWrite(MOTOR_A_IN1, LOW);
    digitalWrite(MOTOR_A_IN2, LOW);
    motorAvailable = xTaskCreate(motorTask, "motorA", 3072, nullptr, 2, nullptr) == pdPASS;
    pinMode(LED_PIN, OUTPUT);
    writeLED();
    // No Bluetooth stack, services, advertising, scanning or connections are initialized.
    if (!btStop() || esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_IDLE) {
        Serial.println("FATAL: Bluetooth controller could not be disabled"); while (true) delay(1000);
    }
    esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);
    WiFi.persistent(false);  // AP credentials live only in our explicit NVS settings.
    WiFi.onEvent([](WiFiEvent_t) { requestMotorStop(); }, ARDUINO_EVENT_WIFI_AP_STADISCONNECTED);
    WiFi.mode(WIFI_AP);
    loadIdentity();
    startAP();
    setupHTTP();
    printIdentity();
}

void loop() {
    processSerial();
    if (restartAP) {
        requestMotorStop();
        restartAP = false;
        http.stop();
        WiFi.softAPdisconnect(false);
        startAP();
        http.begin();
    }
    http.handleClient();
    engine.tick(millis());
    writeLED();
    delay(1);
}
