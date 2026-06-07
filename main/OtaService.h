#pragma once
// OtaService.h — Device-side OTA procedures for locally-uploaded images.
//
// Owns the Update.h state machine for firmware (app partition) and filesystem
// (LittleFS partition) images streamed in over HTTP, plus the "quiesce the
// device before flashing" step (stop the RF transceiver + MQTT). This keeps the
// update orchestration out of the web handler layer: WebOTA just feeds it the
// per-chunk HTTPUpload objects and reads the result.
//
// Single in-flight upload at a time (the Arduino WebServer is synchronous), so
// one success flag suffices.

#include <WebServer.h> // HTTPUpload

class OtaService {
  public:
    /**
     * @brief Stream a firmware image (app partition) from an HTTP upload.
     *
     * Drives Update.h across UPLOAD_FILE_START/WRITE/ABORTED/END and quiesces
     * peripherals once the image is accepted.
     *
     * @param upload The current upload chunk from server.upload().
     */
    void firmwareUploadChunk(HTTPUpload &upload);

    /**
     * @brief Stream a filesystem image (LittleFS partition) from an HTTP upload.
     *
     * Commits shade state and unmounts LittleFS on start (the partition is about
     * to be overwritten), then drives Update.h as for firmware.
     *
     * @param upload The current upload chunk from server.upload().
     */
    void applicationUploadChunk(HTTPUpload &upload);

    /** @brief True if the most recent image upload completed successfully. */
    bool lastImageUploadOk() const { return _uploadOk; }

    /** @brief Human-readable description of the last Update.h error. */
    const char *lastImageError() const;

  private:
    /** @brief Stop the RF transceiver and MQTT so flashing isn't disturbed. */
    void quiesceForUpdate();

    /** @brief Shared WRITE/ABORTED/END handling for both image kinds. */
    void writeOrFinish(HTTPUpload &upload);

    bool _uploadOk = false;
};
