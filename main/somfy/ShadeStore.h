// ShadeStore.h — Owns config persistence policy for the Somfy model: the dirty
// flag, the auto-commit throttle, and the LittleFS read/write/backup path via
// ShadeConfigFile.  Extracted from SomfyShadeController so the aggregate root no
// longer carries persistence concerns (filesystem-lock policy, binary format,
// commit timing).
#pragma once

#include <cstdint>

class SomfyShadeController;

/**
 * @brief Persistence coordinator for the whole Somfy config.
 *
 * Holds the in-memory/disk dirty state and the auto-commit throttle, and drives
 * ShadeConfigFile to load/save/backup the controller it is bound to. All writes
 * are skipped while the filesystem is locked for an OTA update (git.lockFS).
 */
class ShadeStore {
  public:
    /**
     * @brief Bind the store to the controller whose config it persists.
     * @param controller Model serialised on commit()/writeBackup() and populated by load().
     */
    explicit ShadeStore(SomfyShadeController &controller);

    bool dirty = false;      /**< In-memory config differs from disk. */
    uint32_t lastCommit = 0; /**< millis() of the last commit (auto-commit throttle). */

    /**
     * @brief Flag the config as needing persistence.
     */
    void markDirty();

    /**
     * @brief Write the full config to LittleFS and clear the dirty flag.
     * @note No-op while the filesystem is locked for an OTA update.
     */
    void commit();

    /**
     * @brief Commit if dirty and more than 1 s has elapsed since the last commit.
     */
    void autoCommit();

    /**
     * @brief Write a backup snapshot of the config to /controller.backup.
     * @note No-op while the filesystem is locked for an OTA update.
     */
    void writeBackup();

    /**
     * @brief Load config into the bound controller from a named file.
     * @param filename Path of the config file.
     * @return True on success.
     */
    bool load(const char *filename);

  private:
    SomfyShadeController &controller; /**< Model this store persists; not owned. */
};
