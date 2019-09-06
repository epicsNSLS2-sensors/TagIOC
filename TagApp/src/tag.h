// period of motion sensor scanning in 10s of ms
// minimum: 0x0A (100ms)
// max: 0xFF (2.55s)
#define MOTION_PERIOD 0x0A

// delay (in seconds) in between attempts to reconnect to tag
#define RECONNECT_DELAY 3

#define TEMP_HUMIDITY_UUID "21"
#define TEMP_PRESSURE_UUID "41"
#define LIGHT_UUID "71"
#define MOTION_UUID "81"
#define MOTION_PERIOD_UUID "83"
#define BUTTON_UUID "-1"
#define BATTERY_UUID "-2"

char mac_address[100];