#include <Arduino.h>

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESPAsyncTCP.h>

#include <ESPAsyncWebServer.h>
AsyncWebServer webserver(80);

#include <sstream>
using namespace std;

// wifi credentials
#define NETWORK_SSID  "The LAN before time"
#define NETWORK_PASS  "password"
#define HOSTNAME      "canary"

#define DEBUG true // <-- uncomment to get serial logs
#include "log.h"

#include "SerialCom.h"
particleSensorState_t sensor;

bool led(void) { // reads LED state
  return digitalRead(LED_BUILTIN) == HIGH;
}

bool led(bool state) { // writes LED state
  digitalWrite(LED_BUILTIN, state ? HIGH : LOW);
  return led();
}

int uptime() { // uptime in seconds
  return int(millis() / 1000);
}
bool disconnected() { // did we lose wifi?
  return WiFi.status() != WL_CONNECTED;
}

String wifiClientName(){
  String clientName = HOSTNAME;
  clientName.concat("-");
  clientName.concat(ESP.getChipId());
  return clientName;
}


// Prometheus device metrics
string deviceMetrics() {
  stringstream ret;

  // report uptime (time since last reboot) in s
  ret << "# HELP uptime Uptime in seconds.\n";
  ret << "# TYPE uptime counter\n";
  ret << "uptime " << uptime() << "\n";

  return ret.str();
}

// Prometheus sensor metrics
string sensorMetrics() {
  stringstream ret;

  // the only meric exposed: pm 2.5
  ret << "# HELP pm2_5 Fine particulates 2.5μm (μg/m³).\n";
  ret << "# TYPE pm2_5 gauge\n";
  ret << "pm2_5 " << sensor.avgPM25 << "\n";

  return ret.str();
}


void setup() {
  // ESP.wdtEnable(10000); // init watchdog timer

  Serial.begin(115200);

  // init the sensor
  SerialCom::setup();

  delay(100); // let serial console "settle"
  log("\n");

  WiFi.setHostname(wifiClientName().c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(NETWORK_SSID, NETWORK_PASS);
  while (disconnected()) {
    // print 1 dot every half second while we're trying to connect
    delay(500);
    log(".");
  }

  logf("Connected to %s\n", NETWORK_SSID);
  logf("IP address: %s\n", WiFi.localIP().toString().c_str());

  if (!MDNS.begin(HOSTNAME))  log("Error setting up MDNS responder!");
  logf("MDNS responder started at http://%s.local\n", HOSTNAME);

  webserver.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    led(true);
    request->send(200, "text/plain", "Hello, world");
    led(false);
  });

  // responds with all data in a format prometheus expects
  webserver.on("/metrics", [](AsyncWebServerRequest *request) {
    led(true);
    stringstream message;

    message << deviceMetrics();
    message << sensorMetrics();

    request->send(200, "text/plain", message.str().c_str());
    led(false);
  });

  webserver.onNotFound([](AsyncWebServerRequest *request){
    request->send(404, "text/plain", "Not found");
  });

  webserver.begin();
}

void loop() {
  if (disconnected()) ESP.restart();
  MDNS.update(); // re-advertise the mDNS name
  SerialCom::handleUart(sensor);
}