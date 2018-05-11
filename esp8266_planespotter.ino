#include <FS.h>
#include <Wire.h>
#include <Ticker.h>
#include <DNSServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <JsonListener.h>
#include <SSD1306Wire.h>
#include <OLEDDisplayUi.h>
#include <TimeClient.h>

// * Include local stuff
#include "AdsbExchangeClient.h"
#include "settings.h"
#include "images.h"
#include "query.h"

// * Initiate led blinker library
Ticker ticker;

// * Initiate Watchdog
Ticker tickerOSWatch;

// * Initiate WIFI client
WiFiClient espClient;

// * Initiate display
SSD1306Wire display(I2C_DISPLAY_ADDRESS, SDA_PIN, SDC_PIN);

// * Initiate Menu
OLEDDisplayUi ui( &display );

// * Initiate time library
TimeClient timeClient(UTC_OFFSET);

// Initialize adsbexchange client
AdsbExchangeClient adsbClient;

void configModeCallback (WiFiManager *myWiFiManager);
void drawProgress(OLEDDisplay *display, int percentage, String label);
void drawOtaProgress(unsigned int, unsigned int);
void updateData(OLEDDisplay *display);
void drawCurrentAirplane1(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void drawCurrentAirplane2(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void drawCurrentAirplane3(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void drawHeaderOverlay(OLEDDisplay *display, OLEDDisplayUiState* state);
void drawTextAsBigAsPossible(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y, String text, int maxWidth);
void drawHeading(OLEDDisplay *display, int x, int y, double heading);
void checkReadyForUpdate();
int8_t getWifiQuality();

FrameCallback frames[] = { drawCurrentAirplane1, drawCurrentAirplane2, drawCurrentAirplane3 };
int numberOfFrames = 3;

OverlayCallback overlays[] = { drawHeaderOverlay };
int numberOfOverlays = 1;


// **********************************
// * System Functions               *
// **********************************

// * Watchdog function
void ICACHE_RAM_ATTR osWatch(void)
{
    unsigned long t = millis();
    unsigned long last_run = abs(t - last_loop);
    if (last_run >= (OSWATCH_RESET_TIME * 1000)) {
        // save the hit here to eeprom or to rtc memory if needed
        ESP.restart();  // normal reboot
    }
}

// * Gets called when WiFiManager enters configuration mode
void configModeCallback(WiFiManager *myWiFiManager)
{
    Serial.println("Entered config mode");
    Serial.println(WiFi.softAPIP());

    //if you used auto generated SSID, print it
    Serial.println(myWiFiManager->getConfigPortalSSID());

    display.clear();
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setFont(ArialMT_Plain_10);
    display.drawString(64, 10, "WIFI Manager");
    display.drawString(64, 20, "Please connect to AP");
    display.drawString(64, 30, myWiFiManager->getConfigPortalSSID());
    display.drawString(64, 40, "To finish WIFI Configuration");
    display.display();

    // * Entered config mode, make led toggle faster
    ticker.attach(0.2, tick);
}

// * Callback notifying us of the need to save config
void save_wifi_config_callback ()
{
    shouldSaveConfig = true;
}

// * Blink on-board Led
void tick()
{
    // * Toggle state
    int state = digitalRead(LED_BUILTIN);    // * Get the current state of GPIO1 pin
    digitalWrite(LED_BUILTIN, !state);       // * Set pin to the opposite state
}

// **********************************
// * OTA helpers                    *
// **********************************

// * Setup update over the air
void setup_ota()
{
    Serial.println(F("Arduino OTA activated."));

    // * Port defaults to 8266
    ArduinoOTA.setPort(8266);

    // * Set hostname for OTA
    ArduinoOTA.setHostname(HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);

    ArduinoOTA.onStart([]() {
        Serial.println(F("Arduino OTA: Start"));
    });

    ArduinoOTA.onEnd([]() {
        Serial.println(F("Arduino OTA: End (Running reboot)"));
    });

    ArduinoOTA.onProgress(drawOtaProgress);

    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Arduino OTA Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR)
            Serial.println(F("Arduino OTA: Auth Failed"));
        else if (error == OTA_BEGIN_ERROR)
            Serial.println(F("Arduino OTA: Begin Failed"));
        else if (error == OTA_CONNECT_ERROR)
            Serial.println(F("Arduino OTA: Connect Failed"));
        else if (error == OTA_RECEIVE_ERROR)
            Serial.println(F("Arduino OTA: Receive Failed"));
        else if (error == OTA_END_ERROR)
            Serial.println(F("Arduino OTA: End Failed"));
    });
    ArduinoOTA.begin();
    Serial.println(F("Arduino OTA finished"));
}

// **********************************
// * UI                             *
// **********************************

void setup_ui()
{
    // * Setup frame display time to 10 sec
    ui.setTargetFPS(30);
    ui.setTimePerFrame(10*1000);

    // * Hack until disableIndicator works: Set an empty symbol
    ui.setActiveSymbol(emptySymbol);
    ui.setInactiveSymbol(emptySymbol);
    ui.disableIndicator();

    // * You can change the transition that is used: [SLIDE_LEFT, SLIDE_RIGHT, SLIDE_TOP, SLIDE_DOWN]
    ui.setFrameAnimation(SLIDE_LEFT);
    ui.setFrames(frames, numberOfFrames);
    ui.setOverlays(overlays, numberOfOverlays);
    ui.init();
}

// **********************************
// * Setup                          *
// **********************************

void setup()
{
    // Configure Watchdog
    last_loop = millis();
    tickerOSWatch.attach_ms(((OSWATCH_RESET_TIME / 3) * 1000), osWatch);

    // * Configure Serial
    Serial.begin(BAUD_RATE);

    // * Set led pin as output
    pinMode(LED_BUILTIN, OUTPUT);

    // * Start ticker with 0.5 because we start in AP mode and try to connect
    ticker.attach(0.6, tick);

    // * Initialize display
    display.init();
    display.clear();
    display.display();

    // display.flipScreenVertically();  // Comment out to flip display 180deg
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setContrast(255);

    // * Print WIFI logo while connecting to AP
    display.drawXbm(-6, 5, WiFi_Logo_width, WiFi_Logo_height, WiFi_Logo_bits);
    display.drawString(88, 18, "Plane Spotter");
    display.display();

    // * WiFiManager local initialization. Once its business is done, there is no need to keep it around
    WiFiManager wifiManager;

    // * Reset settings - uncomment for testing
    //   wifiManager.resetSettings();

    // * Set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
    wifiManager.setAPCallback(configModeCallback);

    // * Set timeout
    wifiManager.setConfigPortalTimeout(WIFI_TIMEOUT);

    // * Set save config callback
    wifiManager.setSaveConfigCallback(save_wifi_config_callback);

    // * Fetches SSID and pass and tries to connect
    // * Reset when no connection after 10 seconds
    if (!wifiManager.autoConnect()) {
        Serial.println(F("Failed to connect to WIFI and hit timeout"));
        // * Reset and try again
        ESP.reset();
        delay(WIFI_TIMEOUT);
    }

    // * Show wifi connected display screen
    int counter = 0;
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        display.clear();
        display.drawString(64, 10, "Connecting to WiFi");
        display.drawXbm(46, 30, 8, 8, counter % 3 == 0 ? activeSymbol : inactiveSymbol);
        display.drawXbm(60, 30, 8, 8, counter % 3 == 1 ? activeSymbol : inactiveSymbol);
        display.drawXbm(74, 30, 8, 8, counter % 3 == 2 ? activeSymbol : inactiveSymbol);
        display.display();

        counter++;
    }

    // * If you get here you have connected to the WiFi
    Serial.println(F("Connected to WIFI..."));

    // * Keep LED on
    ticker.detach();
    digitalWrite(LED_BUILTIN, LOW);

    // * Configure OTA
    setup_ota();

    // * Startup MDNS Service
    Serial.println(F("Starting MDNS responder service"));
    MDNS.begin(HOSTNAME);

    // * Setup overlays and ui
    setup_ui();

    // * Fetch data
    updateData(&display);

    // * Check every second
    ticker.attach(1, checkReadyForUpdate);
}


// **********************************
// * Loop                           *
// **********************************

void loop()
{
    // * Update last loop watchdog value
    last_loop = millis();

    // * If there are airplanes query often
    if (adsbClient.getNumberOfVisibleAircrafts() == 0) {
        currentUpdateInterval = UPDATE_INTERVAL_SECS_LONG;
    }
    else {
        currentUpdateInterval = UPDATE_INTERVAL_SECS_SHORT;
    }

    if (readyForUpdate && ui.getUiState()->frameState == FIXED) {
        updateData(&display);
    }

    int remainingTimeBudget = ui.update();

    if (remainingTimeBudget > 0) {
        // You can do some work here
        // Don't do stuff if you are below your
        // time budget.
        ArduinoOTA.handle();

        delay(remainingTimeBudget);
    }

}

// **********************************
// * Spotter Functions              *
// **********************************

void drawProgress(OLEDDisplay *display, int percentage, String label)
{
    display->clear();
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->setFont(ArialMT_Plain_10);
    display->drawString(64, 10, label);
    display->drawProgressBar(2, 28, 124, 10, percentage);
    display->display();
}

void drawOtaProgress(unsigned int progress, unsigned int total)
{
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setFont(ArialMT_Plain_10);
    display.drawString(64, 10, "OTA Update");
    display.drawProgressBar(2, 28, 124, 10, progress / (total / 100));
    display.display();
}

void updateData(OLEDDisplay *display)
{
    readyForUpdate = false;
    adsbClient.updateVisibleAircraft(QUERY_STRING);
    lastUpdate = millis();
}

void drawHeaderOverlay(OLEDDisplay *display, OLEDDisplayUiState* state)
{

    if (adsbClient.isAircraftVisible()) {
        display->setTextAlignment(TEXT_ALIGN_LEFT);
        display->setFont(ArialMT_Plain_10);
        display->drawString(0, 10, "Dst:");
        display->drawString(64, 10, "Alt:");
        display->drawString(0, 32, "Head:");
        display->setFont(ArialMT_Plain_16);
        display->drawString(0, 20, String(adsbClient.getDistance()) + "km");
        display->drawString(64, 20, adsbClient.getAltitude() + "ft");
        display->drawString(0, 42, String(adsbClient.getHeading()) + "°");

        drawHeading(display, 78, 52, adsbClient.getHeading());

    }

    int8_t quality = getWifiQuality();
    for (int8_t i = 0; i < 4; i++) {
        for (int8_t j = 0; j < 2 * (i + 1); j++) {
            if (quality > i * 25 || j == 0) {
                display->setPixel(120 + 2 * i, 63 - j);
            }
        }
    }

}

// converts the dBm to a range between 0 and 100%
int8_t getWifiQuality()
{
    int32_t dbm = WiFi.RSSI();
    if (dbm <= -100) {
        return 0;
    }
    else if(dbm >= -50) {
        return 100;
    }
    else {
        return 2 * (dbm + 100);
    }
}

void drawCurrentAirplane1(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y)
{
    if (adsbClient.isAircraftVisible()) {
        display->setTextAlignment(TEXT_ALIGN_LEFT);
        display->setFont(ArialMT_Plain_10);

        display->drawString(0 + x, 0 + y, "From: " + adsbClient.getFrom());
    }
}

void drawCurrentAirplane2(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y)
{
    if (adsbClient.isAircraftVisible()) {
        display->setTextAlignment(TEXT_ALIGN_LEFT);
        display->setFont(ArialMT_Plain_10);

        display->drawString(0 + x, 0 + y, "To: " + adsbClient.getTo());
    }
}

void drawCurrentAirplane3(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y)
{
    if (adsbClient.isAircraftVisible()) {
        display->setTextAlignment(TEXT_ALIGN_LEFT);
        display->setFont(ArialMT_Plain_10);

        display->drawString(0 + x, 0 + y, "Type: " + adsbClient.getAircraftType());
    }
}

void drawHeading(OLEDDisplay *display, int x, int y, double heading)
{
    int degrees[] = {0, 170, 190, 0};
    display->drawCircle(x, y, 10);
    int radius = 8;

    const float pi = 3.141;

    for (int i = 0; i < 3; i++) {
        int x1 = cos((-450 + (heading + degrees[i])) * pi / 180.0) * radius + x;
        int y1 = sin((-450 + (heading + degrees[i])) * pi / 180.0) * radius + y;
        int x2 = cos((-450 + (heading + degrees[i + 1])) * pi / 180.0) * radius + x;
        int y2 = sin((-450 + (heading + degrees[i + 1])) * pi / 180.0) * radius + y;
        display->drawLine(x1, y1, x2, y2);
    }
}

void checkReadyForUpdate()
{
    if (lastUpdate < millis() - currentUpdateInterval * 1000) {
        readyForUpdate = true;
    }
}
