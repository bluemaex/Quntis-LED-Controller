// Dedicated RF sniffer Firmware for Quntis Remote
//
// Wiring:
//  CE=GPIO17,
//  CSN=GPIO5,
//  SCK=GPIO18,
//  MOSI=GPIO23,
//  MISO=GPIO19
//  ESP32-C3 wiring is listed in the project README.
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

#if CONFIG_IDF_TARGET_ESP32C3
#define CE_PIN 1
#else
#define CE_PIN 17
#endif
#define CSN_PIN 5

RF24 radio(CE_PIN, CSN_PIN);

constexpr uint8_t PRO_CHANNEL = 2;
constexpr uint8_t GLOW_CHANNEL = 19;
constexpr uint8_t ADDRESS_SIZE = 5;
constexpr uint8_t PRO_DATA_SIZE = 6;
constexpr uint8_t GLOW_DATA_SIZE = 11;
constexpr uint8_t GLOW_FRAME_SIZE = ADDRESS_SIZE + GLOW_DATA_SIZE + 2;
bool glowMode = false;

// XN297 scramble table (from XN297 protocol reverse engineering)
static const uint8_t xn297_scramble[] = {
    0xe3, 0xb1, 0x4b, 0xea, 0x85, 0xbc, 0xe5, 0x66,
    0x0d, 0xae, 0x8c, 0x88, 0x12, 0x69, 0xee, 0x1f,
    0xc7, 0x62, 0x97, 0xd5, 0x0b, 0x79, 0xca, 0xcc,
    0x1b, 0x5d, 0x19, 0x10, 0x24, 0xd3, 0xdc, 0x3f};

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
void decodePacket(const uint8_t* raw, uint8_t dataSize) {
    // Descramble the 5 address bytes
    // XN297 address is scrambled with XOR and sent MSByte first (reversed from NRF24 perspective)
    uint8_t addr[5];
    for (int i = 0; i < 5; i++) {
        addr[4 - i] = raw[i] ^ xn297_scramble[i];
    }

    // XN297 data is scrambled and bit-reversed.
    uint8_t data[GLOW_DATA_SIZE];
    for (uint8_t i = 0; i < dataSize; i++) {
        uint8_t b = raw[5 + i] ^ xn297_scramble[5 + i];
        data[i] = bit_reverse(b);
    }

    Serial.println("────────────────────────────────────");
    Serial.print("  Raw:     ");
    for (uint8_t i = 0; i < ADDRESS_SIZE + dataSize + 2; i++) {
        Serial.printf("%02X ", raw[i]);
    }
    Serial.println();

    Serial.print("  Address: ");
    for (int i = 0; i < 5; i++) {
        Serial.printf("0x%02X ", addr[i]);
    }
    Serial.println();

    Serial.print("  Data:    ");
    for (uint8_t i = 0; i < dataSize; i++) {
        Serial.printf("0x%02X ", data[i]);
    }
    Serial.println();
}

bool validGlowFrame(const uint8_t* frame) {
    // XN297 CRC covers the scrambled address and scrambled data.
    constexpr uint8_t crcOffset = ADDRESS_SIZE + GLOW_DATA_SIZE;
    uint16_t crc = 0xb5d2;
    for (uint8_t i = 0; i < crcOffset; ++i) {
        crc = crc16_update(crc, frame[i], 8);
    }
    const uint16_t received = (static_cast<uint16_t>(frame[crcOffset]) << 8) | frame[crcOffset + 1];
    if ((crc ^ 0xf494) != received) return false;

    uint8_t sum = 0;
    for (uint8_t i = ADDRESS_SIZE; i < crcOffset - 1; ++i) {
        sum += bit_reverse(frame[i] ^ xn297_scramble[i]);
    }
    return sum == bit_reverse(frame[crcOffset - 1] ^ xn297_scramble[crcOffset - 1]);
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("╔══════════════════════════════════════════╗");
    Serial.println("║  QUNTIS REMOTE SNIFFER (Bare - No WiFi)  ║");
    Serial.println("╚══════════════════════════════════════════╝");
    Serial.println();

#if CONFIG_IDF_TARGET_ESP32C3
    SPI.begin(6, 3, 4, CSN_PIN);
    const bool radioReady = radio.begin(&SPI);
#else
    const bool radioReady = radio.begin(CE_PIN, CSN_PIN);
#endif
    if (!radioReady) {
        Serial.println("[ERROR] NRF24L01 not found! Check wiring.");
        while (1) {
            delay(1000);
        }
    }

    Serial.println("[OK] NRF24L01 initialized");

    // Configure for XN297 packet reception
    radio.setChannel(PRO_CHANNEL); // ScreenLinear Pro+
    radio.setDataRate(RF24_1MBPS);  // 1 Mbps
    radio.setAutoAck(false);        // No auto-ack
    radio.disableCRC();             // XN297 has its own CRC
    radio.setRetries(0, 0);         // No retries
    radio.disableDynamicPayloads();

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
    Serial.println("║  Glow frames are checked with XN297 CRC      ║");
    Serial.println("║  Press 'r' to restart sniffing                ║");
    Serial.println("╚═══════════════════════════════════════════════╝");
    Serial.println("g: Glow White (channel 19), p: Pro+ (channel 2)");
    Serial.println();
}

// Duplicate detection state
uint8_t lastBuf[GLOW_FRAME_SIZE] = {0};
unsigned long lastTime = 0;
bool haveLast = false;
int totalPackets = 0;
int confirmedPackets = 0;
int crcValidPackets = 0;
unsigned long lastStatusTime = 0;

void setCaptureProfile(bool glow) {
    glowMode = glow;
    radio.stopListening();
    radio.setChannel(glowMode ? GLOW_CHANNEL : PRO_CHANNEL);
    radio.setPayloadSize(glowMode ? GLOW_FRAME_SIZE : ADDRESS_SIZE + PRO_DATA_SIZE + 2);
    radio.flush_rx();
    radio.startListening();
    Serial.printf("[Sniffer] %s on channel %u\n", glowMode ? "Glow White" : "Pro+", radio.getChannel());
}

void loop() {
    if (Serial.available()) {
        char c = Serial.read();
        if (c == 'g' || c == 'p') {
            setCaptureProfile(c == 'g');
        }
        if (c == 'r' || c == 'R' || c == 'g' || c == 'p') {
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
        const uint8_t dataSize = glowMode ? GLOW_DATA_SIZE : PRO_DATA_SIZE;
        const uint8_t frameSize = ADDRESS_SIZE + dataSize + 2;
        uint8_t buf[GLOW_FRAME_SIZE];
        radio.read(buf, frameSize);
        totalPackets++;

        unsigned long now = millis();

        if (glowMode) {
            if (validGlowFrame(buf)) {
                ++crcValidPackets;
                if (!haveLast || memcmp(buf, lastBuf, frameSize) != 0 || now - lastTime >= 100) {
                    decodePacket(buf, dataSize);
                } else {
                    ++confirmedPackets;
                }
                memcpy(lastBuf, buf, frameSize);
                lastTime = now;
                haveLast = true;
            }
            return;
        }

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

                decodePacket(buf, dataSize);

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
        memcpy(lastBuf, buf, frameSize);
        lastTime = now;
        haveLast = true;
    }

    delay(0);
}
