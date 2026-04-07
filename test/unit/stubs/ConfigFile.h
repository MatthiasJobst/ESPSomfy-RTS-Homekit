// Minimal stub — replaces main/ConfigFile.h for unit tests.
#pragma once
inline bool configFileExists(const char *) { return false; }
inline bool loadConfigFile(const char *)   { return false; }
inline bool saveConfigFile(const char *)   { return false; }
