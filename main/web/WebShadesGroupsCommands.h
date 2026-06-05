#pragma once
// WebShadesGroupsCommands.h — HTTP handler module for cross-domain Somfy commands.
//
// Concrete WebHandler owning the command endpoints that act on either a shade or
// a group (selected by shadeId/groupId in the request). Per-domain commands live
// in WebShades/WebGroups; these two are the shared shade-or-group operations.
// begin() registers the routes on both servers; end() is a no-op (WebServer has
// no per-route removal).

#include <WebServer.h>
#include "WebHandler.h"

class WebShadesGroupsCommands : public WebHandler {
  public:
    using WebHandler::WebHandler;

    /** @brief Register every command route on the public and API servers. */
    void begin() override;

    /** @brief No-op; WebServer has no per-route removal. */
    void end() override;

  private:
    /**
     * @brief Send or repeat a command to a shade or group (by shadeId/groupId).
     * @param server The server the request arrived on.
     */
    void handleRepeatCommand(WebServer &server);

    /**
     * @brief Send a sun/wind sensor command to a shade or group.
     * @param server The server the request arrived on.
     */
    void handleSetSensor(WebServer &server);
};
