// OtaService.cpp — Update.h streaming + peripheral quiesce for local OTA images.

#include "OtaService.h"

#include <esp_log.h>
#include <esp_task_wdt.h>
#include <LittleFS.h>
#include <Update.h>
#include "ConfigSettings.h"
#include "GitOTA.h"
#include "MQTT.h"
#include "ShadeConfigFile.h"
#include "SomfyShadeController.h"
#include "Utils.h"

extern SomfyShadeController somfy;
extern MQTTClass mqtt;
extern rebootDelay_t rebootDelay;
extern GitUpdater git;

static const char *s_TAG = "OtaService";

void OtaService::quiesceForUpdate()
{
    somfy.transceiver.end();
    mqtt.end();
}

void OtaService::firmwareUploadChunk(HTTPUpload &upload)
{
    if (upload.status == UPLOAD_FILE_START) {
        _uploadOk = false;
        ESP_LOGI(s_TAG, "Update: %s - %d", upload.filename.c_str(), upload.totalSize);
        if (!Update.begin(UPDATE_SIZE_UNKNOWN))
            Update.printError(Serial);
        else
            quiesceForUpdate();
    } else
        writeOrFinish(upload);
}

void OtaService::applicationUploadChunk(HTTPUpload &upload)
{
    if (upload.status == UPLOAD_FILE_START) {
        _uploadOk = false;
        ESP_LOGI(s_TAG, "Update: %s %d", upload.filename.c_str(), upload.totalSize);
        somfy.store.commit();
        LittleFS.end();
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASHFS))
            Update.printError(Serial);
        else
            quiesceForUpdate();
    } else
        writeOrFinish(upload);
}

void OtaService::writeOrFinish(HTTPUpload &upload)
{
    if (upload.status == UPLOAD_FILE_ABORTED) {
        ESP_LOGE(s_TAG, "Upload of %s aborted", upload.filename.c_str());
        Update.abort();
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        esp_task_wdt_reset();
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            Update.printError(Serial);
            ESP_LOGE(s_TAG, "Upload of %s aborted invalid size %d", upload.filename.c_str(), upload.currentSize);
            Update.abort();
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) {
            ESP_LOGI(s_TAG, "Update Success: %u\nRebooting...\n", upload.totalSize);
            _uploadOk = true;
        } else {
            Update.printError(Serial);
            ESP_LOGE(s_TAG, "Update failed");
        }
    }
}

const char *OtaService::lastImageError() const
{
    return Update.errorString();
}

void OtaService::backupUploadChunk(HTTPUpload &upload)
{
    if (upload.status == UPLOAD_FILE_START) {
        _backupReceived = false;
        ESP_LOGI(s_TAG, "Restore: %s", upload.filename.c_str());
        File fup = LittleFS.open("/shades.tmp", "w");
        fup.close();
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        File fup = LittleFS.open("/shades.tmp", "a");
        fup.write(upload.buf, upload.currentSize);
        fup.close();
    } else if (upload.status == UPLOAD_FILE_END) {
        _backupReceived = true;
    }
}

void OtaService::applyBackup(restore_options_t &opts)
{
    ShadeConfigFile::restore(&somfy, "/shades.tmp", opts);
    ESP_LOGI(s_TAG, "Rebooting ESP for restored settings...");
    rebootDelay.requestReboot(1000);
}

void OtaService::shadeConfigUploadChunk(HTTPUpload &upload)
{
    if (upload.status == UPLOAD_FILE_START) {
        ESP_LOGI(s_TAG, "Update: shades.cfg");
        File fup = LittleFS.open("/shades.tmp", "w");
        fup.close();
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            File fup = LittleFS.open("/shades.tmp", "a");
            fup.write(upload.buf, upload.currentSize);
            fup.close();
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        somfy.store.load("/shades.tmp");
    }
}

void OtaService::queueFirmwareDownload(const char *version)
{
    strlcpy(git.targetRelease, version, sizeof(git.targetRelease));
    git.status = GIT_AWAITING_UPDATE;
}

void OtaService::cancelFirmwareUpdate()
{
    git.status = GIT_UPDATE_CANCELLING;
}

void OtaService::emitUpdateCheck()
{
    git.emitUpdateCheck();
}

bool OtaService::filesystemLocked() const
{
    return git.lockFS;
}

void OtaService::requestReboot(uint32_t ms)
{
    rebootDelay.requestReboot(ms);
}
