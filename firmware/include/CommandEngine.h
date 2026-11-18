#pragma once

#include <cstdint>
#include <cstring>

// Hardware-independent, non-blocking LED state machine, tested on CI.
class CommandEngine {
public:
    enum class Reply { Accepted, On, Off, Status, Pong, Busy, Unknown };

    Reply execute(const char* command, uint32_t now) {
        if (std::strcmp(command, "ping") == 0) return Reply::Pong;
        if (std::strcmp(command, "status") == 0) return Reply::Status;
        const bool blink = std::strcmp(command, "1") == 0;
        const bool on = std::strcmp(command, "on") == 0;
        const bool off = std::strcmp(command, "off") == 0;
        if (!blink && !on && !off) return Reply::Unknown;
        if (busy_) return Reply::Busy;
        if (blink) {
            busy_ = true;
            led_ = true;
            transitions_ = 0;
            lastChange_ = now;
            activeJob_ = ++nextJob_;
            return Reply::Accepted;
        }
        led_ = on;
        return on ? Reply::On : Reply::Off;
    }

    // Returns true only when a blink job finishes. Unsigned arithmetic handles millis() wraparound.
    bool tick(uint32_t now) {
        if (!busy_ || static_cast<uint32_t>(now - lastChange_) < 250) return false;
        lastChange_ = now;
        ++transitions_;
        led_ = (transitions_ % 2 == 0);
        if (transitions_ >= 5) {
            busy_ = false;
            led_ = false;
            completedJob_ = activeJob_;
            return true;
        }
        return false;
    }

    bool led() const { return led_; }
    bool busy() const { return busy_; }
    uint32_t activeJob() const { return activeJob_; }
    uint32_t completedJob() const { return completedJob_; }

private:
    bool led_ = false;
    bool busy_ = false;
    uint8_t transitions_ = 0;
    uint32_t lastChange_ = 0;
    uint32_t nextJob_ = 0;
    uint32_t activeJob_ = 0;
    uint32_t completedJob_ = 0;
};
