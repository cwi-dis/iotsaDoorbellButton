//
// Server for a 4-line display with a buzzer and 2 buttons.
// Messages to the display (and buzzes) can be controlled with a somewhat REST-like interface.
// The buttons can be polled, or can be setup to send a GET request to a preprogrammed URL.
// 
// Support for buttons, LCD and buzzer can be disabled selectively.
//
// Hardware schematics and PCB stripboard layout can be found in the "extras" folder, in Fritzing format,
// for an ESP201-based device.
//
// (c) 2016, Jack Jansen, Centrum Wiskunde & Informatica.
// License TBD.
//

#include <ESP8266HTTPClient.h>
#include "iotsa.h"
#include "iotsaWifi.h"
#include "iotsaOta.h"
#include "iotsaFilesBackup.h"
#include "iotsaSimple.h"
#include "iotsaConfigFile.h"
#include "iotsaUser.h"
#include "iotsaLed.h"
#include "iotsaLogger.h"
#include "iotsaCapabilities.h"
#include "iotsaButton.h"
#include <functional>

#define PIN_BUTTON 4	// GPIO4 is the pushbutton
#define PIN_LOCK 5		// GPIO5 is the keylock switch
#define PIN_NEOPIXEL 15  // pulled-down during boot, can be used for NeoPixel afterwards

IotsaApplication application("Doorbell Button Server");

// Configure modules we need
IotsaWifiMod wifiMod(application);  // wifi is always needed
IotsaOtaMod otaMod(application);    // we want OTA for updating the software (will not work with esp-201)
IotsaLedMod ledMod(application, PIN_NEOPIXEL);

IotsaUserMod myUserAuthenticator(application, "owner");  // Our username/password authenticator module
IotsaCapabilityMod myTokenAuthenticator(application, myUserAuthenticator); // Our token authenticator
#ifdef IOTSA_WITH_HTTP
IotsaLoggerMod myLogger(application, &myTokenAuthenticator);
#endif


Button buttons[] = {
  Button(PIN_BUTTON, true, false),
  Button(PIN_LOCK, true, true)
};
const int nButton = sizeof(buttons) / sizeof(buttons[0]);

static void buttonOk() {
  ledMod.set(0x002000, 250, 0, 1);
}

static void buttonNotOk() {
  ledMod.set(0x200000, 250, 0, 1);
}

IotsaButtonMod buttonMod(application, buttons, nButton, &myTokenAuthenticator, buttonOk, buttonNotOk);

//
// Boilerplate for iotsa server, with hooks to our code added.
//
void setup(void) {
  application.setup();
  application.serverSetup();
  ESP.wdtEnable(WDTO_120MS);
}
 
void loop(void) {
  application.loop();
} 
