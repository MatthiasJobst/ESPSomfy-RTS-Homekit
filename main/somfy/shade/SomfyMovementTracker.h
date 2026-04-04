#pragma once

class SomfyShade;

class SomfyMovementTracker {
public:
    SomfyShade *shade = nullptr;
    void checkMovement();
    void setMovement(int8_t dir);
    void setTiltMovement(int8_t dir);
};
