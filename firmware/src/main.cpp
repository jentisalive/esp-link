#include <Arduino.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_system.h>
#include <esp_bt.h>
#include <esp32-hal-bt.h>
#include <esp_arduino_version.h>
#include <mutex>
#include "CommandEngine.h"
#include "DualMotorTest.h"
#include "MotorATest.h"
#include "PasswordPolicy.h"
#include "RobotRemote.h"

#ifndef LED_PIN
#define LED_PIN 2
#endif
#ifndef LED_ACTIVE_HIGH
#define LED_ACTIVE_HIGH 1
#endif

namespace {
constexpr size_t MAX_COMMAND = 20;
constexpr uint8_t MOTOR_PINS[] = {25, 26, 27, 14};
constexpr uint8_t MOTOR_PIN_COUNT = sizeof(MOTOR_PINS) / sizeof(MOTOR_PINS[0]);
constexpr unsigned MOTOR_PWM_FREQ = 10000;
constexpr uint8_t MOTOR_PWM_BITS = 8;
constexpr uint32_t MOTOR_TICK_MS = 10;
constexpr uint32_t MOTOR_MAX_TICK_GAP_MS = 100;
constexpr uint8_t BUCKET_PIN = 33, BUCKET_CHANNEL = 4, BATTERY_PIN = 34;
std::mutex driveLock;
RemoteLease drive;
Bucket bucket;
bool bucketAttached = false;
uint16_t bucketRest = 0, bucketRaised = 0, batteryGain = 0;
int leftSign = 1, rightSign = 1;
bool swapTracks = false;
enum class MotorState : uint8_t { Idle, RequestedA, RequestedAB, RunningA, RunningAB, Stopping };
MotorState motorState = MotorState::Idle;
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

// The drive lock also covers output writes; a revoked lease cannot apply its old PWM afterward.
void stopLocked() {
    drive.stop();
    if (motorState != MotorState::Idle) motorState = MotorState::Stopping;
}

void requestMotorStop() {
    const std::lock_guard<std::mutex> lock(driveLock);
    stopLocked();
}

void writeMotorPin(uint8_t index, int duty) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(MOTOR_PINS[index], duty);
#else
    ledcWrite(index, duty);
#endif
}

bool setupMotorOutputs() {
    for (auto pin : MOTOR_PINS) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);
    }
    for (uint8_t index = 0; index < MOTOR_PIN_COUNT; ++index) {
        bool attached;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
        attached = ledcAttachChannel(MOTOR_PINS[index], MOTOR_PWM_FREQ, MOTOR_PWM_BITS, index);
#else
        attached = ledcSetup(index, MOTOR_PWM_FREQ, MOTOR_PWM_BITS) > 0;
        if (attached) ledcAttachPin(MOTOR_PINS[index], index);
#endif
        if (!attached) {
            for (uint8_t active = 0; active < index; ++active) {
                writeMotorPin(active, 0);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
                ledcDetach(MOTOR_PINS[active]);
#else
                ledcDetachPin(MOTOR_PINS[active]);
#endif
                pinMode(MOTOR_PINS[active], OUTPUT);
                digitalWrite(MOTOR_PINS[active], LOW);
            }
            return false;
        }
        writeMotorPin(index, 0);
    }
    return true;
}

bool moveBucket() {
    if (!validBucket(bucketRest, bucketRaised)) return false;
    if (!bucketAttached) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
        bucketAttached = ledcAttachChannel(BUCKET_PIN, 50, 14, BUCKET_CHANNEL);
#else
        bucketAttached = ledcSetup(BUCKET_CHANNEL, 50, 14) > 0;
        if (bucketAttached) ledcAttachPin(BUCKET_PIN, BUCKET_CHANNEL);
#endif
    }
    if (!bucketAttached) return false;
    const unsigned duty = static_cast<unsigned>(bucket.next(bucketRest, bucketRaised)) * 16384 / 20000;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    return ledcWrite(BUCKET_PIN, duty);
#else
    ledcWrite(BUCKET_CHANNEL, duty);
    return true;
#endif
}

void writeMotor(uint8_t first, int duty) {
    // Drop the opposite input first. Both LOW is the inactive state.
    if (duty > 0) {
        writeMotorPin(first + 1, 0);
        writeMotorPin(first, duty);
    } else {
        writeMotorPin(first, 0);
        writeMotorPin(first + 1, -duty);
    }
}

// Keep the cutoff independent of blocking HTTP/Serial work in loop().
void motorTask(void*) {
    uint32_t started = 0;
    uint32_t lastTick = 0;
    MotorDuties previous{0, 0};
    TrackOutput trackA, trackB;
    for (;;) {
        {
            const std::lock_guard<std::mutex> lock(driveLock);
            const uint32_t now = millis();
            if (motorState == MotorState::RequestedA || motorState == MotorState::RequestedAB) {
                motorState = motorState == MotorState::RequestedA ? MotorState::RunningA : MotorState::RunningAB;
                started = lastTick = now;
            }
            const uint32_t elapsed = now - started;
            MotorDuties next{0, 0};
            if (motorState == MotorState::RunningA || motorState == MotorState::RunningAB) {
                const bool dual = motorState == MotorState::RunningAB;
                if (elapsed >= (dual ? DUAL_MOTOR_TEST_MS : 5000u) || now - lastTick > MOTOR_MAX_TICK_GAP_MS) {
                    // If scheduling skipped part of the cycle, abort rather than jump to another phase.
                    stopLocked();
                } else {
                    next = dual ? dualMotorTestDuties(elapsed) : MotorDuties{motorATestDuty(elapsed), 0};
                }
            } else if (motorState == MotorState::Idle) {
                if (now - lastTick > MOTOR_MAX_TICK_GAP_MS) drive.stop();
                next = drive.output(now);
                next = swapTracks ? MotorDuties{next.b * rightSign, next.a * leftSign}
                                  : MotorDuties{next.a * leftSign, next.b * rightSign};
            }
            lastTick = now;
            next = {trackA.apply(next.a, now), trackB.apply(next.b, now)};
            if (next.a != previous.a) writeMotor(0, next.a);
            if (next.b != previous.b) writeMotor(2, next.b);
            previous = next;
            if (motorState == MotorState::Stopping) motorState = MotorState::Idle;
        }
        constexpr TickType_t tickDelay = pdMS_TO_TICKS(MOTOR_TICK_MS);
        vTaskDelay(tickDelay > 0 ? tickDelay : 1);
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

int batteryAdcMillivolts() {
    int sample = 0;
    for (int i = 0; i < 8; ++i) sample += analogReadMilliVolts(BATTERY_PIN);
    return sample / 8;
}

void setupRemoteHTTP() {
    http.on("/api/remote", HTTP_GET, [] {
        if (!authorizeHTTP()) return;
        const int mv = batteryGain ? batteryMillivolts(batteryAdcMillivolts(), batteryGain) : 0;
        String json = String("{\"version\":1,\"motors\":") + (motorAvailable ? "true" : "false") +
            ",\"bucket\":" + (validBucket(bucketRest, bucketRaised) ? "true" : "false") +
            ",\"mv\":" + String(mv) + ",\"low\":" + (batteryLow(mv) ? "true" : "false") + "}";
        http.sendHeader("Cache-Control", "no-store");
        http.send(200, "application/json", json);
    });
    http.on("/api/remote/arm", HTTP_POST, [] {
        if (!authorizeHTTP()) return;
        if (!motorAvailable || !WiFi.softAPgetStationNum()) {
            http.send(503, "application/json", "{}"); return;
        }
        const uint32_t id = esp_random();
        bool ok;
        {
            const std::lock_guard<std::mutex> lock(driveLock);
            ok = motorState == MotorState::Idle && drive.open(id, millis());
        }
        if (!ok) { http.send(409, "application/json", "{}"); return; }
        if (!WiFi.softAPgetStationNum()) { requestMotorStop(); http.send(503, "application/json", "{}"); return; }
        http.send(200, "application/json", String("{\"id\":") + String(id) + "}");
    });
    http.on("/api/remote/frame", HTTP_POST, [] {
        if (!authorizeHTTP()) return;
        const String body = http.arg("plain");
        unsigned long id, seq;
        int x, y, toggle, consumed = 0;
        if (body.length() > 32 || sscanf(body.c_str(), "%10lu,%6lu,%4d,%4d,%1d%n", &id, &seq, &x, &y, &toggle, &consumed) != 5 ||
            static_cast<unsigned>(consumed) != body.length() || (toggle != 0 && toggle != 1)) {
            http.send(400, "application/json", "{}"); return;
        }
        if (!WiFi.softAPgetStationNum()) { requestMotorStop(); http.send(503, "application/json", "{}"); return; }
        bool ok, servoOK = true;
        {
            const std::lock_guard<std::mutex> lock(driveLock);
            ok = motorState == MotorState::Idle && drive.update(id, seq, x, y, millis());
            if (ok && toggle && !moveBucket()) { servoOK = false; stopLocked(); }
        }
        if (!ok) { http.send(409, "application/json", "{}"); return; }
        if (!servoOK) { http.send(503, "application/json", "{}"); return; }
        http.send(200, "application/json", "{}");
    });
}

void setupHTTP() {
    const char* headers[] = {"X-Device-Token"};
    http.collectHeaders(headers, 1);
    setupRemoteHTTP();
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
        if (command == "m1test" || command == "mtest") {
            const bool dual = command == "mtest";
            if (!motorAvailable || WiFi.softAPgetStationNum() == 0) {
                http.send(503, "application/json", "{\"reply\":\"error:m1-unavailable\"}");
                return;
            }
            bool started;
            {
                const std::lock_guard<std::mutex> lock(driveLock);
                started = !drive.live(millis()) && motorState == MotorState::Idle;
                if (started) motorState = dual ? MotorState::RequestedAB : MotorState::RequestedA;
            }
            if (!started) {
                http.send(409, "application/json", "{\"reply\":\"error:m1-busy\"}");
                return;
            }
            // Cover a station disconnect racing with the state change above.
            if (WiFi.softAPgetStationNum() == 0) requestMotorStop();
            http.send(202, "application/json", dual ? "{\"reply\":\"ok:mtest\"}" : "{\"reply\":\"ok:m1test\"}");
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
    const uint32_t pair = preferences.getUInt("bucket-pair", 0);
    bucketRest = pair >> 16;
    bucketRaised = pair & 0xffff;
    batteryGain = preferences.getUShort("battery-gain", 0);
    const uint8_t tracks = preferences.getUChar("track-flags", 0);
    swapTracks = tracks & 1;
    leftSign = tracks & 2 ? -1 : 1;
    rightSign = tracks & 4 ? -1 : 1;
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
    Serial.println("\n=== ESP Link 1.3.0 / Wi-Fi ONLY / ROBOT REMOTE ===");
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
    int first = 0, second = 0, third = 0, consumed = 0;
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
    } else if (strcmp(serialLine, "esplink:battery-adc") == 0) {
        Serial.printf("ESPLINK:BATTERY-ADC-MV:%d\n", batteryAdcMillivolts());
    } else if (sscanf(serialLine, "esplink:bucket:%4d,%4d%n", &first, &second, &consumed) == 2 &&
               consumed > 0 && serialLine[consumed] == '\0' && ((first == 0 && second == 0) || validBucket(first, second))) {
        Preferences prefs;
        if (!prefs.begin("esp-link", false) || !prefs.putUInt("bucket-pair", (static_cast<uint32_t>(first) << 16) | second)) {
            Serial.println("ESPLINK:ERROR:NVS"); return;
        }
        Serial.println("ESPLINK:ROBOT-CONFIG-SAVED: press EN to apply");
    } else if (sscanf(serialLine, "esplink:battery:%4d%n", &first, &consumed) == 1 && consumed > 0 &&
               serialLine[consumed] == '\0' && (first == 0 || (first >= 850 && first <= 1150))) {
        Preferences prefs;
        if (!prefs.begin("esp-link", false) || !prefs.putUShort("battery-gain", first)) {
            Serial.println("ESPLINK:ERROR:NVS"); return;
        }
        Serial.println("ESPLINK:ROBOT-CONFIG-SAVED: press EN to apply");
    } else if (sscanf(serialLine, "esplink:tracks:%1d,%1d,%1d%n", &first, &second, &third, &consumed) == 3 &&
               consumed > 0 && serialLine[consumed] == '\0' && (first | second | third) <= 1 &&
               first >= 0 && second >= 0 && third >= 0) {
        Preferences prefs;
        if (!prefs.begin("esp-link", false) || !prefs.putUChar("track-flags", first | (second << 1) | (third << 2))) {
            Serial.println("ESPLINK:ERROR:NVS"); return;
        }
        Serial.println("ESPLINK:ROBOT-CONFIG-SAVED: press EN to apply");
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
    const bool motorOutputs = setupMotorOutputs();
    pinMode(BUCKET_PIN, OUTPUT);
    digitalWrite(BUCKET_PIN, LOW);
    analogSetPinAttenuation(BATTERY_PIN, ADC_11db);
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
    motorAvailable = motorOutputs && xTaskCreate(motorTask, "motors", 3072, nullptr, 2, nullptr) == pdPASS;
    if (!motorAvailable) Serial.println("ERROR: motor outputs unavailable");
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
