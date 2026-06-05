#pragma once

// Web — lifecycle coordinator for the HTTP/REST servers. Owns no routes itself;
// each route lives in a WebHandler module driven by begin()/end() (see Web.cpp).
class Web {
  public:
    void startup();
    void begin();
    void loop();
    void end();
};
