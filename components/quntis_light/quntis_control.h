#pragma once

//
//  QuntisControl - RF controller for Quntis ScreenLinear Monitor LED lamp
//
//  Original Author: ing. A. Vermaning
//  (c) 2023 LEXYINDUSTRIES
//
//  Adapted for ESPHome external component: configurable pins and addresses
//

#include "xn297.h"
#include <string>
#include <vector>

#define TX_REPEAT 6
#define TX_REPEAT_DELAY 5
#define PAYLOAD_LENGTH 6
#define ADDRESS_LENGTH 5

// Payload indices
#define PL_INDEX 4
#define PL_CMD 5

// Quntis Commands
#define QUNTIS_CMD_ONOFF 0x20
#define QUNTIS_CMD_DIM 0x40
#define QUNTIS_CMD_COLOR 0x30
#define QUNTIS_CMD_DOWN 0x8
#define QUNTIS_CMD_UP 0x0

class QuntisControl {
 public:
  QuntisControl();
  ~QuntisControl() { delete _radio; }

  void set_pins(uint8_t ce_pin, uint8_t csn_pin);
  void set_device_address(const std::vector<uint8_t> &addr);
  void set_device_payload(const std::vector<uint8_t> &payload);

  bool begin();

  void OnOff();
  void Dim(bool up, bool repeat = true);
  void Color(bool up, bool repeat = true);

  long GetPacketCount();

  const uint8_t* get_address() const { return _address; }
  const uint8_t* get_payload() const { return _payload; }
  std::string get_rf_info() const;

 private:
  void SendCommand(uint8_t cmd, bool repeat);

  XN297 *_radio{nullptr};
  uint8_t _ce_pin{1};
  uint8_t _csn_pin{5};

  uint8_t _address[ADDRESS_LENGTH] = {0};
  uint8_t _payload[PAYLOAD_LENGTH] = {0};

  uint8_t _index{0};
};
