#include "mqtt_manager.h"

static MqttManager* mqtt_instance = nullptr;

MqttManager::MqttManager(QuntisControl* controller) : _mqtt(_wifi_client), _controller(controller) {
    mqtt_instance = this;

    _config_topic = String(MQTT_DISCOVERY_PREFIX) + "/light/" + MQTT_DEVICE_ID + "/config";
    _state_topic = String(MQTT_DISCOVERY_PREFIX) + "/light/" + MQTT_DEVICE_ID + "/state";
    _command_topic = String(MQTT_DISCOVERY_PREFIX) + "/light/" + MQTT_DEVICE_ID + "/set";
    _availability_topic = String(MQTT_DISCOVERY_PREFIX) + "/light/" + MQTT_DEVICE_ID + "/availability";
}

void MqttManager::begin() {
    loadState();

    _mqtt.setServer(MQTT_BROKER, MQTT_PORT);
    _mqtt.setCallback(messageCallback);
    _mqtt.setBufferSize(1024);  // Default is 256 (too small)

    connect();
}

void MqttManager::loop() {
    if (!_mqtt.connected()) {
        connect();
    }

    _mqtt.loop();
}

bool MqttManager::isConnected() {
    return _mqtt.connected();
}

void MqttManager::connect() {
    while (!_mqtt.connected()) {
        Serial.print("Connecting to MQTT broker...");

        bool connected = false;
        if (strlen(MQTT_USER) > 0) {
            connected = _mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD, _availability_topic.c_str(), 0, true, "offline");
        } else {
            connected = _mqtt.connect(MQTT_CLIENT_ID, _availability_topic.c_str(), 0, true, "offline");
        }

        if (connected) {
            Serial.println(" Connected!");

            publishHomeAssistantDiscovery();
            publishAvailability(true);

            _mqtt.subscribe(_command_topic.c_str());
            Serial.printf("Subscribed to: %s\n", _command_topic.c_str());

            publishState();
        } else {
            Serial.printf(" Failed (rc=%d), retrying in 5s\n", _mqtt.state());
            delay(MQTT_RECONNECT_DELAY_MS);
        }
    }
}

void MqttManager::publishHomeAssistantDiscovery() {
    JsonDocument doc;

    doc["name"] = DEVICE_NAME;
    doc["unique_id"] = MQTT_DEVICE_ID;
    doc["state_topic"] = _state_topic;
    doc["command_topic"] = _command_topic;
    doc["availability_topic"] = _availability_topic;
    doc["schema"] = "json";
    doc["payload_on"] = "ON";
    doc["payload_off"] = "OFF";
    doc["brightness"] = true;
    doc["brightness_scale"] = 100;
    doc["min_mireds"] = 153;  // Coldest (6500K)
    doc["max_mireds"] = 500;  // Warmest (2000K)

    JsonArray modes = doc["supported_color_modes"].to<JsonArray>();
    modes.add("color_temp");

    JsonObject device = doc["device"].to<JsonObject>();
    JsonArray identifiers = device["identifiers"].to<JsonArray>();
    identifiers.add(MQTT_CLIENT_ID);
    device["name"] = DEVICE_NAME;
    device["model"] = DEVICE_MODEL;
    device["manufacturer"] = DEVICE_MANUFACTURER;
    device["sw_version"] = DEVICE_SW_VERSION;

    String payload;
    serializeJson(doc, payload);

    Serial.printf("Discovery payload size: %d bytes\n", payload.length());
    bool ok = _mqtt.publish(_config_topic.c_str(), payload.c_str(), true);
    Serial.printf("Published discovery (%s) to: %s\n", ok ? "OK" : "FAIL", _config_topic.c_str());
}

void MqttManager::publishState() {
    JsonDocument doc;

    doc["state"] = _power_state ? "ON" : "OFF";
    doc["brightness"] = _brightness;
    doc["color_temp"] = _color_temp;
    doc["color_mode"] = "color_temp";

    String payload;
    serializeJson(doc, payload);

    bool ok = _mqtt.publish(_state_topic.c_str(), payload.c_str());
    Serial.printf("Published state (%s): %s\n", ok ? "OK" : "FAIL", payload.c_str());
}

void MqttManager::publishAvailability(bool online) {
    _mqtt.publish(_availability_topic.c_str(), online ? "online" : "offline", true);
}

void MqttManager::messageCallback(char* topic, byte* payload, unsigned int length) {
    if (mqtt_instance) {
        // Null-terminate the payload
        payload[length] = '\0';
        mqtt_instance->handleCommand((char*)payload);
    }
}

void MqttManager::handleCommand(const char* json, bool colorTempInMireds) {
    Serial.printf("Received command: %s (colorTemp in %s)\n", json, colorTempInMireds ? "mireds" : "percent");

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, json);

    if (error) {
        Serial.printf("JSON parse error: %s\n", error.c_str());
        return;
    }

    if (!doc["state"].isNull()) {
        const char* state = doc["state"];
        bool new_power = (strcmp(state, "ON") == 0);
        setPower(new_power);
    }

    if (!doc["brightness"].isNull()) {
        int new_brightness = doc["brightness"];
        setBrightness(new_brightness);
    }

    if (!doc["color_temp"].isNull()) {
        int value = doc["color_temp"];
        int percent = colorTempInMireds ? miredsToPercent(value) : value;
        setColorTemp(percent);
    }

    publishState();
}

void MqttManager::setPower(bool on) {
    Serial.printf("[MQTT] setPower(%s) current_state=%s\n", on ? "ON" : "OFF", _power_state ? "ON" : "OFF");

    _controller->OnOff();
    _power_state = on;

    saveState();
}

void MqttManager::setBrightness(int value) {
    value = constrain(value, 0, 100);
    Serial.printf("[MQTT] setBrightness(%d) current=%d diff=%d\n", value, _brightness, value - _brightness);

    applyBrightness(value);
    _brightness = value;

    saveState();
}

void MqttManager::setColorTemp(int percent) {
    percent = constrain(percent, 0, 100);

    int currentPercent = miredsToPercent(_color_temp);
    int targetSteps = (percent * COLOR_TEMP_STEPS) / 100;
    int currentSteps = (currentPercent * COLOR_TEMP_STEPS) / 100;
    int diff = targetSteps - currentSteps;
    bool direction = (diff < 0);  // true = colder, false = warmer

    Serial.printf("[MQTT] setColorTemp(%d%%) current=%d%% steps=%d->%d diff=%d direction=%s\n",
                  percent, currentPercent, currentSteps, targetSteps, diff, direction ? "colder" : "warmer");

    for (int i = 0; i < abs(diff); i++) {
        _controller->Color(direction, true);
        delay(RF_STEP_DELAY_MS);
    }

    _color_temp = percentToMireds(percent);
    saveState();

    Serial.printf("[MQTT] Color temp set to: %d%% (%d mireds)\n", percent, _color_temp);
}

void MqttManager::applyBrightness(int target) {
    int targetSteps = (target * BRIGHTNESS_STEPS) / 100;
    int currentSteps = (_brightness * BRIGHTNESS_STEPS) / 100;
    int diff = targetSteps - currentSteps;
    bool direction = (diff > 0);  // true = brighter, false = dimmer

    Serial.printf("[MQTT] applyBrightness target=%d%% current=%d%% steps=%d->%d diff=%d direction=%s\n",
                  target, _brightness, currentSteps, targetSteps, diff, direction ? "up" : "down");

    for (int i = 0; i < abs(diff); i++) {
        _controller->Dim(direction, true);
        delay(RF_STEP_DELAY_MS);
    }

    Serial.printf("[MQTT] Brightness applied: %d%%\n", target);
}

int MqttManager::percentToMireds(int percent) {
    return 153 + (percent * (500 - 153) / 100);
}

int MqttManager::miredsToPercent(int mireds) {
    return ((mireds - 153) * 100) / (500 - 153);
}

void MqttManager::loadState() {
    if (!_prefs.begin("quntis", true)) {
        Serial.println("[NVS] No saved state found, using defaults");
        return;
    }
    _power_state = _prefs.getBool("power", false);
    _brightness = _prefs.getInt("brightness", 50);
    _color_temp = _prefs.getInt("color_temp", 250);
    _prefs.end();

    Serial.printf("[NVS] Loaded state: power=%s brightness=%d color_temp=%d mireds\n", _power_state ? "ON" : "OFF", _brightness, _color_temp);
}

void MqttManager::saveState() {
    _prefs.begin("quntis", false);
    _prefs.putBool("power", _power_state);
    _prefs.putInt("brightness", _brightness);
    _prefs.putInt("color_temp", _color_temp);
    _prefs.end();
}
