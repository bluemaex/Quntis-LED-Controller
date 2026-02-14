#include "web_ui.h"

#include <ArduinoJson.h>

WebUI::WebUI(MqttManager* mqtt) : _server(HTTP_PORT), _mqtt(mqtt) {}

bool WebUI::checkAuth() {
    if (strlen(WEB_PASSWORD) == 0) {
        return true;
    }

    if (!_server.authenticate(WEB_USERNAME, WEB_PASSWORD)) {
        _server.requestAuthentication();
        return false;
    }

    return true;
}

void WebUI::sendState() {
    JsonDocument doc;
    doc["state"] = _mqtt->getPowerState() ? "ON" : "OFF";
    doc["brightness"] = _mqtt->getBrightness();
    doc["color_temp"] = _mqtt->getColorTempPercent();

    String response;
    serializeJson(doc, response);

    _server.send(200, "application/json", response);
}

void WebUI::begin() {
    if (!SPIFFS.begin(true)) {
        Serial.println("ERROR: SPIFFS mount failed!");
        Serial.println("Web UI will not be available.");
        return;
    }

    Serial.println("✓ SPIFFS mounted");

    _server.on("/", HTTP_GET, [this]() { handleRoot(); });
    _server.on("/api/status", HTTP_GET, [this]() { handleStatus(); });
    _server.on("/api/info", HTTP_GET, [this]() { handleInfo(); });
    _server.on("/api/set", HTTP_POST, [this]() { handleSet(); });
    _server.onNotFound([this]() { handleNotFound(); });
    _server.begin();

    Serial.printf("Web UI started: http://%s.local\n", MDNS_NAME);

    if (strlen(WEB_PASSWORD) > 0) {
        Serial.printf("Authentication: Enabled (username: %s)\n", WEB_USERNAME);
    } else {
        Serial.println("Authentication: Disabled (no password set)");
    }
}

void WebUI::handleClient() {
    _server.handleClient();
}

void WebUI::handleRoot() {
    if (!checkAuth()) {
        return;
    }

    if (SPIFFS.exists("/index.html")) {
        File file = SPIFFS.open("/index.html", "r");
        if (file) {
            _server.streamFile(file, "text/html");
            file.close();
            return;
        }
    }

    _server.send(404, "text/plain", "index.html not found in SPIFFS. Please upload filesystem: pio run -e esp32dev -t uploadfs");
}

void WebUI::handleStatus() {
    if (!checkAuth()) {
        return;
    }

    sendState();
}

void WebUI::handleInfo() {
    if (!checkAuth()) {
        return;
    }

    JsonDocument doc;
    doc["name"] = DEVICE_NAME;
    doc["version"] = DEVICE_SW_VERSION;
    doc["model"] = DEVICE_MODEL;
    doc["manufacturer"] = DEVICE_MANUFACTURER;

    String response;
    serializeJson(doc, response);

    _server.send(200, "application/json", response);
}

void WebUI::handleSet() {
    Serial.println("[WebUI] POST /api/set");

    if (!checkAuth()) {
        return;
    }

    if (!_server.hasArg("plain")) {
        Serial.println("[WebUI] ERROR: Missing request body");
        _server.send(400, "application/json", "{\"error\":\"Missing body\"}");
        return;
    }

    String body = _server.arg("plain");
    _mqtt->handleCommand(body.c_str(), false);
    sendState();
}

void WebUI::handleNotFound() {
    String message = "File Not Found\n\n";
    message += "URI: " + _server.uri() + "\n";
    message += "Method: " + String((_server.method() == HTTP_GET) ? "GET" : "POST") + "\n";

    _server.send(404, "text/plain", message);
}
