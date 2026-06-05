// WebFiles.cpp — Static front-end asset handler (see WebFiles.h).
//
// Streams the web UI bundle (HTML/JS/CSS/icons) and the shade config files from
// LittleFS. The route table drives begin(); each entry optionally requests the
// shared long-lived cache headers before streaming.

#include "WebFiles.h"
#include <LittleFS.h>
#include <esp_log.h>
#include <esp_task_wdt.h>
#include "GitOTA.h"
#include "Sockets.h"

extern GitUpdater git;
extern SocketEmitter sockEmit;

static const char *s_TAG = "WebFiles";

const WebFiles::StaticFile WebFiles::s_files[] = {
    {"/", "/index.html", "text/html", false},
    {"/shades.cfg", "/shades.cfg", "text/plain", false},
    {"/shades.tmp", "/shades.tmp", "text/plain", false},
    {"/index.js", "/index.js", "text/javascript", true},
    {"/ui.js", "/ui.js", "text/javascript", true},
    {"/settings.js", "/settings.js", "text/javascript", true},
    {"/somfy.js", "/somfy.js", "text/javascript", true},
    {"/extras.js", "/extras.js", "text/javascript", true},
    {"/qrcode.min.js", "/qrcode.min.js", "text/javascript", true},
    {"/main.css", "/main.css", "text/css", true},
    {"/widgets.css", "/widgets.css", "text/css", true},
    {"/icons.css", "/icons.css", "text/css", true},
    {"/favicon.png", "/favicon.png", "image/png", true},
    {"/icon.png", "/icon.png", "image/png", true},
    {"/icon.svg", "/icon.svg", "image/svg+xml", true},
    {"/apple-icon.png", "/apple-icon.png", "image/png", true},
    {"/HomeKit.svg", "/HomeKit.svg", "image/svg+xml", true},
};
const size_t WebFiles::s_fileCount = sizeof(s_files) / sizeof(s_files[0]);

void WebFiles::begin()
{
    for (size_t i = 0; i < s_fileCount; i++) {
        const StaticFile *f = &s_files[i];
        registerHandler(f->route, [this, f]() {
            if (f->cache) this->sendCacheHeaders();
            this->handleStreamFile(f->file, f->encoding);
        });
    }
}

void WebFiles::end()
{
    // WebServer exposes no per-route removal; nothing to release.
}

void WebFiles::sendCacheHeaders(uint32_t seconds)
{
    server.sendHeader(F("Cache-Control"), F("public, max-age=604800, immutable"));
}

void WebFiles::handleStreamFile(const char *filename, const char *encoding)
{
    if (git.lockFS) {
        server.send(500, "application/json", F("{\"status\":\"ERROR\",\"desc\":\"Filesystem update in progress\"}"));
        return;
    }
    // Load the index html page from the data directory.
    ESP_LOGI(s_TAG, "Loading file %s", filename);
    File file = LittleFS.open(filename, "r");
    if (!file) {
        ESP_LOGE(s_TAG, "Error opening %s", filename);
        server.send(500, "text/plain", "Error opening file");
        return;
    }
    // Stream the file in 1 KB chunks. Poll the WebSocket server every 4 chunks
    // (~4 KB) so a new WS connection upgrade request is never starved while a
    // large file (e.g. somfy.js ~128 KB) is being transferred.
    server.setContentLength(file.size());
    server.send(200, encoding, ""); // Send status + headers; body follows via sendContent
    static uint8_t streamBuf[1024];
    uint8_t chunkCount = 0;
    while (file.available()) {
        int n = static_cast<int>(file.read(streamBuf, sizeof(streamBuf)));
        if (n > 0) server.sendContent((const char *)streamBuf, n);
        if (++chunkCount == 4) {
            chunkCount = 0;
            sockEmit.loop();
            esp_task_wdt_reset();
        }
    }
    file.close();
}
