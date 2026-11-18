#pragma once

#include <cstdint>

// Use elapsed time, not chained delays: a late tick never extends a powered phase.
inline int motorATestDuty(uint32_t elapsedMs) {
    if (elapsedMs < 2000) return 150;
    if (elapsedMs < 3000) return 0;
    if (elapsedMs < 5000) return -255;
    return 0;
}
