// WebOTA.cpp — Web handler implementations for OTA firmware and backup/restore.
//
// Covers all over-the-air update and data migration endpoints:
//   - Queuing a firmware download from GitHub (handleDownloadFirmware)
//   - Backup restore from uploaded file (handleRestore, handleRestoreUpload)
//   - Firmware binary update (handleUpdateFirmware, handleUpdateFirmwareUpload)
//   - Shade config migration (handleUpdateShadeConfig, handleUpdateShadeConfigUpload)
//   - Application / filesystem update (handleUpdateApplication, handleUpdateApplicationUpload)

#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <Update.h>
#include <esp_task_wdt.h>
#include "ConfigSettings.h"
#include "ConfigFile.h"
#include "Utils.h"
#include "SomfyController.h"
#include "WResp.h"
#include "Web.h"
#include "GitOTA.h"
#include "MQTT.h"

extern ConfigSettings settings;
extern SomfyShadeController somfy;
extern Web webServer;
extern GitUpdater git;
extern rebootDelay_t rebootDelay;
extern MQTTClass mqtt;

#define WEB_MAX_RESPONSE 4096
extern char g_content[WEB_MAX_RESPONSE];
extern const char _encoding_text[];
extern const char _encoding_json[];

void Web::handleDownloadFirmware(WebServer &server) {
  GitRepo repo;
  GitRelease *rel = nullptr;
  int8_t err = repo.getReleases();
  Serial.println("downloadFirmware called...");
  if(err == 0) {
    if(server.hasArg("ver")) {
      if(strcmp(server.arg("ver").c_str(), "latest") == 0) rel = &repo.releases[0];
      else if(strcmp(server.arg("ver").c_str(), "main") == 0) {
        rel = &repo.releases[GIT_MAX_RELEASES];
      }
      else {
        for(uint8_t i = 0; i < GIT_MAX_RELEASES; i++) {
          if(repo.releases[i].id == 0) continue;
          if(strcmp(repo.releases[i].name, server.arg("ver").c_str()) == 0) {
            rel = &repo.releases[i];  
          }
        }
      }
      if(rel) {
        JsonResponse resp;
        resp.beginResponse(&server, g_content, sizeof(g_content));
        resp.beginObject();
        rel->toJSON(resp);
        resp.endObject();
        resp.endResponse();
        strcpy(git.targetRelease, rel->name);
        git.status = GIT_AWAITING_UPDATE;
      }
      else
        server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Release not found in repo.\"}"));
    }
    else
      server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Release version not supplied.\"}"));
  }
  else {
      server.send(err, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Error communicating with Github.\"}"));
  }
}

void Web::handleRestore(WebServer &server) {
  server.sendHeader("Connection", "close");
  if(webServer.uploadSuccess) {
    server.send(200, _encoding_json, "{\"status\":\"Success\",\"desc\":\"Restoring Shade settings\"}");
    restore_options_t opts;
    if(server.hasArg("data")) {
      Serial.println(server.arg("data"));
      StaticJsonDocument<256> doc;
      DeserializationError err = deserializeJson(doc, server.arg("data"));
      if (err) {
        webServer.handleDeserializationError(server, err);
        return;
      }
      else {
        JsonObject obj = doc.as<JsonObject>();
        opts.fromJSON(obj);
      }
    }
    else {
      Serial.println("No restore options sent.  Using defaults...");
      opts.shades = true;
    }
    ShadeConfigFile::restore(&somfy, "/shades.tmp", opts);
    Serial.println("Rebooting ESP for restored settings...");
    rebootDelay.reboot = true;
    rebootDelay.rebootTime = millis() + 1000;
  }
}
void Web::handleRestoreUpload(WebServer &server) {
  esp_task_wdt_reset();
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    webServer.uploadSuccess = false;
    Serial.printf("Restore: %s\n", upload.filename.c_str());
    File fup = LittleFS.open("/shades.tmp", "w");
    fup.close();
  }
  else if (upload.status == UPLOAD_FILE_WRITE) {
    File fup = LittleFS.open("/shades.tmp", "a");
    fup.write(upload.buf, upload.currentSize);
    fup.close();
  }
  else if (upload.status == UPLOAD_FILE_END) {
    webServer.uploadSuccess = true;
  }
}
void Web::handleUpdateFirmware(WebServer &server) {
  if (Update.hasError())
    server.send(500, _encoding_json, "{\"status\":\"ERROR\",\"desc\":\"Error updating firmware: \"}");
  else
    server.send(200, _encoding_json, "{\"status\":\"SUCCESS\",\"desc\":\"Successfully updated firmware\"}");
  rebootDelay.reboot = true;
  rebootDelay.rebootTime = millis() + 500;
}
void Web::handleUpdateFirmwareUpload(WebServer &server) {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    webServer.uploadSuccess = false;
    Serial.printf("Update: %s - %d\n", upload.filename.c_str(), upload.totalSize);
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Update.printError(Serial);
    }
    else {
      somfy.transceiver.end();
      mqtt.end();
    }
  }
  else if(upload.status == UPLOAD_FILE_ABORTED) {
    Serial.printf("Upload of %s aborted\n", upload.filename.c_str());
    Update.abort();
  }
  else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
      Serial.printf("Upload of %s aborted invalid size %d\n", upload.filename.c_str(), upload.currentSize);
      Update.abort();
    }
  }
  else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
      webServer.uploadSuccess = true;
    }
    else {
      Update.printError(Serial);
    }
  }
  esp_task_wdt_reset();
}
void Web::handleUpdateShadeConfig(WebServer &server) {
  if(git.lockFS) {
    server.send(500, _encoding_json, F("{\"status\":\"ERROR\",\"desc\":\"Filesystem update in progress\"}"));
    return;
  }
  server.sendHeader("Connection", "close");
  server.send(200, _encoding_json, "{\"status\":\"ERROR\",\"desc\":\"Updating Shade Config: \"}");
}
void Web::handleUpdateShadeConfigUpload(WebServer &server) {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    Serial.printf("Update: shades.cfg\n");
    File fup = LittleFS.open("/shades.tmp", "w");
    fup.close();
  }
  else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      File fup = LittleFS.open("/shades.tmp", "a");
      fup.write(upload.buf, upload.currentSize);
      fup.close();
    }
  }
  else if (upload.status == UPLOAD_FILE_END) {
    somfy.loadShadesFile("/shades.tmp");
  }
}
void Web::handleUpdateApplication(WebServer &server) {
  server.sendHeader("Connection", "close");
  if (Update.hasError())
    server.send(500, _encoding_json, "{\"status\":\"ERROR\",\"desc\":\"Error updating application: \"}");
  else
    server.send(200, _encoding_json, "{\"status\":\"SUCCESS\",\"desc\":\"Successfully updated application\"}");
  rebootDelay.reboot = true;
  rebootDelay.rebootTime = millis() + 500;
}
void Web::handleUpdateApplicationUpload(WebServer &server) {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    webServer.uploadSuccess = false;
    Serial.printf("Update: %s %d\n", upload.filename.c_str(), upload.totalSize);
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_SPIFFS)) {
      Update.printError(Serial);
    }
    else {
      somfy.transceiver.end();
      mqtt.end();
    }
  }
  else if(upload.status == UPLOAD_FILE_ABORTED) {
    Serial.printf("Upload of %s aborted\n", upload.filename.c_str());
    Update.abort();
    somfy.commit();
  }
  else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
      Serial.printf("Upload of %s aborted invalid size %d\n", upload.filename.c_str(), upload.currentSize);
      Update.abort();
    }
  }
  else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      webServer.uploadSuccess = true;
      Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
      somfy.commit();
    }
    else {
      somfy.commit();
      Update.printError(Serial);
    }
  }
  esp_task_wdt_reset();
}

