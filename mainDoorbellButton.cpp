//
// Doorbell button server: a pushbutton and a keylock switch. Each can be polled
// over REST, or set up (with the right "buttons") to fire a GET request to a
// preprogrammed URL -- typically an iotsaDoorbellRinger's /ring?alarm=20.
//
// Hardware schematics and PCB stripboard layout are in the "extras" folder, in
// Fritzing format, for an ESP-201-based device.
//
// (c) 2016, Jack Jansen, Centrum Wiskunde & Informatica.
// License TBD.
//

#include "iotsa.h"
#include "iotsaUser.h"
#include "iotsaCapabilities.h"
#include "iotsaButton.h"
#include <functional>

#define PIN_BUTTON 4	// GPIO4 is the pushbutton
#define PIN_LOCK 5		// GPIO5 is the keylock switch

IotsaApplication application("Doorbell Button Server");

// Configure modules we need

IotsaUserMod myUserAuthenticator(application, "owner");  // Our username/password authenticator module
IotsaCapabilityMod myTokenAuthenticator(application, myUserAuthenticator); // Our token authenticator

Button buttons[] = {
  Button(PIN_BUTTON, true, false),
  Button(PIN_LOCK, true, true)
};
const int nButton = sizeof(buttons) / sizeof(buttons[0]);

// Transient status-LED pulses (cwi-dis/iotsa#176/#256) -- decay back to the
// normal status display on their own, no restore logic needed.
static void buttonOk() {
  iotsaStatus.setStatusPulse(0x002000, 0, 0, 250, "button ok");
}

static void buttonNotOk() {
  iotsaStatus.setStatusPulse(0x200000, 0, 0, 250, "button not ok");
}

IotsaButtonMod buttonMod(application, buttons, nButton, buttonOk, buttonNotOk);

//
// Boilerplate for iotsa server, with hooks to our code added.
//
void setup(void) {
  application.setAuth(&myTokenAuthenticator);  // every module, the standard ones included, uses this
  application.setup();
  application.lateSetup();
}

void loop(void) {
  application.loop();
}
