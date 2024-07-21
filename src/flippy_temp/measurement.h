#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <furi_hal_rtc.h>

typedef struct {
    float temperature;
    float humidity;
    DateTime timestamp;
} Measurement;
