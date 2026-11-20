#pragma once

#include <cstdint>
#include <cstdlib>
#include <algorithm>
#include "DualMotorTest.h"

inline MotorDuties mixTracks(int x, int y) {
    const int left = y + x, right = y - x;
    const int scale = std::max(100, std::max(std::abs(left), std::abs(right)));
    return {left * 255 / scale, right * 255 / scale};
}

class RemoteLease {
public:
    bool open(uint32_t id, uint32_t now) {
        if (live(now) || id == 0) return false;
        id_ = id;
        sequence_ = 0;
        updated_ = now;
        return true;
    }
    bool live(uint32_t now) {
        if (id_ && now - updated_ >= 250) stop();
        return id_ != 0;
    }
    bool update(uint32_t id, uint32_t sequence, int x, int y, uint32_t now) {
        if (!live(now) || id_ != id || sequence <= sequence_ || x < -100 || x > 100 || y < -100 || y > 100) return false;
        sequence_ = sequence;
        updated_ = now;
        duty_ = mixTracks(x, y);
        return true;
    }
    MotorDuties output(uint32_t now) { return live(now) ? duty_ : MotorDuties{0, 0}; }
    void stop() { id_ = 0; duty_ = {0, 0}; }
private:
    uint32_t id_ = 0, sequence_ = 0, updated_ = 0;
    MotorDuties duty_{0, 0};
};

class TrackOutput {
public:
    int apply(int duty, uint32_t now) {
        const int sign = (duty > 0) - (duty < 0);
        if (active_ && sign != sign_) {
            neutral_ = now;
            active_ = false;
        }
        if (!sign || (sign_ && sign != sign_ && now - neutral_ < 50)) return 0;
        sign_ = sign;
        active_ = true;
        return duty;
    }
private:
    int sign_ = 0;
    bool active_ = false;
    uint32_t neutral_ = 0;
};

inline int batteryMillivolts(int adc, int gainPermille) {
    if (adc <= 0 || adc > 3000 || gainPermille < 850 || gainPermille > 1150) return 0;
    const int mv = adc * 2 * gainPermille / 1000;
    return mv >= 2500 && mv <= 4400 ? mv : 0;
}

inline bool batteryLow(int mv) { return mv > 0 && mv <= 3350; }

inline bool validBucket(int rest, int raised) {
    return rest >= 1000 && rest <= 2000 && raised >= 1000 && raised <= 2000 && rest != raised;
}

class Bucket {
public:
    int next(int rest, int raised) {
        if (!validBucket(rest, raised)) return 0;
        current_ = current_ == rest ? raised : rest;
        return current_;
    }
private:
    int current_ = 0;
};
