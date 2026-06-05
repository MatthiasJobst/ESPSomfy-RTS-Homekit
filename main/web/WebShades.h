#pragma once
// WebShades.h — HTTP handler module for Somfy shade management.
//
// Concrete WebHandler owning the shade query/command/CRUD routes on both the
// public HTTP server and the REST API server. begin() registers the routes;
// end() is a no-op (WebServer has no per-route removal).

#include <WebServer.h>
#include "WebHandler.h"

class SomfyShade;

class WebShades : public WebHandler {
  public:
    using WebHandler::WebHandler;

    /** @brief Register every shade route on the public and API servers. */
    void begin() override;

    /** @brief No-op; WebServer has no per-route removal. */
    void end() override;

  private:
    /**
     * @brief Look up a shade by id, sending a 500 error if not found.
     * @param server The server to send the error on.
     * @param shadeId The shade id to look up.
     * @return The shade, or nullptr (after sending the error) if not found.
     */
    static SomfyShade *requireShade(WebServer &server, uint8_t shadeId);

    /**
     * @brief Send a shade's JSON state as the response.
     * @param server The server the request arrived on.
     * @param shade The shade to serialize.
     * @param ref When true, emit the minimal toJSONRef() form.
     */
    static void sendShadeJSON(WebServer &server, SomfyShade *shade, bool ref = false);

    /**
     * @brief Link or unlink a hardware repeater by address.
     * @param server The server the request arrived on.
     * @param link true to link, false to unlink.
     */
    static void repeaterLinkOp(WebServer &server, bool link);

    /**
     * @brief Link or unlink a physical remote to/from a shade.
     * @param server The server the request arrived on.
     * @param link true to link, false to unlink.
     */
    static void remoteLinkOp(WebServer &server, bool link);

    /**
     * @brief Stream all shades as a JSON array.
     * @param server The server the request arrived on.
     */
    void handleGetShades(WebServer &server);

    /**
     * @brief Read a single shade by id (GET only).
     * @param server The server the request arrived on.
     */
    void handleShade(WebServer &server);

    /**
     * @brief Enqueue a movement command or position target for a shade.
     * @param server The server the request arrived on.
     */
    void handleShadeCommand(WebServer &server);

    /**
     * @brief Enqueue a tilt command or tilt target for a shade.
     * @param server The server the request arrived on.
     */
    void handleTiltCommand(WebServer &server);

    /**
     * @brief Set the reported current/tilt position of a shade without moving it.
     * @param server The server the request arrived on.
     */
    void handleSetPositions(WebServer &server);

    /**
     * @brief Return the next available shade id and remote address scaffold.
     * @param server The server the request arrived on.
     */
    void handleGetNextShade(WebServer &server);

    /**
     * @brief Persist the display sort order from a posted id array.
     * @param server The server the request arrived on.
     */
    void handleShadeSortOrder(WebServer &server);

    /**
     * @brief Add a new shade from the posted JSON body.
     * @param server The server the request arrived on.
     */
    void handleAddShade(WebServer &server);

    /**
     * @brief Save edits to an existing shade from the posted JSON body.
     * @param server The server the request arrived on.
     */
    void handleSaveShade(WebServer &server);

    /**
     * @brief Delete a shade by id (rejected if it belongs to a group).
     * @param server The server the request arrived on.
     */
    void handleDeleteShade(WebServer &server);

    /**
     * @brief Set the shade's "my" (favourite) position and tilt.
     * @param server The server the request arrived on.
     */
    void handleSetMyPosition(WebServer &server);

    /**
     * @brief Override the stored rolling code for a shade.
     * @param server The server the request arrived on.
     */
    void handleSetRollingCode(WebServer &server);

    /**
     * @brief Set the paired flag on a shade.
     * @param server The server the request arrived on.
     */
    void handleSetPaired(WebServer &server);

    /**
     * @brief Send a prog command to unpair a shade from its motor.
     * @param server The server the request arrived on.
     */
    void handleUnpairShade(WebServer &server);

    /**
     * @brief Link a hardware repeater by address.
     * @param server The server the request arrived on.
     */
    void handleLinkRepeater(WebServer &server);

    /**
     * @brief Unlink a hardware repeater by address.
     * @param server The server the request arrived on.
     */
    void handleUnlinkRepeater(WebServer &server);

    /**
     * @brief Link a physical remote to a shade.
     * @param server The server the request arrived on.
     */
    void handleLinkRemote(WebServer &server);

    /**
     * @brief Unlink a physical remote from a shade.
     * @param server The server the request arrived on.
     */
    void handleUnlinkRemote(WebServer &server);
};
