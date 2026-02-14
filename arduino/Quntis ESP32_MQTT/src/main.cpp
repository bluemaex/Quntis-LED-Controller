//
//	main_esp32.cpp
//
//      Quntis Monitor Light Bar Remote Controller - ESP32 + WiFi + Home Assistant
//      WiFi-enabled controller with MQTT auto-discovery for Home Assistant
//      Control via Home Assistant or minimal web UI (backup)
//
//	Author(s)
//	ing. A. Vermaning (original)
//	bluemaex - Modified for ESP32 (WiFi + Home Assistant integration)
//
//=================================================================================================
//	(c) 2023 LEXYINDUSTRIES
//=================================================================================================
#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#include "QuntisControl.h"
#include "config.h"
#include "mqtt_manager.h"
#include "web_ui.h"

QuntisControl quntis;
MqttManager* mqttManager = nullptr;
WebUI* webUI = nullptr;

void setupWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(WIFI_HOSTNAME);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    Serial.printf("Connecting to WiFi '%s'\n", WIFI_SSID);

    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < WIFI_CONNECT_TIMEOUT_MS) {
        Serial.print(".");
        delay(500);
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println(" ✓ Connected!");
        Serial.printf("IP Address: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("Hostname: %s.local\n", WIFI_HOSTNAME);
    } else {
        Serial.println(" ✗ FAILED!");
        Serial.println("ERROR: WiFi Failed - Check credentials");
        while (1) delay(1000);
    }
}

void setupMDNS() {
    if (MDNS.begin(MDNS_NAME)) {
        Serial.printf("mDNS started: http://%s.local\n", MDNS_NAME);

        MDNS.addService("http", "tcp", HTTP_PORT);
        MDNS.addService("mqtt", "tcp", MQTT_PORT);
    } else {
        Serial.println("ERROR: mDNS failed to start");
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n");
    Serial.println("╔════════════════════════════════════╗");
    Serial.println("║  Quntis LED Controller v1.0        ║");
    Serial.println("║  ESP32 + NRF24L01                  ║");
    Serial.println("╚════════════════════════════════════╝");
    Serial.println("\n");

    Serial.println("\n[RF24 Init]");
    if (!quntis.begin()) {
        Serial.println("✗ ERROR: RF24 initialization failed!");
        Serial.println("Check NRF24L01 wiring and power");
        while (1) delay(1000);
    }

    Serial.println("✓ RF24 initialized");

    setupWiFi();
    setupMDNS();

    Serial.println("\n[MQTT Init]");
    mqttManager = new MqttManager(&quntis);
    mqttManager->begin();

    Serial.println("\n[Web UI Init]");
    webUI = new WebUI(mqttManager);
    webUI->begin();

    Serial.println("\n");
    Serial.println("╔════════════════════════════════════╗");
    Serial.println("║  ✓ System Ready                    ║");
    Serial.println("╚════════════════════════════════════╝");
    Serial.printf("Home Assistant: Discovery published\n");
    Serial.printf("Web UI: http://%s.local\n", MDNS_NAME);
    Serial.println("Serial: Send '?' for command help\n");
}

void handleSerialCommands() {
    if (Serial.available()) {
        char c = Serial.read();
        switch (c) {
            case 'o':
                Serial.println("[Serial] Sending ON/OFF");
                quntis.OnOff();
                break;
            case '+':
                Serial.println("[Serial] Sending DIM UP");
                quntis.Dim(true, true);
                break;
            case '-':
                Serial.println("[Serial] Sending DIM DOWN");
                quntis.Dim(false, true);
                break;
            case 'w':
                Serial.println("[Serial] Sending COLOR WARMER");
                quntis.Color(false, true);  // DOWN = 0x38 = warmer/orange
                break;
            case 'c':
                Serial.println("[Serial] Sending COLOR COLDER");
                quntis.Color(true, true);  // UP = 0x30 = colder/white
                break;
            case 'p':
                Serial.println("[Serial] Packet count:");
                quntis.ShowNrOfPacketsSend();
                break;
            case '?':
                Serial.println("\n[Serial Commands]");
                Serial.println("  o  = On/Off toggle");
                Serial.println("  +  = Dim up");
                Serial.println("  -  = Dim down");
                Serial.println("  w  = Color warmer");
                Serial.println("  c  = Color colder");
                Serial.println("  p  = Show packet count");
                Serial.println("  ?  = Show this help");
                break;
            default:
                break;
        }
    }
}

void loop() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi disconnected, reconnecting...");
        setupWiFi();
    }

    if (mqttManager) {
        mqttManager->loop();
    }

    if (webUI) {
        webUI->handleClient();
    }

    handleSerialCommands();
    delay(10);
}
