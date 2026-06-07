// OtaService.cpp — Update.h streaming + peripheral quiesce for local OTA images.

#include "OtaService.h"

#include <esp_log.h>
#include <esp_task_wdt.h>
#include <LittleFS.h>
#include <Update.h>
#include "MQTT.h"
#include "SomfyShadeController.h"

extern SomfyShadeController somfy;
extern MQTTClass mqtt;

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
        somfy.commit();
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
