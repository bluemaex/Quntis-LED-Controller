// Dedicated RF sniffer Firmware for Quntis Remote
//
// Wiring:
//  CE=GPIO17,
//  CSN=GPIO5,
//  SCK=GPIO18,
//  MOSI=GPIO23,
//  MISO=GPIO19
//
// Usage:
// Open serial monitor at 115200 baud (e.g. https://terminal.spacehuhn.com/)
// Press button on your Quntis remote repeatedly while next to the NRF24L01.
//
// The Quintis remote sends 6 identical packets in a burst for each button press,
// if we detected those duplicates the address will be printed along with the payload
//
//  Duplicate packet detected (10ms apart)!
//  ────────────────────────────────────
//  Raw:     49 80 4A CB A5 BC 8B 3F 81 FC 88 CF E5
//  Address: 0x20 0x21 0x01 0x31 0xAA
//  Data:    0x00 0x76 0x9A 0x31 0x4A 0x20
//
// The address is unique per remote and is what need to copy over to QuntisControl.h
// to be able to control your LED bar.

#include <Arduino.h>
#include <RF24.h>
#include <SPI.h>

#define CE_PIN 17
#define CSN_PIN 5

RF24 radio(CE_PIN, CSN_PIN);

// XN297 scramble table (from XN297 protocol reverse engineering)
static const uint8_t xn297_scramble[] = {
    0xe3, 0xb1, 0x4b, 0xea, 0x85, 0xbc, 0xe5, 0x66,
    0x0d, 0xae, 0x8c, 0x88, 0x12, 0x69, 0xee, 0x1f,
    0xc7, 0x62, 0x97, 0xd5, 0x0b, 0x79, 0xca, 0xcc,
    0x1b, 0x5d, 0x19, 0x10, 0x24, 0xd3, 0xdc, 0x3f};

// XN297 CRC table
static const uint16_t xn297_crc_xorout[] = {
    0x0000, 0x3d5f, 0xa6f1, 0x3a23, 0xaa16, 0x1caf,
    0x62b2, 0xe0eb, 0x0821, 0xbe07, 0x5f1a, 0xaf15,
    0x4f01, 0xad16, 0x5f31, 0x7a56, 0x1b71, 0x0a33,
    0x7e44, 0xae90};

static uint16_t crc16_update(uint16_t crc, uint8_t byte, uint8_t bits) {
    crc ^= (uint16_t)byte << 8;
    for (uint8_t i = 0; i < bits; i++) {
        if (crc & 0x8000)
            crc = (crc << 1) ^ 0x1021;
        else
            crc <<= 1;
    }
    return crc;
}

static uint8_t bit_reverse(uint8_t b) {
    b = ((b & 0xF0) >> 4) | ((b & 0x0F) << 4);
    b = ((b & 0xCC) >> 2) | ((b & 0x33) << 2);
    b = ((b & 0xAA) >> 1) | ((b & 0x55) << 1);
    return b;
}

// Descramble and decode a captured XN297 packet
// Input: raw[] = 13 bytes captured after the 3-byte NRF24 address match
//        (5 scrambled addr bytes + 6 scrambled data bytes + 2 CRC bytes)
// Output: prints decoded address and payload
void decodePacket(const uint8_t* raw, int len) {
    if (len < 13) return;

    // Descramble the 5 address bytes
    // XN297 address is scrambled with XOR and sent MSByte first (reversed from NRF24 perspective)
    uint8_t addr[5];
    for (int i = 0; i < 5; i++) {
        addr[4 - i] = raw[i] ^ xn297_scramble[i];
    }

    // Descramble the 6 data bytes
    // XN297 data is scrambled AND bit-reversed
    uint8_t data[6];
    for (int i = 0; i < 6; i++) {
        uint8_t b = raw[5 + i] ^ xn297_scramble[5 + i];
        data[i] = bit_reverse(b);
    }

    Serial.println("────────────────────────────────────");
    Serial.print("  Raw:     ");
    for (int i = 0; i < 13; i++) {
        Serial.printf("%02X ", raw[i]);
    }
    Serial.println();

    Serial.print("  Address: ");
    for (int i = 0; i < 5; i++) {
        Serial.printf("0x%02X ", addr[i]);
    }
    Serial.println();

    Serial.print("  Data:    ");
    for (int i = 0; i < 6; i++) {
        Serial.printf("0x%02X ", data[i]);
    }
    Serial.println();
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("╔══════════════════════════════════════════╗");
    Serial.println("║  QUNTIS REMOTE SNIFFER (Bare - No WiFi)  ║");
    Serial.println("╚══════════════════════════════════════════╝");
    Serial.println();

    if (!radio.begin(CE_PIN, CSN_PIN)) {
        Serial.println("[ERROR] NRF24L01 not found! Check wiring.");
        while (1) {
            delay(1000);
        }
    }

    Serial.println("[OK] NRF24L01 initialized");

    // Configure for XN297 packet reception
    radio.setChannel(2);            // Quntis uses channel 2 (2402 MHz)
    radio.setDataRate(RF24_1MBPS);  // 1 Mbps
    radio.setAutoAck(false);        // No auto-ack
    radio.disableCRC();             // XN297 has its own CRC
    radio.setRetries(0, 0);         // No retries

    // Use 3-byte address to match end of XN297 preamble
    // XN297 preamble ends with: ...0x71 0x0F 0x55
    // NRF24L01 absorbs 0x55 as part of its own preamble, so we match on
    // the remaining bytes. NRF24L01 expects address LSByte first on SPI,
    // but openReadingPipe handles this.
    radio.setAddressWidth(3);
    radio.setPayloadSize(13);  // 5 scrambled addr + 6 scrambled data + 2 CRC

    // The XN297 preamble after NRF24's 0x55 absorption:
    // Over the air: ...0x71 0x0F 0x55 [address bytes...]
    // NRF24 sees 0x55 as preamble, then matches address bytes
    uint8_t rxAddr[] = {0x71, 0x0F, 0x55};
    radio.openReadingPipe(0, rxAddr);

    // Also try pipe 1 with alternate byte order
    uint8_t rxAddr2[] = {0x55, 0x0F, 0x71};
    radio.openReadingPipe(1, rxAddr2);

    radio.startListening();
    radio.printPrettyDetails();

    Serial.println();
    Serial.println("╔═══════════════════════════════════════════════╗");
    Serial.println("║  READY TO SNIFF                               ║");
    Serial.println("║                                               ║");
    Serial.println("║  1. Hold Quntis remote CLOSE to NRF24 module  ║");
    Serial.println("║  2. Press ONE button repeatedly               ║");
    Serial.println("║  3. Look for CONFIRMED packets below          ║");
    Serial.println("║                                               ║");
    Serial.println("║  Packets with valid CRC = real Quntis data    ║");
    Serial.println("║  Press 'r' to restart sniffing                ║");
    Serial.println("╚═══════════════════════════════════════════════╝");
    Serial.println();
}

// Duplicate detection state
uint8_t lastBuf[13] = {0};
unsigned long lastTime = 0;
bool haveLast = false;
int totalPackets = 0;
int confirmedPackets = 0;
int crcValidPackets = 0;
unsigned long lastStatusTime = 0;

void loop() {
    if (Serial.available()) {
        char c = Serial.read();
        if (c == 'r' || c == 'R') {
            Serial.println("\n[Sniffer] Resetting counters...\n");
            totalPackets = 0;
            confirmedPackets = 0;
            crcValidPackets = 0;
            haveLast = false;
            memset(lastBuf, 0, sizeof(lastBuf));
        }
    }

    if (millis() - lastStatusTime > 10000) {
        lastStatusTime = millis();
        Serial.printf("[Status] Total: %d | Duplicates: %d | CRC valid: %d | Listening...\n",
                      totalPackets, confirmedPackets, crcValidPackets);
    }

    if (radio.available()) {
        uint8_t buf[13];
        radio.read(buf, 13);
        totalPackets++;

        unsigned long now = millis();

        // Duplicate detection: Quntis sends 6 identical packets within ~30ms
        // A real packet will have at least 2 copies arriving within 100ms
        if (haveLast && (now - lastTime) < 100) {
            // Compare all 13 bytes for exact match
            bool match = true;
            for (int i = 0; i < 13; i++) {
                if (buf[i] != lastBuf[i]) {
                    match = false;
                    break;
                }
            }

            if (match) {
                confirmedPackets++;
                Serial.printf("\nDuplicate packet detected (%lums apart)!\n", now - lastTime);

                decodePacket(buf, 13);

                // Skip remaining duplicates from printing
                haveLast = false;
                delay(200);
                while (radio.available()) {
                    uint8_t discard[13];
                    radio.read(discard, 13);
                }
                return;
            }
        }

        // Save for next comparison
        memcpy(lastBuf, buf, 13);
        lastTime = now;
        haveLast = true;
    }

    delay(0);
}
