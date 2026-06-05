#pragma once
// WebRooms.h — HTTP handler module for Somfy room management.
//
// Concrete WebHandler owning the room CRUD/query routes on both the public HTTP
// server and the REST API server. begin() registers the routes; end() is a
// no-op (WebServer has no per-route removal).

#include <WebServer.h>
#include "WebHandler.h"

class WebRooms : public WebHandler {
  public:
    using WebHandler::WebHandler;

    /** @brief Register every room route on the public and API servers. */
    void begin() override;

    /** @brief No-op; WebServer has no per-route removal. */
    void end() override;

  private:
    /**
     * @brief Stream all rooms as a JSON array.
     * @param server The server the request arrived on.
     */
    void handleGetRooms(WebServer &server);

    /**
     * @brief Read (GET) or update (PUT/POST) a single room by id.
     * @param server The server the request arrived on.
     */
    void handleRoom(WebServer &server);

    /**
     * @brief Return the next available room id scaffold.
     * @param server The server the request arrived on.
     */
    void handleGetNextRoom(WebServer &server);

    /**
     * @brief Persist the display sort order from a posted id array.
     * @param server The server the request arrived on.
     */
    void handleRoomSortOrder(WebServer &server);

    /**
     * @brief Add a new room from the posted JSON body.
     * @param server The server the request arrived on.
     */
    void handleAddRoom(WebServer &server);

    /**
     * @brief Save edits to an existing room from the posted JSON body.
     * @param server The server the request arrived on.
     */
    void handleSaveRoom(WebServer &server);

    /**
     * @brief Delete a room by id.
     * @param server The server the request arrived on.
     */
    void handleDeleteRoom(WebServer &server);
};
