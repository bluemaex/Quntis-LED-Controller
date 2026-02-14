//
//	QuntisControl.cpp
//
//	    Remote controller for Quntis ScreenLinear Monitor LED lamp
//
//	Author(s)
//	ing. A. Vermaning (original)
//
//=================================================================================================
//	(c) 2023 LEXYINDUSTRIES
//=================================================================================================
#include <xn297.h>

// ESP32-C3 Super Mini (lolin_c3_mini) SPI pins for NRF24L01:
// GND  → GND    (black)
// VCC  → 3.3V   (gray)
// SCK  → GPIO 2 (red)
// MOSI → GPIO 4 (violet)
// MISO → GPIO 3 (orange)
// CSN  → GPIO 5 (white)
// CE   → GPIO 1 (brown)
#define CE_PIN 1
#define CSN_PIN 5

#define TX_REPEAT 6
#define TX_REPEAT_DELAY 5
#define PAYLOAD_LENGTH 6
#define ADDRESS_LENGTH 5

// index in _payload
#define PL_INDEX 4
#define PL_CMD 5

//
// Quntis Commands
//
#define QUNTIS_CMD_ONOFF 0x20
#define QUNTIS_CMD_DIM 0x40
#define QUNTIS_CMD_COLOR 0x30
#define QUNTIS_CMD_DOWN 0x8  //  OR with cmd to go down
#define QUNTIS_CMD_UP 0x0

//=================================================================================================
//	QuntisControl
//=================================================================================================
#ifndef QUNTISCONTROL_H
#define QUNTISCONTROL_H

class QuntisControl {
   public:
    QuntisControl();

    bool begin();

    void OnOff();
    void Dim(bool up, bool repeat = true);
    void Color(bool up, bool repeat = true);

    void ShowNrOfPacketsSend();
    void ResetNrOfPacketsSend();

   private:
    void SendCommand(byte cmd, bool repeat);

   private:
    XN297 _radio;

    byte _address[ADDRESS_LENGTH] = {0x20, 0x21, 0x01, 0x31, 0xAA};

    // The "fixed" payload is unique for each Quntis lamp, please find out your own
    // by sniffing with the original remote control with either a logic analyzer or
    // with the sniffer firmware in this repo that might help you to get started
    //                               |------ Fixed -------| |Idx|  |cmd|
    byte _payload[PAYLOAD_LENGTH] = {0x00, 0x76, 0x9A, 0x31, 0x00, 0x00};

    byte _index;
};

#endif