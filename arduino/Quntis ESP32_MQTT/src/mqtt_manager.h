#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <ArduinoJson.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <WiFi.h>

#include "QuntisControl.h"
#include "config.h"

class MqttManager {
   public:
    MqttManager(QuntisControl* controller);
    void begin();
    void loop();
    bool isConnected();

    // State publishing
    void publishState();
    void publishAvailability(bool online);

    // Getters for current state
    bool getPowerState() { return _power_state; }
    int getBrightness() { return _brightness; }
    int getColorTemp() { return _color_temp; }
    int getColorTempPercent() { return miredsToPercent(_color_temp); }

    // Setters (called from web UI)
    void setPower(bool on);
    void setBrightness(int value);
    void setColorTemp(int value);

    // Command handling (colorTempInMireds: true for MQTT/HA, false for WebUI)
    void handleCommand(const char* json, bool colorTempInMireds = true);

   private:
    PubSubClient _mqtt;
    WiFiClient _wifi_client;
    QuntisControl* _controller;
    Preferences _prefs;

    // Current state
    bool _power_state = false;
    int _brightness = 50;   // 0-100
    int _color_temp = 250;  // Mireds (153-500)

    // MQTT topics (built dynamically)
    String _config_topic;
    String _state_topic;
    String _command_topic;
    String _availability_topic;

   private:
    void connect();
    void publishHomeAssistantDiscovery();
    static void messageCallback(char* topic, byte* payload, unsigned int length);
    void applyBrightness(int target);
    void applyColorTemp(int target);
    void saveState();
    void loadState();
    int percentToMireds(int percent);  // 0-100% → 153-500 mireds
    int miredsToPercent(int mireds);   // 153-500 mireds → 0-100%
};

#endif
