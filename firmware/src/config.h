#pragma once

// MOSFET gate pins (IRLZ34N, low-side)
#define PIN_RED   12
#define PIN_GREEN 13
#define PIN_BLUE  14

#define PWM_FREQ_HZ 1000
#define PWM_RANGE   1023

#define HOSTNAME_PREFIX "deskled"

// Setup access point, used when no Wi-Fi credentials are saved or connection fails
#define SETUP_AP_PASSWORD "deskled-setup"
#define WIFI_CONNECT_TIMEOUT_MS 30000
// Wi-Fi transmit power in dBm (max 20.5). Lower power means smaller current spikes on weak 3.3V supplies;
// signal at the desk is strong, so 12 dBm is enough.
#define WIFI_TX_POWER_DBM 12.0f

// Admin password for web UI, REST API, telnet console and OTA (user "admin") until changed on the device
#define DEFAULT_ADMIN_PASSWORD "deskled-ota"

// Delay before persisting state changes, to spare flash wear
#define STATE_SAVE_DELAY_MS 5000
