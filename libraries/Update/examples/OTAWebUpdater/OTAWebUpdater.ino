#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <Ticker.h>
#include "html.h"

#define SSID_FORMAT "ESP32-%06lX"  // 12 chars total
//#define PASSWORD "test123456"    // generate if remarked

// Set the username and password for firmware upload
const char *authUser = "........";
const char *authPass = "........";

WebServer server(80);
Ticker tkSecond;
uint8_t otaDone = 0;

const char *csrfHeaders[2] = {"Origin", "Host"};
static bool authenticated = false;

const char *alphanum = "0123456789!@#$%^&*abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
String generatePass(uint8_t str_len) {
  String buff;
  for (int i = 0; i < str_len; i++) {
    buff += alphanum[random(strlen(alphanum) - 1)];
  }
  return buff;
}

void apMode() {
  char ssid[13];
  char passwd[11];
  long unsigned int espmac = ESP.getEfuseMac() >> 24;
  snprintf(ssid, 13, SSID_FORMAT, espmac);
#ifdef PASSWORD
  snprintf(passwd, 11, PASSWORD);
#else
  snprintf(passwd, 11, generatePass(10).c_str());
#endif
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, passwd);  // Set up the SoftAP
  MDNS.begin("esp32");
  Serial.printf("AP: %s, PASS: %s\n", ssid, passwd);
}

void handleUpdateEnd() {
  if (!authenticated) {
    return server.requestAuthentication();
  }
  server.sendHeader("Connection", "close");
  if (Update.hasError()) {
    server.send(502, "text/plain", Update.errorString());
  } else {
    server.sendHeader("Refresh", "10");
    server.sendHeader("Location", "/");
    server.send(307);
    delay(500);
    ESP.restart();
  }
}

void handleUpdate() {
  size_t fsize = UPDATE_SIZE_UNKNOWN;
  if (server.hasArg("size")) {
    fsize = server.arg("size").toInt();
  }
  HTTPUpload &upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    authenticated = server.authenticate(authUser, authPass);
    if (!authenticated) {
      Serial.println("Authentication fail!");
      otaDone = 0;
      return;
    }
    String origin = server.header(String(csrfHeaders[0]));
    String host = server.header(String(csrfHeaders[1]));
    String expectedOrigin = String("http://") + host;
    if (origin != expectedOrigin) {
      Serial.printf("Wrong origin received! Expected: %s, Received: %s\n", expectedOrigin.c_str(), origin.c_str());
      authenticated = false;
      otaDone = 0;
      return;
    }

    Serial.printf("Receiving Update: %s, Size: %d\n", upload.filename.c_str(), fsize);
    if (!Update.begin(fsize)) {
      otaDone = 0;
      Update.printError(Serial);
    }
  } else if (authenticated && upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
    } else {
      otaDone = 100 * Update.progress() / Update.size();
    }
  } else if (authenticated && upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      Serial.printf("Update Success: %u bytes\nRebooting...\n", upload.totalSize);
    } else {
      Serial.printf("%s\n", Update.errorString());
      otaDone = 0;
    }
  }
}

void webServerInit() {
  server.collectHeaders(csrfHeaders, 2);
  server.on(
    "/update", HTTP_POST,
    []() {
      handleUpdateEnd();
    },
    []() {
      handleUpdate();
    }
  );
  server.on("/favicon.ico", HTTP_GET, []() {
    server.sendHeader("Content-Encoding", "gzip");
    server.send_P(200, "image/x-icon", favicon_ico_gz, favicon_ico_gz_len);
  });
  server.onNotFound([]() {
    if (!server.authenticate(authUser, authPass)) {
      return server.requestAuthentication();
    }
    server.send(200, "text/html", indexHtml);
  });
  server.begin();
  Serial.printf("Web Server ready at http://esp32.local or http://%s\n", WiFi.softAPIP().toString().c_str());
}

void everySecond() {
  if (otaDone > 1) {
    Serial.printf("ota: %d%%\n", otaDone);
  }
}

void setup() {
  Serial.begin(115200);
  apMode();
  webServerInit();
  tkSecond.attach(1, everySecond);
}

void loop() {
  delay(150);
  server.handleClient();
}
