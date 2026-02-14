//
//  xn297.cpp
//
//      Emulate the simple/normal XN297 protocol using an RF24L01
//
//      Based on https://github.com/goebish/nrf24_multipro : XN297_emu.ino
//      Uses the RF24 arduino library (https://github.com/nRF24/RF24)
//
//  Original Author: ing. A. Vermaning
//  (c) 2023 LEXYINDUSTRIES
//
//  Adapted for ESPHome external component
//
#include "xn297.h"

static const char* const TAG = "xn297";

static uint8_t xn297_addr_len;
static uint8_t xn297_tx_addr[5];
static uint8_t xn297_rx_addr[5];
static uint8_t xn297_crc = 1;

static const uint8_t xn297_scramble[] = {
    0xe3, 0xb1, 0x4b, 0xea, 0x85, 0xbc, 0xe5, 0x66,
    0x0d, 0xae, 0x8c, 0x88, 0x12, 0x69, 0xee, 0x1f,
    0xc7, 0x62, 0x97, 0xd5, 0x0b, 0x79, 0xca, 0xcc,
    0x1b, 0x5d, 0x19, 0x10, 0x24, 0xd3, 0xdc, 0x3f,
    0x8e, 0xc5, 0x2f};

static const uint16_t xn297_crc_xorout[] = {
    0x0000, 0x3448, 0x9BA7, 0x8BBB, 0x85E1, 0x3E8C,
    0x451E, 0x18E6, 0x6B24, 0xE7AB, 0x3828, 0x814B,
    0xD461, 0xF494, 0x2503, 0x691D, 0xFE8B, 0x9BA7,
    0x8B17, 0x2920, 0x8B5F, 0x61B1, 0xD391, 0x7401,
    0x2138, 0x129F, 0xB3A0, 0x2988};

static const uint16_t polynomial = 0x1021;
static const uint16_t initial = 0xb5d2;

XN297::XN297(rf24_gpio_pin_t _cepin, rf24_gpio_pin_t _cspin) : RF24(_cepin, _cspin) {
    _nrOfPackets = 0;
}

uint8_t XN297::bit_reverse(uint8_t b_in) {
    uint8_t b_out = 0;
    for (uint8_t i = 0; i < 8; ++i) {
        b_out = (b_out << 1) | (b_in & 1);
        b_in >>= 1;
    }
    return b_out;
}

uint16_t XN297::crc16_update(uint16_t crc, unsigned char a) {
    crc ^= a << 8;
    for (uint8_t i = 0; i < 8; ++i) {
        if (crc & 0x8000) {
            crc = (crc << 1) ^ polynomial;
        } else {
            crc = crc << 1;
        }
    }
    return crc;
}

void XN297::XN297_SetTXAddr(const uint8_t* addr, uint8_t len) {
    if (len > 5) len = 5;
    if (len < 3) len = 3;
    uint8_t buf[] = {0x55, 0x0F, 0x71, 0x0C, 0x00};
    xn297_addr_len = len;
    if (xn297_addr_len < 4) {
        for (uint8_t i = 0; i < 4; ++i) {
            buf[i] = buf[i + 1];
        }
    }

    write_register(SETUP_AW, len - 2);
    write_register(TX_ADDR, buf, 5);

    memcpy(xn297_tx_addr, addr, len);
}

uint8_t XN297::XN297_WritePayload(uint8_t* msg, uint8_t len) {
    uint8_t buf[32];
    uint8_t res;
    uint8_t last = 0;
    if (xn297_addr_len < 4) {
        buf[last++] = 0x55;
    }
    for (uint8_t i = 0; i < xn297_addr_len; ++i) {
        buf[last++] = xn297_tx_addr[xn297_addr_len - i - 1] ^ xn297_scramble[i];
    }

    for (uint8_t i = 0; i < len; ++i) {
        uint8_t b_out = bit_reverse(msg[i]);
        buf[last++] = b_out ^ xn297_scramble[xn297_addr_len + i];
    }
    if (xn297_crc) {
        uint8_t offset = xn297_addr_len < 4 ? 1 : 0;
        uint16_t crc = initial;
        for (uint8_t i = offset; i < last; ++i) {
            crc = crc16_update(crc, buf[i]);
        }
        crc ^= xn297_crc_xorout[xn297_addr_len - 3 + len];
        buf[last++] = crc >> 8;
        buf[last++] = crc & 0xff;
    }
    res = write(buf, last);
    _nrOfPackets++;
    return res;
}
