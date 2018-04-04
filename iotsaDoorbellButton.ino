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

#define PIN_BUTTON 4	// GPIO4 is the pushbutton
#define PIN_LOCK 5		// GPIO5 is the keylock switch
#define PIN_NEOPIXEL 15  // pulled-down during boot, can be used for NeoPixel afterwards
#define IFDEBUGX if(0)

// Define this to handle https requests with WifiClientSecure, there seems to be an issue
// (in esp8266 2.4.0) with HttpClient complaining about the fingerprint.
#undef HTTPS_BUG_WORKAROUND
#ifdef HTTPS_BUG_WORKAROUND
#include <WiFiClientSecure.h>
#endif

ESP8266WebServer server(80);
IotsaApplication application(server, "Doorbell Button Server");

// Configure modules we need
IotsaWifiMod wifiMod(application);  // wifi is always needed
IotsaOtaMod otaMod(application);    // we want OTA for updating the software (will not work with esp-201)
IotsaLedMod ledMod(application, PIN_NEOPIXEL);

IotsaUserMod myUserAuthenticator(application, "owner");  // Our username/password authenticator module
IotsaCapabilityMod myTokenAuthenticator(application, myUserAuthenticator); // Our token authenticator
IotsaLoggerMod myLogger(application, &myTokenAuthenticator);
// NOTE: the next line is temporary, for development, it allows getting at tokens.
IotsaFilesBackupMod filesBackupMod(application, &myTokenAuthenticator);  // we want backup to clone server

static void decodePercentEscape(String &src, String *dst); // Forward declaration

//
// Button parameters and implementation
//

typedef struct _Button {
  int pin;
  String url;
  String fingerprint;
  String token;
  bool sendOnPress;
  bool sendOnRelease;
  int debounceState;
  int debounceTime;
  bool buttonState;
} Button;

Button buttons[] = {
  { PIN_BUTTON, "", "", "", true, false, 0, 0, false},
  { PIN_LOCK, "", "", "", true, true, 0, 0, false}
};

const int nButton = sizeof(buttons) / sizeof(buttons[0]);

#define DEBOUNCE_DELAY 50 // 50 ms debouncing
#define BUTTON_BEEP_DUR 10  // 10ms beep for button press

class IotsaButtonMod : IotsaApiMod {
public:
  IotsaButtonMod(IotsaApplication &_app, IotsaAuthMod *_auth=NULL) : IotsaApiMod(_app, _auth) {}
  void setup();
  void serverSetup();
  void loop();
  String info();
protected:
  bool getHandler(const char *path, JsonObject& reply);
  bool putHandler(const char *path, const JsonVariant& request, JsonObject& reply);
  void configLoad();
  void configSave();
  void handler();
};

void IotsaButtonMod::configLoad() {
  IotsaConfigFileLoad cf("/config/buttons.cfg");
  for (int i=0; i<nButton; i++) {
    String name = "button" + String(i+1) + "url";
    cf.get(name, buttons[i].url, "");
    name = "button" + String(i+1) + "fingerprint";
    cf.get(name, buttons[i].fingerprint, "");
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

void IotsaButtonMod::configSave() {
  IotsaConfigFileSave cf("/config/buttons.cfg");
  for (int i=0; i<nButton; i++) {
    String name = "button" + String(i+1) + "url";
    cf.put(name, buttons[i].url);
    name = "button" + String(i+1) + "fingerprint";
    cf.put(name, buttons[i].fingerprint);
    name = "button" + String(i+1) + "token";
    cf.put(name, buttons[i].token);
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

void IotsaButtonMod::setup() {
  for (int i=0; i<nButton; i++) {
    pinMode(buttons[i].pin, INPUT_PULLUP);
  }
  configLoad();
}

void IotsaButtonMod::handler() {
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
      wtdName = "button" + String(j+1) + "fingerprint";
      if (server.argName(i) == wtdName) {
        String arg = server.arg(i);
        decodePercentEscape(arg, &buttons[j].fingerprint);
        IFDEBUG IotsaSerial.print(wtdName);
        IFDEBUG IotsaSerial.print("=");
        IFDEBUG IotsaSerial.println(buttons[j].fingerprint);
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
  if (any) configSave();
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
      message += "fingerprint\" : \"";
      message += buttons[i].fingerprint;
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

      message += "Fingerprint <i>(https only)</i>: <input name='button" + String(i+1) + "fingerprint' value='";
      message += buttons[i].fingerprint;
      message += "'><br>\n";

      message += "Bearer token <i>(optional)</i>: <input name='button" + String(i+1) + "token' value='";
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

String IotsaButtonMod::info() {
  return "<p>See <a href='/buttons'>/buttons</a> to program URLs for button presses. REST API at <a href='/api/buttons'>/api/buttons</a></p>";
}

#ifdef HTTPS_BUG_WORKAROUND
bool sendRequestHTTPSWorkaround(String urlStr, String token, String fingerprint="") {
  bool rv = true;
  int index = urlStr.indexOf(':');
  if(index < 0) {
    IotsaSerial.println("No protocol in url");
    return false;
  }

  String _protocol = urlStr.substring(0, index);
  if (_protocol != "https") {
    IotsaSerial.println("Not https");
    return false;
  }
  urlStr.remove(0, (index + 3)); // remove http:// or https://

  index = urlStr.indexOf('/');
  String hostTmp = urlStr.substring(0, index);
  String host;
  int port;
  urlStr.remove(0, index); // remove host part

  // get Authorization
  index = hostTmp.indexOf('@');
  if(index >= 0) {
    IotsaSerial.println("Username in URL not supported");
    return false;
  }

  // get port
  index = hostTmp.indexOf(':');
  if(index >= 0) {
      host = hostTmp.substring(0, index); // hostname
      hostTmp.remove(0, (index + 1)); // remove hostname + :
      port = hostTmp.toInt(); // get port
  } else {
      host = hostTmp;
  }

  WiFiClientSecure client;

  client.connect(host, port);
  if (fingerprint != "") {
    if (!client.verify(fingerprint.c_str(), host.c_str())) {
      IotsaSerial.println("https fingerprint does not match");
      ledMod.set(0x200000, 250, 0, 1); // quarter second red flash for failure
      return false;
    }
  } else {
    IotsaSerial.println("Warning: no fingerprint");
  }
  String req = "GET ";
  req += urlStr;
  req += " HTTP/1.1\r\nUser-Agent: iotsa\r\nHost: ";
  req += host;
  req += "\r\n";
  if (token) {
    req += "Authorization: Bearer ";
    req += token;
    req += "\r\n";
  }
  req += "Connection: close\r\n\r\n";

  String rep = client.readStringUntil('\n');
  if (rep.startsWith("HTTP/1.1 2")) {
    ledMod.set(0x002000, 250, 0, 1); // quarter second green flash for success
    IFDEBUG IotsaSerial.print(" OK GET ");
    IFDEBUG IotsaSerial.println(urlStr);
    rv = true;
  } else {
    ledMod.set(0x200000, 250, 0, 1); // quarter second red flash for failure
    IFDEBUG IotsaSerial.print(" FAIL GET ");
    IFDEBUG IotsaSerial.println(urlStr);
    IFDEBUG IotsaSerial.println(rep);
    rv = false;  
  }
  // Read all headers, at least
  while (client.connected()) {
    rep = client.readStringUntil('\n');
    if (rep == "" || rep == "\r") break;
  }
  return rv;
}
#endif

bool sendRequest(String urlStr, String token, String fingerprint="") {
  bool rv = true;
  HTTPClient http;
  ledMod.set(0x000020, 250, 250, 10); // Flash 2 times per second blue, while connecting
  delay(1);
  if (urlStr.startsWith("https:")) {
#ifdef HTTPS_BUG_WORKAROUND
    return sendRequestHTTPSWorkaround(urlStr, token, fingerprint);
#else
    http.begin(urlStr, fingerprint);
#endif
  } else {
    http.begin(urlStr);  
  }
  if (token != "") {
    http.addHeader("Authorization", "Bearer " + token);
  }
  int code = http.GET();
  if (code >= 200 && code <= 299) {
    ledMod.set(0x002000, 250, 0, 1); // quarter second green flash for success
    IFDEBUG IotsaSerial.print(code);
    IFDEBUG IotsaSerial.print(" OK GET ");
    IFDEBUG IotsaSerial.println(urlStr);
  } else {
    ledMod.set(0x200000, 250, 0, 1); // quarter second red flash for failure
    IFDEBUG IotsaSerial.print(code);
    IFDEBUG IotsaSerial.print(" FAIL GET ");
    IFDEBUG IotsaSerial.println(urlStr);
  }
  http.end();
  return rv;
}

void IotsaButtonMod::loop() {
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
        if (doSend && buttons[i].url != "") {
          sendRequest(buttons[i].url, buttons[i].token, buttons[i].fingerprint);
        }
      }
    }
  }
}

bool IotsaButtonMod::getHandler(const char *path, JsonObject& reply) {
  JsonArray& rv = reply.createNestedArray("buttons");
  for (Button *b=buttons; b<buttons+nButton; b++) {
    JsonObject& bRv = rv.createNestedObject();
    bRv["url"] = b->url;
    bRv["fingerprint"] = b->fingerprint;
    bRv["state"] = b->buttonState;
    bRv["hasToken"] = b->token != "";
    bRv["onPress"] = b->sendOnPress;
    bRv["onRelease"] = b->sendOnRelease;
  }
  return true;
}

bool IotsaButtonMod::putHandler(const char *path, const JsonVariant& request, JsonObject& reply) {
  Button *b;
  if (strcmp(path, "/api/buttons/0") == 0) {
    b = &buttons[0];
  } else if (strcmp(path, "/api/buttons/1") == 0) {
    b = &buttons[1];
  } else {
    return false;
  }
  if (!request.is<JsonObject>()) return false;
  JsonObject& reqObj = request.as<JsonObject>();
  bool any = false;
  if (reqObj.containsKey("url")) {
    any = true;
    b->url = reqObj.get<String>("url");
  }
  if (reqObj.containsKey("fingerprint")) {
    any = true;
    b->fingerprint = reqObj.get<String>("fingerprint");
  }
  if (reqObj.containsKey("token")) {
    any = true;
    b->token = reqObj.get<String>("token");
  }
  if (reqObj.containsKey("onPress")) {
    any = true;
    b->sendOnPress = reqObj.get<bool>("onPress");
  }
  if (reqObj.containsKey("onRelease")) {
    any = true;
    b->sendOnRelease = reqObj.get<bool>("onRelease");
  }
  if (any) configSave();
  return any;
}

void IotsaButtonMod::serverSetup() {
  server.on("/buttons", std::bind(&IotsaButtonMod::handler, this));
  api.setup("/api/buttons", true, false);
  api.setup("/api/buttons/0", false, true);
  api.setup("/api/buttons/1", false, true);
  name = "buttons";
}

IotsaButtonMod buttonMod(application, &myTokenAuthenticator);

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
  ESP.wdtEnable(WDTO_120MS);
}
 
void loop(void) {
  application.loop();
} 
