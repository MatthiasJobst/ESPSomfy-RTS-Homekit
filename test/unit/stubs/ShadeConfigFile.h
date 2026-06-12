// ShadeConfigFile.h — stub replacing main/ShadeConfigFile.h for unit tests.
// Provides no-op implementations; exposes call-count globals so tests can
// assert whether commit() / writeBackup() actually called save() / backup().
#pragma once
#include <cstdint>

// Control flags — set in test SetUp(); read by the stub methods.
extern bool stub_shadeconfig_exists;

// Call counters — reset in test SetUp(); read in test assertions.
extern int stub_save_call_count;
extern int stub_backup_call_count;

class SomfyShadeController; // forward declaration — avoids circular include

class ShadeConfigFile {
  public:
    static bool exists() { return stub_shadeconfig_exists; }
    static bool load(SomfyShadeController *, const char * = nullptr) { return true; }

    bool begin(const char *, bool = false) { return true; }
    bool begin() { return true; }
    bool save(SomfyShadeController *)
    {
        stub_save_call_count++;
        return true;
    }
    bool backup(SomfyShadeController *)
    {
        stub_backup_call_count++;
        return true;
    }
    bool validate() { return true; }
    void end() {}
};
