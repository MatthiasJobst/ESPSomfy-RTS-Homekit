#pragma once
// WebGroups.h — HTTP handler module for Somfy shade group management.
//
// Concrete WebHandler owning the group CRUD/query routes plus group command
// dispatch (/groupCommand) on both the public HTTP server and the REST API
// server. begin() registers the routes; end() is a no-op (WebServer has no
// per-route removal).

#include <WebServer.h>
#include "WebHandler.h"

class WebGroups : public WebHandler {
  public:
    using WebHandler::WebHandler;

    /** @brief Register every group route on the public and API servers. */
    void begin() override;

    /** @brief No-op; WebServer has no per-route removal. */
    void end() override;

  private:
    /**
     * @brief Stream all groups as a JSON array.
     * @param server The server the request arrived on.
     */
    void handleGetGroups(WebServer &server);

    /**
     * @brief Read (GET) or update (PUT/POST) a single group by id.
     * @param server The server the request arrived on.
     */
    void handleGroup(WebServer &server);

    /**
     * @brief Enqueue a Somfy command for a group.
     * @param server The server the request arrived on.
     */
    void handleGroupCommand(WebServer &server);

    /**
     * @brief Return the next available group id and remote address scaffold.
     * @param server The server the request arrived on.
     */
    void handleGetNextGroup(WebServer &server);

    /**
     * @brief Persist the display sort order from a posted id array.
     * @param server The server the request arrived on.
     */
    void handleGroupSortOrder(WebServer &server);

    /**
     * @brief Add a new group from the posted JSON body.
     * @param server The server the request arrived on.
     */
    void handleAddGroup(WebServer &server);

    /**
     * @brief Save edits to an existing group from the posted JSON body.
     * @param server The server the request arrived on.
     */
    void handleSaveGroup(WebServer &server);

    /**
     * @brief Return a group plus the shades still available to link to it.
     * @param server The server the request arrived on.
     */
    void handleGroupOptions(WebServer &server);

    /**
     * @brief Delete a group by id.
     * @param server The server the request arrived on.
     */
    void handleDeleteGroup(WebServer &server);

    /**
     * @brief Link a shade into a group.
     * @param server The server the request arrived on.
     */
    void handleLinkToGroup(WebServer &server);

    /**
     * @brief Unlink a shade from a group.
     * @param server The server the request arrived on.
     */
    void handleUnlinkFromGroup(WebServer &server);
};
