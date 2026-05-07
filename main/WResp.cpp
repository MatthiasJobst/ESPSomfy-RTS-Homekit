#include "esp_log.h"
#include "WResp.h"

static const char *TAG = "WResp";

void JsonSockEvent::beginEvent(WebSocketsServer *server, const char *evt, char *buff, size_t buffSize) {
  this->server = server;
  this->buff = buff;
  this->buffSize = buffSize;
  this->_nocomma = true;
  this->_closed = false;
  snprintf(this->buff, buffSize, "42[%s,", evt);
}
void JsonSockEvent::closeEvent() {
  if(!this->_closed) {
    if(strlen(this->buff) < this->buffSize) strlcat(this->buff, "]", this->buffSize);
    else this->buff[this->buffSize - 1] = ']';
  }
  this->_nocomma = true;
  this->_closed = true;
}
void JsonSockEvent::endEvent(uint8_t num) {
  this->closeEvent();
  if(num == 255) this->server->broadcastTXT(this->buff);
  else this->server->sendTXT(num, this->buff);
}
void JsonSockEvent::_safecat(const char *val, bool escape) {
  size_t len = (escape ? this->calcEscapedLength(val) : strlen(val)) + strlen(this->buff);
  if(escape) len += 2;
  if(len >= this->buffSize) {
    ESP_LOGW(TAG, "Socket exceeded buffer size %d - %d", this->buffSize, len);
    ESP_LOGW(TAG, "%s", this->buff);
    return;
  }
  if(escape) strlcat(this->buff, "\"", this->buffSize);
  if(escape) this->escapeString(val, &this->buff[strlen(this->buff)], this->buffSize - strlen(this->buff));
  else strlcat(this->buff, val, this->buffSize);
  if(escape) strlcat(this->buff, "\"", this->buffSize);
}
void JsonResponse::beginResponse(WebServer *server, char *buff, size_t buffSize) {
  this->server = server;
  this->buff = buff;
  this->buffSize = buffSize;
  this->buff[0] = 0x00;
  this->_nocomma = true;
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
}
void JsonResponse::endResponse() {
  if(strlen(buff)) this->send();
  server->sendContent("", 0);
}
void JsonResponse::send() {
    if(!this->_headersSent) server->send_P(200, "application/json", this->buff);
    else server->sendContent(this->buff);
    ESP_LOGD(TAG, "Sent %d bytes %d", strlen(this->buff), this->buffSize);
    this->buff[0] = 0x00;
    this->_headersSent = true;
}
void JsonResponse::_safecat(const char *val, bool escape) {
  size_t len = (escape ? this->calcEscapedLength(val) : strlen(val)) + strlen(this->buff);
  if(escape) len += 2;
  if(len >= this->buffSize) {
    this->send();
  }
  if(escape) strlcat(this->buff, "\"", this->buffSize);
  if(escape) this->escapeString(val, &this->buff[strlen(this->buff)], this->buffSize - strlen(this->buff));
  else strlcat(this->buff, val, this->buffSize);
  if(escape) strlcat(this->buff, "\"", this->buffSize);
}

void JsonFormatter::beginObject(const char *name) {
  if(name && strlen(name) > 0) this->appendElem(name);
  else if(!this->_nocomma) this->_safecat(",");
  this->_safecat("{");
  this->_objects++;
  this->_nocomma = true;
}
void JsonFormatter::endObject() {
  //if(strlen(this->buff) + 1 > this->buffSize - 1) this->send();
  this->_safecat("}");
  this->_objects--;
  this->_nocomma = false;
}
void JsonFormatter::beginArray(const char *name) {
  if(name && strlen(name) > 0) this->appendElem(name);
  else if(!this->_nocomma) this->_safecat(",");
  this->_safecat("[");
  this->_arrays++;
  this->_nocomma = true;
}
void JsonFormatter::endArray() {
  //if(strlen(this->buff) + 1 > this->buffSize - 1) this->send();
  this->_safecat("]");
  this->_arrays--;
  this->_nocomma = false;
}

void JsonFormatter::appendElem(const char *name) {
  if(!this->_nocomma) this->_safecat(",");
  if(name && strlen(name) > 0) {
    this->_safecat(name, true);
    this->_safecat(":");
  }
  this->_nocomma = false;
}

void JsonFormatter::addElem(const char *name, const char *val) {
  if(!val) return;
  this->appendElem(name);
  this->_safecat(val, true);
}
void JsonFormatter::addElem(const char *val) { this->addElem(nullptr, val); }
void JsonFormatter::addElem(float fval) { sprintf(this->_numbuff, "%.4f", fval); this->_appendNumber(nullptr); }
void JsonFormatter::addElem(int8_t nval) { sprintf(this->_numbuff, "%d", nval); this->_appendNumber(nullptr); }
void JsonFormatter::addElem(uint8_t nval) { sprintf(this->_numbuff, "%u", nval); this->_appendNumber(nullptr); }
void JsonFormatter::addElem(int32_t nval) { sprintf(this->_numbuff, "%ld", (long)nval); this->_appendNumber(nullptr); }
void JsonFormatter::addElem(uint32_t nval) { sprintf(this->_numbuff, "%lu", (unsigned long)nval); this->_appendNumber(nullptr); }

/*
void JsonFormatter::addElem(int16_t nval) { sprintf(this->_numbuff, "%d", nval); this->_appendNumber(nullptr); }
void JsonFormatter::addElem(uint16_t nval) { sprintf(this->_numbuff, "%u", nval); this->_appendNumber(nullptr); }
void JsonFormatter::addElem(int64_t lval) { sprintf(this->_numbuff, "%lld", (long long)lval); this->_appendNumber(nullptr); }
void JsonFormatter::addElem(uint64_t lval) { sprintf(this->_numbuff, "%llu", (unsigned long long)lval); this->_appendNumber(nullptr); }
*/
void JsonFormatter::addElem(bool bval) { strlcpy(this->_numbuff, bval ? "true" : "false", sizeof(this->_numbuff)); this->_appendNumber(nullptr); }

void JsonFormatter::addElem(const char *name, float fval) { sprintf(this->_numbuff, "%.4f", fval); this->_appendNumber(name); }
void JsonFormatter::addElem(const char *name, int8_t nval) { sprintf(this->_numbuff, "%d", nval); this->_appendNumber(name); }
void JsonFormatter::addElem(const char *name, uint8_t nval) { sprintf(this->_numbuff, "%u", nval); this->_appendNumber(name); }
void JsonFormatter::addElem(const char *name, int32_t nval) { sprintf(this->_numbuff, "%ld", (long)nval); this->_appendNumber(name); }
void JsonFormatter::addElem(const char *name, uint32_t nval) { sprintf(this->_numbuff, "%lu", (unsigned long)nval); this->_appendNumber(name); }

/*
void JsonFormatter::addElem(const char *name, int16_t nval) { sprintf(this->_numbuff, "%d", nval); this->_appendNumber(name); }
void JsonFormatter::addElem(const char *name, uint16_t nval) { sprintf(this->_numbuff, "%u", nval); this->_appendNumber(name); }
void JsonFormatter::addElem(const char *name, int64_t lval) { sprintf(this->_numbuff, "%lld", (long long)lval); this->_appendNumber(name); }
void JsonFormatter::addElem(const char *name, uint64_t lval) { sprintf(this->_numbuff, "%llu", (unsigned long long)lval); this->_appendNumber(name); }
*/
void JsonFormatter::addElem(const char *name, bool bval) { strlcpy(this->_numbuff, bval ? "true" : "false", sizeof(this->_numbuff)); this->_appendNumber(name); }

void JsonFormatter::_safecat(const char *val, bool escape) {
  size_t len = (escape ? this->calcEscapedLength(val) : strlen(val)) + strlen(this->buff);
  if(escape) len += 2;
  if(len >= this->buffSize) {
    return;
  }
  if(escape) strlcat(this->buff, "\"", this->buffSize);
  if(escape) this->escapeString(val, &this->buff[strlen(this->buff)], this->buffSize - strlen(this->buff));
  else strlcat(this->buff, val, this->buffSize);
  if(escape) strlcat(this->buff, "\"", this->buffSize);
}
void JsonFormatter::_appendNumber(const char *name) { this->appendElem(name); this->_safecat(this->_numbuff); } 
uint32_t JsonFormatter::calcEscapedLength(const char *raw) {
  uint32_t len = 0;
  size_t n = strlen(raw);
  for(size_t i = 0; i < n; i++) {
    switch(raw[i]) {
      case '"': case '/': case '\b': case '\f':
      case '\n': case '\r': case '\t': case '\\':
        len += 2; break;
      default:
        len++; break;
    }
  }
  return len;
}
void JsonFormatter::escapeString(const char *raw, char *escaped, size_t escapedSize) {
  for(uint32_t i = 0; i < strlen(raw); i++) {
    switch(raw[i]) {
      case '"':  strlcat(escaped, "\\\"", escapedSize); break;
      case '/':  strlcat(escaped, "\\/",  escapedSize); break;
      case '\b': strlcat(escaped, "\\b",  escapedSize); break;
      case '\f': strlcat(escaped, "\\f",  escapedSize); break;
      case '\n': strlcat(escaped, "\\n",  escapedSize); break;
      case '\r': strlcat(escaped, "\\r",  escapedSize); break;
      case '\t': strlcat(escaped, "\\t",  escapedSize); break;
      case '\\': strlcat(escaped, "\\\\", escapedSize); break;
      default: {
        size_t len = strlen(escaped);
        if(len + 1 < escapedSize) {  // need room for byte + NUL
          escaped[len] = raw[i];
          escaped[len + 1] = 0x00;
        }
        break;
      }
    }
  }
}
