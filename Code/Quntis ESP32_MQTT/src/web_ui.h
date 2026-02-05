#ifndef WEB_UI_H
#define WEB_UI_H

#include <WebServer.h>
#include <SPIFFS.h>
#include "config.h"
#include "mqtt_manager.h"

class WebUI {
public:
    WebUI(MqttManager* mqtt);
    void begin();
    void handleClient();

private:
    WebServer _server;
    MqttManager* _mqtt;

    bool checkAuth();
    void sendState();
    void handleRoot();
    void handleStatus();
    void handleSet();
    void handleInfo();
    void handleNotFound();
};

#endif
