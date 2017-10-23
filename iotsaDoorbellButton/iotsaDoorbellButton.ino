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

#include <ESP.h>
#include <FS.h>
#include <ESP8266HTTPClient.h>
#include "iotsa.h"
#include "iotsaWifi.h"
#include "iotsaOta.h"
#include "iotsaFilesBackup.h"
#include "iotsaSimple.h"
#include "iotsaConfigFile.h"

#define PIN_BUTTON 4	// GPIO4 is the pushbutton
#define PIN_LOCK 5		// GPIO5 is the keylock switch
#undef PIN_ALARM
#define IFDEBUGX if(0)

ESP8266WebServer server(80);
IotsaApplication application(server, "Doorbell Server");

// Configure modules we need
IotsaWifiMod wifiMod(application);  // wifi is always needed
IotsaOtaMod otaMod(application);    // we want OTA for updating the software (will not work with esp-201)
IotsaFilesBackupMod filesBackupMod(application);  // we want backup to clone server

static void decodePercentEscape(String &src, String *dst); // Forward declaration

//
// Buzzer configuration and implementation
//
#ifdef PIN_ALARM
unsigned long alarmEndTime;
#endif

//
// LCD configuration and implementation
//
#ifdef PIN_ALARM
void alarmSetup() {
  pinMode(PIN_ALARM, INPUT); // Trick: we configure to input so we make the pin go Hi-Z.
}

void alarmHandler() {
  String msg;

  for (uint8_t i=0; i<server.args(); i++){
    if (server.argName(i) == "alarm") {
      const char *arg = server.arg(i).c_str();
      if (arg && *arg) {
        int dur = atoi(server.arg(i).c_str());
        if (dur) {
          alarmEndTime = millis() + dur*100;
          IotsaSerial.println("alarm on");
          pinMode(PIN_ALARM, OUTPUT);
          digitalWrite(PIN_ALARM, LOW);
        } else {
          alarmEndTime = 0;
        }
      }
    }
  String message = "<html><head><title>Alarm Server</title></head><body><h1>Alarm Server</h1>";
  message += "<form method='get'>";
  message += "Alarm: <input name='alarm' value=''> (times 0.1 second)<br>\n";
  message += "<input type='submit'></form></body></html>";
  server.send(200, "text/html", message);
  
}

String alarmInfo() {
  return "<p>See <a href='/alarm'>/alarm</a> to use the buzzer.";
}

void alarmLoop() {
  if (alarmEndTime && millis() > alarmEndTime) {
    alarmEndTime = 0;
    pinMode(PIN_ALARM, INPUT);
  }
}

IotsaSimpleMod alarmMod(application, "/alarm", alarmHandler, alarmInfo);

#endif // PIN_ALARM

//
// Button parameters and implementation
//
#ifdef PIN_BUTTON

typedef struct _Button {
  int pin;
  String url;
  String token;
  bool sendOnPress;
  bool sendOnRelease;
  int debounceState;
  int debounceTime;
  bool buttonState;
} Button;

Button buttons[] = {
  { PIN_BUTTON, "", "", true, false, 0, 0, false},
  { PIN_LOCK, "", "", true, true, 0, 0, false}
};

const int nButton = sizeof(buttons) / sizeof(buttons[0]);

#define DEBOUNCE_DELAY 50 // 50 ms debouncing
#define BUTTON_BEEP_DUR 10  // 10ms beep for button press

void buttonConfigLoad() {
  IotsaConfigFileLoad cf("/config/buttons.cfg");
  for (int i=0; i<nButton; i++) {
    String name = "button" + String(i+1) + "url";
    cf.get(name, buttons[i].url, "");
    name = "button" + String(i+1) + "token";
    cf.get(name, buttons[i].token, "");
    name = "button" + String(i+1) + "on";
    String sendOn;
    cf.get(name, sendOn, "");
    if (sendOn == "press") {
      buttons[i].sendOnPress = true;
      buttons[i].sendOnRelease = false;
    } else
    if (sendOn == "release") {
      buttons[i].sendOnPress = false;
      buttons[i].sendOnRelease = true;
    } else
    if (sendOn == "change") {
      buttons[i].sendOnPress = true;
      buttons[i].sendOnRelease = true;
    } else {
      buttons[i].sendOnPress = false;
      buttons[i].sendOnRelease = false;
    }
  }
}

void buttonConfigSave() {
  IotsaConfigFileSave cf("/config/buttons.cfg");
  for (int i=0; i<nButton; i++) {
    String name = "button" + String(i+1) + "url";
    cf.put(name, buttons[i].url);
    name = "button" + String(i+1) + "token";
    cf.put(name, buttons[i].url);
    name = "button" + String(i+1) + "on";
    if (buttons[i].sendOnPress) {
      if (buttons[i].sendOnRelease) {
        cf.put(name, "change");
      } else {
        cf.put(name, "press");
      }
    } else {
      if (buttons[i].sendOnRelease) {
        cf.put(name, "release");
      } else {
        cf.put(name, "none");
      }
    }
  }
}

void buttonSetup() {
  for (int i=0; i<nButton; i++) {
    pinMode(buttons[i].pin, INPUT_PULLUP);
  }
  buttonConfigLoad();
}

void buttonHandler() {
  bool any = false;
  bool isJSON = false;

  for (uint8_t i=0; i<server.args(); i++){
    for (int j=0; j<nButton; j++) {
      String wtdName = "button" + String(j+1) + "url";
      if (server.argName(i) == wtdName) {
        String arg = server.arg(i);
        decodePercentEscape(arg, &buttons[j].url);
        IFDEBUG IotsaSerial.print(wtdName);
        IFDEBUG IotsaSerial.print("=");
        IFDEBUG IotsaSerial.println(buttons[j].url);
        any = true;
      }

      wtdName = "button" + String(j+1) + "token";
      if (server.argName(i) == wtdName) {
        String arg = server.arg(i);
        decodePercentEscape(arg, &buttons[j].token);
        IFDEBUG IotsaSerial.print(wtdName);
        IFDEBUG IotsaSerial.print("=");
        IFDEBUG IotsaSerial.println(buttons[j].token);
        any = true;
      }

      wtdName = "button" + String(j+1) + "on";
      if (server.argName(i) == wtdName) {
        String arg = server.arg(i);
        if (arg == "press") {
          buttons[j].sendOnPress = true;
          buttons[j].sendOnRelease = false;
        } else if (arg == "release") {
          buttons[j].sendOnPress = false;
          buttons[j].sendOnRelease = true;
        } else if (arg == "change") {
          buttons[j].sendOnPress = true;
          buttons[j].sendOnRelease = true;
        } else {
          buttons[j].sendOnPress = false;
          buttons[j].sendOnRelease = false;
        }
        any = true;
      }
    }
    if (server.argName(i) == "format" && server.arg(i) == "json") {
      isJSON = true;
    }
  }
  if (any) buttonConfigSave();
  if (isJSON) {
    String message = "{\"buttons\" : [";
    for (int i=0; i<nButton; i++) {
      if (i > 0) message += ", ";
      if (buttons[i].buttonState) {
        message += "true";
      } else {
        message += "false";
      }
    }
    message += "]";
    for (int i=0; i<nButton; i++) {
      message += ", \"button";
      message += String(i+1);
      message += "url\" : \"";
      message += buttons[i].url;
      message += "\"";

      message += ", \"button";
      message += String(i+1);
      message += "token\" : \"";
      message += buttons[i].token;
      message += "\"";
      
      message += ", \"button";
      message += String(i+1);
      if (buttons[i].sendOnPress) {
        if (buttons[i].sendOnRelease) {
          message += "on\" : \"change\"";
        } else {
          message += "on\" : \"press\"";
        }
      } else {
        if (buttons[i].sendOnRelease) {
          message += "on\" : \"release\"";
        } else {
          message += "on\" : \"none\"";
        } 
      }
    }
    message += "}\n";
    server.send(200, "application/json", message);
  } else {
    String message = "<html><head><title>Buttons</title></head><body><h1>Buttons</h1>";
    for (int i=0; i<nButton; i++) {
      message += "<p>Button " + String(i+1) + " is currently ";
      if (buttons[i].buttonState) message += "on"; else message += "off";
      message += ".</p>";
    }
    message += "<form method='get'>";
    for (int i=0; i<nButton; i++) {
      message += "<br><em>Button " + String(i+1) + "</em><br>\n";
      message += "Activation URL: <input name='button" + String(i+1) + "url' value='";
      message += buttons[i].url;
      message += "'><br>\n";
      
      message += "Activation URL bearer token: <input name='button" + String(i+1) + "token' value='";
      message += buttons[i].token;
      message += "'><br>\n";
      
      message += "Call URL on: ";
      message += "<input name='button" + String(i+1) + "on' type='radio' value='press'";
      if (buttons[i].sendOnPress && !buttons[i].sendOnRelease) message += " checked";
      message += "> Press ";
      
      message += "<input name='button" + String(i+1) + "on' type='radio' value='release'";
      if (!buttons[i].sendOnPress && buttons[i].sendOnRelease) message += " checked";
      message += "> Release ";
      
      message += "<input name='button" + String(i+1) + "on' type='radio' value='change'";
      if (buttons[i].sendOnPress && buttons[i].sendOnRelease) message += " checked";
      message += "> Press and release ";
      
      message += "<input name='button" + String(i+1) + "on' type='radio' value='none'";
      if (!buttons[i].sendOnPress && !buttons[i].sendOnRelease) message += " checked";
      message += "> Never<br>\n";
    }
    message += "<input type='submit'></form></body></html>";
    server.send(200, "text/html", message);
  }
}

String buttonInfo() {
  return "<p>See <a href='/buttons'>/buttons</a> to program URLs for button presses.</a>";
}

bool sendRequest(String urlStr, String token) {
  bool rv = true;
  HTTPClient http;

  http.begin(urlStr);
  if (token != "") {
    http.addHeader("Authorization", "Bearer " + token);
  }
  int code = http.GET();
  if (code > 0) {
    IFDEBUG IotsaSerial.print("OK GET ");
    IFDEBUG IotsaSerial.println(urlStr);
 #ifdef PIN_ALARM
    alarmEndTime = millis() + BUTTON_BEEP_DUR;
    pinMode(PIN_ALARM, OUTPUT);
    digitalWrite(PIN_ALARM, LOW);
#endif // PIN_ALARM
  } else {
    IFDEBUG IotsaSerial.print("FAIL GET ");
    IFDEBUG IotsaSerial.println(urlStr);
  }
  http.end();
  return rv;
}

void buttonLoop() {
  for (int i=0; i<nButton; i++) {
    int state = digitalRead(buttons[i].pin);
    if (state != buttons[i].debounceState) {
      buttons[i].debounceTime = millis();
    }
    buttons[i].debounceState = state;
    if (millis() > buttons[i].debounceTime + DEBOUNCE_DELAY) {
      int newButtonState = (state == LOW);
      if (newButtonState != buttons[i].buttonState) {
        buttons[i].buttonState = newButtonState;
        bool doSend = (buttons[i].buttonState && buttons[i].sendOnPress) || (!buttons[i].buttonState && buttons[i].sendOnRelease);
        if (doSend && buttons[i].url != "") sendRequest(buttons[i].url, buttons[i].token);
      }
    }
  }
}

IotsaSimpleMod buttonMod(application, "/buttons", buttonHandler, buttonInfo);

#endif // PIN_BUTTON

//
// Decode percent-escaped string src.
// If dst is NULL the result is sent to the LCD.
// 
static void decodePercentEscape(String &src, String *dst) {
    const char *arg = src.c_str();
    if (dst) *dst = String();
    while (*arg) {
      char newch;
      if (*arg == '+') newch = ' ';
      else if (*arg == '%') {
        arg++;
        if (*arg >= '0' && *arg <= '9') newch = (*arg-'0') << 4;
        if (*arg >= 'a' && *arg <= 'f') newch = (*arg-'a'+10) << 4;
        if (*arg >= 'A' && *arg <= 'F') newch = (*arg-'A'+10) << 4;
        arg++;
        if (*arg == 0) break;
        if (*arg >= '0' && *arg <= '9') newch |= (*arg-'0');
        if (*arg >= 'a' && *arg <= 'f') newch |= (*arg-'a'+10);
        if (*arg >= 'A' && *arg <= 'F') newch |= (*arg-'A'+10);
      } else {
        newch = *arg;
      }
      if (dst) {
        *dst += newch;
      } else {
#ifdef WITH_LCD
        lcd.print(newch);
        x++;
        if (x >= LCD_WIDTH) {
          x = 0;
          y++;
          if (y >= LCD_HEIGHT) y = 0;
          lcd.setCursor(x, y);
        }
#endif
      }
      arg++;
    }
}

//
// Boilerplate for iotsa server, with hooks to our code added.
//
void setup(void) {
  application.setup();
  application.serverSetup();
#ifdef PIN_BUTTON
  buttonSetup();
#endif
  ESP.wdtEnable(WDTO_120MS);
}
 
void loop(void) {
  application.loop();
#ifdef PIN_BUTTON
  buttonLoop();
#endif
} 