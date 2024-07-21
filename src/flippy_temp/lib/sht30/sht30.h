#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <furi_hal_rtc.h>
#include "../../measurement.h"

/**
 * @brief Initialize the SHT30 sensor.
 */
void sht30_init();

/**
 * @brief Read data from the SHT30 sensor.
 * 
 * @param measurement Pointer to a Measurement structure to store the read data.
 * @return true if the read was successful, false otherwise.
 */
bool sht30_read(Measurement* measurement);
