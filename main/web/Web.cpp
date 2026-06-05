#include <WiFi.h>
#include <WebServer.h>
#include "esp_log.h"
#include "AppConfig.h"
#include "Web.h"
#include "WebHandler.h"
#include "WebFiles.h"
#include "WebGroups.h"
#include "WebRooms.h"
#include "WebShades.h"
#include "WebSettings.h"
#include "WebOTA.h"
#include "WebUtils.h"
#include "WebHomeKit.h"
#include "WebShadesGroupsCommands.h"
#include "WebAuth.h"
#include "WebSystem.h"

static const char *s_TAG = "Web";

WebServer apiServer(APP_API_PORT);
WebServer server(APP_HTTP_PORT);
WebFiles webFiles(server, apiServer);
WebGroups webGroups(server, apiServer);
WebRooms webRooms(server, apiServer);
WebShades webShades(server, apiServer);
WebSettings webSettings(server, apiServer);
WebOTA webOTA(server, apiServer);
WebUtils webUtils(server, apiServer);
WebHomeKit webHomeKit(server, apiServer);
WebShadesGroupsCommands webShadesGroupsCommands(server, apiServer);
WebAuth webAuth(server, apiServer);
WebSystem webSystem(server, apiServer);

// The web layer owns the set of handler modules; begin()/end() drive them as a
// group, in this order.
static WebHandler *const s_handlers[] = {
    &webFiles, &webGroups, &webRooms, &webShades, &webSettings, &webOTA, &webUtils,
    &webHomeKit, &webShadesGroupsCommands, &webAuth, &webSystem,
};
void Web::startup()
{
    ESP_LOGI(s_TAG, "Launching web server...");
}
void Web::loop()
{
    server.handleClient();
    delay(1);
    apiServer.handleClient();
    delay(1);
}
void Web::end()
{
    for (WebHandler *handler : s_handlers) handler->end();
    // server.end();
}
void Web::begin()
{
    ESP_LOGI(s_TAG, "Creating Web MicroServices...");
    const char *keys[1] = {"apikey"};
    server.collectHeaders(keys, 1);
    apiServer.collectHeaders(keys, 1);
    // Every route lives in a WebHandler module; register them all.
    for (WebHandler *handler : s_handlers) handler->begin();
    server.begin();
    apiServer.begin();
}
