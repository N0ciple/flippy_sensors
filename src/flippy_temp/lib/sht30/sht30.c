#include "sht30.h"
#include <furi_hal_i2c.h>
#include <furi_hal.h>
#include <furi.h>

void sht30_init() {
    // Initialization code for the SHT30 sensor, if needed
}

bool sht30_read(Measurement* measurement) {
    uint8_t cmd[2] = {0x24, 0x16};
    uint8_t data[6];

    furi_hal_i2c_acquire(&furi_hal_i2c_handle_external);

    furi_hal_i2c_tx(&furi_hal_i2c_handle_external, 0x44 << 1, cmd, 2, 100);

    furi_delay_ms(30);

    furi_hal_i2c_rx(&furi_hal_i2c_handle_external, 0x44 << 1 | 1, data, 6, 5000);

    furi_hal_i2c_release(&furi_hal_i2c_handle_external);

    measurement->temperature = ((data[0] << 8) | data[1]) * 175.0f / 65535.0f - 45.0f;
    measurement->humidity = ((data[3] << 8) | data[4]) * 100.0f / 65535.0f;
    furi_hal_rtc_get_datetime(&measurement->timestamp);

    return true;
}
