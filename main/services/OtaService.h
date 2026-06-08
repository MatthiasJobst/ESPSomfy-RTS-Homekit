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

struct restore_options_t;

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

    /**
     * @brief Stream an uploaded backup file to LittleFS (/shades.tmp).
     *
     * @param upload The current upload chunk from server.upload().
     */
    void backupUploadChunk(HTTPUpload &upload);

    /** @brief True if a backup file was fully received by backupUploadChunk(). */
    bool backupReceived() const { return _backupReceived; }

    /**
     * @brief Apply the received backup (/shades.tmp) and schedule a reboot.
     *
     * @param opts Which record types to restore.
     */
    void applyBackup(restore_options_t &opts);

    /**
     * @brief Stream an uploaded shade config (shades.cfg) to LittleFS and reload it.
     *
     * @param upload The current upload chunk from server.upload().
     */
    void shadeConfigUploadChunk(HTTPUpload &upload);

    /**
     * @brief Queue a GitHub firmware download for the given release/tag.
     * @param version Release tag (e.g. "v0.4.2") or "main".
     */
    void queueFirmwareDownload(const char *version);

    /**
     * @brief Cancel an in-progress firmware download (not during FS update).
     *
     * Sets the updater to the cancelling state; GitUpdater::loop() completes the
     * cancel (sets cancelled + emits). Caller should check filesystemLocked().
     */
    void cancelFirmwareUpdate();

    /** @brief Re-emit the update-availability status over the websocket. */
    void emitUpdateCheck();

    /** @brief True if a filesystem update is in progress (uploads must wait). */
    bool filesystemLocked() const;

    /**
     * @brief Schedule a post-update reboot @p ms milliseconds from now.
     * @param ms Delay before the main loop performs the reboot.
     */
    void requestReboot(uint32_t ms);

  private:
    /** @brief Stop the RF transceiver and MQTT so flashing isn't disturbed. */
    void quiesceForUpdate();

    /** @brief Shared WRITE/ABORTED/END handling for both image kinds. */
    void writeOrFinish(HTTPUpload &upload);

    bool _uploadOk = false;
    bool _backupReceived = false;
};
