// ConfigFile.cpp — ConfigFile base class implementation: open/close, typed binary
// read/write helpers (int, uint, float, bool, string), record seeking and header
// read/write. Used as the base for ShadeConfigFile.
#include <Arduino.h>
#include <LittleFS.h>
#include <esp_log.h>
#include "ConfigFile.h"
#include "Utils.h"
#include "ConfigSettings.h"

extern ConfigSettings settings;

static const char *TAG = "ConfigFile";

bool ConfigFile::begin(const char* filename, bool readOnly) {
  this->file = LittleFS.open(filename, readOnly ? "r" : "w");
  this->_opened = true;
  return true;
}
void ConfigFile::end() {
  if(this->isOpen()) {
    if(!this->readOnly) this->file.flush();
    this->file.close();
  }
  this->_opened = false;
}
bool ConfigFile::isOpen() { return this->_opened; }
bool ConfigFile::seekChar(const char val) {
  if(!this->isOpen()) return false;
  char ch;
  do {
    ch = this->readChar('\0');
    if(ch == '\0') return false;
  } while(ch != val);
  return true;
}
bool ConfigFile::writeSeparator() {return this->writeChar(CFG_VALUE_SEP); }
bool ConfigFile::writeRecordEnd() { return this->writeChar(CFG_REC_END); }
bool ConfigFile::writeHeader() { return this->writeHeader(this->header); }
bool ConfigFile::writeHeader(const config_header_t &hdr) {
  if(!this->isOpen()) return false;
  this->writeUInt8(hdr.version);
  this->writeUInt8(hdr.length);
  this->writeUInt16(hdr.roomRecordSize);
  this->writeUInt8(hdr.roomRecords);
  this->writeUInt16(hdr.shadeRecordSize);
  this->writeUInt8(hdr.shadeRecords);
  this->writeUInt16(hdr.groupRecordSize);
  this->writeUInt8(hdr.groupRecords);
  this->writeUInt16(hdr.repeaterRecordSize);
  this->writeUInt8(hdr.repeaterRecords);
  this->writeUInt16(hdr.settingsRecordSize);
  this->writeUInt16(hdr.netRecordSize);
  this->writeUInt16(hdr.transRecordSize);
  this->writeString(settings.serverId, sizeof(hdr.serverId), CFG_REC_END);
  return true;
}
bool ConfigFile::readHeader() {
  if(!this->isOpen()) return false;
  //if(this->file.position() != 0) this->file.seek(0, SeekSet);
  ESP_LOGI(TAG, "Reading header at %u", this->file.position());
  this->header.version = this->readUInt8(this->header.version);
  this->header.length = this->readUInt8(0);
  if(this->header.version >= 19) {
    this->header.roomRecordSize = this->readUInt16(this->header.roomRecordSize);
    this->header.roomRecords = this->readUInt8(this->header.roomRecords);
  }
  if(this->header.version >= 13) this->header.shadeRecordSize = this->readUInt16(this->header.shadeRecordSize);
  else this->header.shadeRecordSize = this->readUInt8((uint8_t)this->header.shadeRecordSize);
  this->header.shadeRecords = this->readUInt8(this->header.shadeRecords);
  if(this->header.version > 10) {
    if(this->header.version >= 13) this->header.groupRecordSize = this->readUInt16(this->header.groupRecordSize);
    else this->header.groupRecordSize = this->readUInt8(this->header.groupRecordSize);
    this->header.groupRecords = this->readUInt8(this->header.groupRecords);
  }
  if(this->header.version >= 21) {
    this->header.repeaterRecordSize = this->readUInt16(this->header.repeaterRecordSize);
    this->header.repeaterRecords = this->readUInt8(this->header.repeaterRecords);
  }
  if(this->header.version > 13) {
    this->header.settingsRecordSize = this->readUInt16(this->header.settingsRecordSize);
    this->header.netRecordSize = this->readUInt16(this->header.netRecordSize);
    this->header.transRecordSize = this->readUInt16(this->header.transRecordSize);
    this->readString(this->header.serverId, sizeof(this->header.serverId));
  }
  ESP_LOGI(TAG, "version:%u len:%u roomSize:%u roomRecs:%u shadeSize:%u shadeRecs:%u groupSize:%u groupRecs: %u pos:%d", this->header.version, this->header.length, this->header.roomRecordSize, this->header.roomRecords, this->header.shadeRecordSize, this->header.shadeRecords, this->header.groupRecordSize, this->header.groupRecords, this->file.position());
  return true;
}
bool ConfigFile::readString(char *buff, size_t len) {
  if(!this->file) return false;
  memset(buff, 0x00, len);
  uint16_t i = 0;
  while(i < len) {
    uint8_t val;
    if(this->file.read(&val, 1) == 1) {
      switch(val) {
        case CFG_REC_END:
        case CFG_VALUE_SEP:
          _rtrim(buff);
          return true;
      }
      buff[i++] = val;
      if(i == len) {
        _rtrim(buff);
        return true;
      }
    }
    else
      return false;
  }
  _rtrim(buff);
  return true;
}
bool ConfigFile::skipValue(size_t len) {
  if(!this->file) return false;
  uint8_t quotes = 0;
  uint8_t j = 0;
  while(j < len) {
    uint8_t val;
    j++;
    if(this->file.read(&val, 1) == 1) {
      switch(val) {
        case CFG_VALUE_SEP:
          if(quotes >= 2 || quotes == 0) return true;
          break;
        case CFG_REC_END:
          return true;
        case CFG_TOK_QUOTE:
          quotes++;
          break;
      }
    }
    else return false;
  }
  return true;
}
bool ConfigFile::readVarString(char *buff, size_t len) {
  if(!this->file) return false;
  memset(buff, 0x00, len);
  uint8_t quotes = 0;
  uint16_t i = 0;
  uint16_t j = 0;
  while(j < len) {
    uint8_t val;
    j++;
    if(this->file.read(&val, 1) == 1) {
      switch(val) {
        case CFG_VALUE_SEP:
          if(quotes >= 2) {
            _rtrim(buff);
            return true;
          }
          break;
        case CFG_REC_END:
          return true;
        case CFG_TOK_QUOTE:
          quotes++;
          continue;
      }
      buff[i++] = val;
      if(i == len) {
        _rtrim(buff);
        return true;
      }
    }
    else
      return false;
  }
  _rtrim(buff);
  return true;
}
bool ConfigFile::writeString(const char *val, size_t len, const char tok) {
  if(!this->isOpen()) return false;
  int slen = strlen(val);
  if(slen > 0)
    if(this->file.write((uint8_t *)val, slen) != slen) return false;
  // Now we need to pad the end of the string so that it is of a fixed length.
  while(slen < len - 1) {
    this->file.write(' ');
    slen++;
  }
  if(tok != CFG_TOK_NONE)
    return this->writeChar(tok);
  return true;
}
bool ConfigFile::writeVarString(const char *val, const char tok) {
  if(!this->isOpen()) return false;
  int slen = strlen(val);
  this->writeChar(CFG_TOK_QUOTE);
  if(slen > 0) if(this->file.write((uint8_t *)val, slen) != slen) return false;
  this->writeChar(CFG_TOK_QUOTE);
  if(tok != CFG_TOK_NONE) return this->writeChar(tok);
  return true;
}
bool ConfigFile::writeChar(const char val) {
  if(!this->isOpen()) return false;
  if(this->file.write(static_cast<uint8_t>(val)) == 1) return true;
  return false;
}
bool ConfigFile::writeInt8(const int8_t val, const char tok) {
  char buff[5];
  snprintf(buff, sizeof(buff), "%4d", val);
  return this->writeString(buff, sizeof(buff), tok);
}
bool ConfigFile::writeUInt8(const uint8_t val, const char tok) {
  char buff[4];
  snprintf(buff, sizeof(buff), "%3u", val);
  return this->writeString(buff, sizeof(buff), tok);
}
bool ConfigFile::writeUInt16(const uint16_t val, const char tok) {
  char buff[6];
  snprintf(buff, sizeof(buff), "%5u", val);
  return this->writeString(buff, sizeof(buff), tok);
}
bool ConfigFile::writeUInt32(const uint32_t val, const char tok) {
  char buff[11];
  snprintf(buff, sizeof(buff), "%10lu", val);
  return this->writeString(buff, sizeof(buff), tok);
}
bool ConfigFile::writeFloat(const float val, const uint8_t prec, const char tok) {
  char buff[20];
  snprintf(buff, sizeof(buff), "%*.*f", 7 + prec, prec, val);
  return this->writeString(buff, 8 + prec, tok);
}
bool ConfigFile::writeBool(const bool val, const char tok) {
  return this->writeString(val ? "true" : "false", 6, tok);
}
char ConfigFile::readChar(const char defVal) {
  uint8_t ch;
  if(this->file.read(&ch, 1) == 1) return (char)ch;
  return defVal;
}
int8_t ConfigFile::readInt8(const int8_t defVal) {
  char buff[5];
  if(this->readString(buff, sizeof(buff)))
    return static_cast<int8_t>(atoi(buff));
  return defVal;
}
uint8_t ConfigFile::readUInt8(const uint8_t defVal) {
  char buff[4];
  if(this->readString(buff, sizeof(buff)))
    return static_cast<uint8_t>(atoi(buff));
  return defVal;
}
uint16_t ConfigFile::readUInt16(const uint16_t defVal) {
  char buff[6];
  if(this->readString(buff, sizeof(buff)))
    return static_cast<uint16_t>(atoi(buff));
  return defVal;
}
uint32_t ConfigFile::readUInt32(const uint32_t defVal) {
  char buff[11];
  if(this->readString(buff, sizeof(buff)))
    return static_cast<uint32_t>(atoi(buff));
  return defVal;
}
float ConfigFile::readFloat(const float defVal) {
  char buff[25];
  if(this->readString(buff, sizeof(buff)))
    return atof(buff);
  return defVal;
}
bool ConfigFile::readBool(const bool defVal) {
  char buff[6];
  if(this->readString(buff, sizeof(buff))) {
    switch(buff[0]) {
      case 't':
      case 'T':
      case '1':
        return true;
      default:
        return false;
    }
  }
  return defVal;
}
