// **********************************
// * Settings                       *
// **********************************

// * Serial baud rate
#define BAUD_RATE 115200

// * Header pins
#define SDA_PIN D2
#define SDC_PIN D1

// * Define display i2c address
#define I2C_DISPLAY_ADDRESS 0x3C

// Update every 15 seconds if no airplanes around
#define UPDATE_INTERVAL_SECS_LONG 15

 // Update every 3 seconds if there are airplanes
#define UPDATE_INTERVAL_SECS_SHORT 3

// * Flag changed in the ticker function every 10 minutes
bool readyForUpdate = false;

#define UTC_OFFSET 2

// * Hostname
#define HOSTNAME "weatherstation"

// * The password used for uploading
#define OTA_PASSWORD "admin"

// * Wifi timeout in milliseconds
#define WIFI_TIMEOUT 30000

// * Watchdog timer
#define OSWATCH_RESET_TIME 300

// * Watchdog: Will be updated each loop
static unsigned long last_loop;

// * Used by wifi manager to determine if settings should be saved
bool shouldSaveConfig = false;

// * Last update value
unsigned long lastUpdate = 0;

int currentUpdateInterval = (int)UPDATE_INTERVAL_SECS_LONG;
