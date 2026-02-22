#include "utils/Timer.h"
#include <chrono>

uint64_t Timer::now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
