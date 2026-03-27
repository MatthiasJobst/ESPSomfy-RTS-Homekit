// ShadeConfigFile.h — ShadeConfigFile: reads and writes the shades.cfg binary
// record store on LittleFS. Handles save/backup/restore/validate and all per-record
// read/write helpers for shades, groups, rooms, repeaters, settings, network and
// transceiver records.
#ifndef shadeconfigfile_h
#define shadeconfigfile_h
#include "ConfigFile.h"
#include "SomfyController.h"

class ShadeConfigFile : public ConfigFile {
  protected:
    bool writeRepeaterRecord(SomfyShadeController *s);
    bool writeRoomRecord(SomfyRoom *room);
    bool writeShadeRecord(SomfyShade *shade);
    bool writeGroupRecord(SomfyGroup *group);
    bool writeSettingsRecord();
    bool writeNetRecord();
    bool writeTransRecord(transceiver_config_t &cfg);
    bool readRepeaterRecord(SomfyShadeController *s);
    bool readRoomRecord(SomfyRoom *room);
    bool readShadeRecord(SomfyShade *shade);
    bool readGroupRecord(SomfyGroup *group);
    bool readSettingsRecord();
    bool readNetRecord(restore_options_t &opts);
    bool readTransRecord(transceiver_config_t &cfg);
  public:
    static bool exists();
    static bool load(SomfyShadeController *somfy, const char *filename = "/shades.cfg");
    static bool restore(SomfyShadeController *somfy, const char *filename, restore_options_t &opts);
    bool begin(const char *filename, bool readOnly = false);
    bool begin(bool readOnly = false);
    bool save(SomfyShadeController *somfy);
    bool backup(SomfyShadeController *somfy);
    bool loadFile(SomfyShadeController *somfy, const char *filename = "/shades.cfg");
    bool restoreFile(SomfyShadeController *somfy, const char *filename, restore_options_t &opts);
    void end();
    bool validate();
};
#endif
