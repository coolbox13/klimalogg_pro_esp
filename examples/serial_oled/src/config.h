// Example settings. Override with -D in platformio.ini build_flags.
#pragma once

// Print every completed telegram, including CRC failures.
#ifndef KL_DEBUG_FRAMES
#define KL_DEBUG_FRAMES 1
#endif

#define KL_SERIAL_BAUD 115200
#define KL_DISPLAY_REFRESH_MS 1000UL
#define KL_STATS_INTERVAL_MS 10000UL
#define KL_LED_FLASH_MS 50UL
