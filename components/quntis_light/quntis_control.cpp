//
//  QuntisControl - RF controller for Quntis ScreenLinear Monitor LED lamp
//
//  Original Author: ing. A. Vermaning
//  (c) 2023 LEXYINDUSTRIES
//
//  Adapted for ESPHome external component
//
#include "quntis_control.h"

#include "esphome/core/log.h"

static const char* const TAG = "quntis";

QuntisControl::QuntisControl() {}

void QuntisControl::set_pins(uint8_t ce_pin, uint8_t csn_pin) {
    _ce_pin = ce_pin;
    _csn_pin = csn_pin;
}

void QuntisControl::set_device_address(const std::vector<uint8_t>& addr) {
    for (size_t i = 0; i < ADDRESS_LENGTH && i < addr.size(); i++) {
        _address[i] = addr[i];
    }
}

void QuntisControl::set_device_payload(const std::vector<uint8_t>& payload) {
    for (size_t i = 0; i < 4 && i < payload.size(); i++) {
        _payload[i] = payload[i];
    }
}

bool QuntisControl::begin() {
    _index = 0;

    if (_radio) {
        delete _radio;
    }
    _radio = new XN297(_ce_pin, _csn_pin);

    if (!_radio->begin()) {
        ESP_LOGE(TAG, "RF24 initialization failed! Check wiring: CE=%d CSN=%d", _ce_pin, _csn_pin);
        return false;
    }

    _radio->setPALevel(RF24_PA_MAX);
    _radio->setChannel(2);
    _radio->setPayloadSize(11 + 2);
    _radio->disableAckPayload();
    _radio->disableDynamicPayloads();
    _radio->setDataRate(RF24_1MBPS);
    _radio->openWritingPipe(_address);
    _radio->setAutoAck(false);
    _radio->setAddressWidth(5);
    _radio->disableCRC();
    _radio->setRetries(0, 0);

    _radio->XN297_SetTXAddr(_address, ADDRESS_LENGTH);

    ESP_LOGI(TAG, "RF24 initialized: CE=%d CSN=%d", _ce_pin, _csn_pin);
    ESP_LOGI(TAG, "Device address: %02X %02X %02X %02X %02X",
             _address[0], _address[1], _address[2], _address[3], _address[4]);
    ESP_LOGI(TAG, "Device payload prefix: %02X %02X %02X %02X",
             _payload[0], _payload[1], _payload[2], _payload[3]);

    return true;
}

void QuntisControl::OnOff() {
    SendCommand(QUNTIS_CMD_ONOFF, true);
}

void QuntisControl::Dim(bool up, bool repeat) {
    SendCommand(QUNTIS_CMD_DIM | (uint8_t)(up ? QUNTIS_CMD_UP : QUNTIS_CMD_DOWN), repeat);
}

void QuntisControl::Color(bool up, bool repeat) {
    SendCommand(QUNTIS_CMD_COLOR | (uint8_t)(up ? QUNTIS_CMD_UP : QUNTIS_CMD_DOWN), repeat);
}

void QuntisControl::SendCommand(uint8_t cmd, bool repeat) {
    _payload[PL_CMD] = cmd;
    _payload[PL_INDEX] = _index++;

    ESP_LOGD(TAG, "SendCommand cmd=0x%02X idx=%d repeat=%s", cmd, _payload[PL_INDEX], repeat ? "true" : "false");

    if (repeat) {
        for (int i = 0; i < TX_REPEAT; i++) {
            bool ok = _radio->XN297_WritePayload(_payload, PAYLOAD_LENGTH);
            if (i == 0) {
                ESP_LOGD(TAG, "XN297_WritePayload: %s", ok ? "OK" : "FAIL");
            }
            if (i < TX_REPEAT - 1) {
                delayMicroseconds(TX_REPEAT_DELAY * 1000);
                yield();  // Let WiFi/system tasks run
            }
        }
    } else {
        bool ok = _radio->XN297_WritePayload(_payload, PAYLOAD_LENGTH);
        ESP_LOGD(TAG, "XN297_WritePayload: %s", ok ? "OK" : "FAIL");
    }
}

long QuntisControl::GetPacketCount() {
    return _radio ? _radio->GetPacketCount() : 0;
}
