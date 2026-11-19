#pragma once

#include <cstdint>

struct MotorDuties {
    int a;
    int b;
};

constexpr uint32_t MOTOR_RAMP_MS = 1020;  // 51 increments of 5 at 20 ms intervals
constexpr uint32_t MOTOR_SINGLE_ON_MS = MOTOR_RAMP_MS + 1500;
constexpr uint32_t MOTOR_PAUSE_MS = 800;
constexpr uint32_t MOTOR_SINGLE_PHASE_MS = MOTOR_SINGLE_ON_MS + MOTOR_PAUSE_MS;
constexpr uint32_t MOTOR_PAIR_ON_MS = 2000;
constexpr uint32_t DUAL_MOTOR_TEST_MS = 4 * MOTOR_SINGLE_PHASE_MS + 2 * MOTOR_PAIR_ON_MS + MOTOR_PAUSE_MS;

inline MotorDuties dualMotorTestDuties(uint32_t elapsed) {
    for (unsigned phase = 0; phase < 4; ++phase) {
        if (elapsed < MOTOR_SINGLE_ON_MS) {
            const int duty = elapsed < MOTOR_RAMP_MS ? static_cast<int>(elapsed / 20) * 5 : 255;
            const int signedDuty = phase % 2 == 0 ? duty : -duty;
            return phase < 2 ? MotorDuties{signedDuty, 0} : MotorDuties{0, signedDuty};
        }
        if (elapsed < MOTOR_SINGLE_PHASE_MS) return {0, 0};
        elapsed -= MOTOR_SINGLE_PHASE_MS;
    }
    if (elapsed < MOTOR_PAIR_ON_MS) return {200, 200};
    if (elapsed < MOTOR_PAIR_ON_MS + MOTOR_PAUSE_MS) return {0, 0};
    if (elapsed < 2 * MOTOR_PAIR_ON_MS + MOTOR_PAUSE_MS) return {200, -200};
    return {0, 0};
}
