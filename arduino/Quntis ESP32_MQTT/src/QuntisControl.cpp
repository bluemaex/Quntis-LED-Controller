#include <QuntisControl.h>

//=================================================================================================
// QuntisControl
//=================================================================================================
QuntisControl::QuntisControl() {
}

//=================================================================================================
// begin
//=================================================================================================
bool QuntisControl::begin() {
    _index = 0;

    // initialize the transceiver on the SPI bus
    if (!_radio.begin(CE_PIN, CSN_PIN)) {
        return false;
    }

    // Set the PA Level low to try preventing power supply related problems
    // because these examples are likely run with nodes in close proximity to
    // each other.
    _radio.setPALevel(RF24_PA_MAX);  // RF24_PA_MAX is default.

    _radio.setChannel(2);
    // radio.enableDynamicPayloads();

    _radio.setPayloadSize(11 + 2);
    _radio.disableAckPayload();
    _radio.disableDynamicPayloads();

    _radio.setDataRate(RF24_1MBPS);

    // set the TX address of the RX node into the TX pipe
    _radio.openWritingPipe(_address);  // always uses pipe 0

    // set the RX address of the TX node into a RX pipe
    // radio.openReadingPipe(1, address[!radioNumber]);  // using pipe 1

    _radio.setAutoAck(false);
    _radio.setAddressWidth(5);
    _radio.disableCRC();      // XN297 protocol has its own CRC in the payload
    _radio.setRetries(0, 0);  // No retries needed (no auto-ack)

    _radio.XN297_SetTXAddr(_address, ADDRESS_LENGTH);

    // Dump RF24 register configuration for debugging
    _radio.printPrettyDetails();  // prints human readable register data
    return true;
}

//=================================================================================================
// OnOff
//=================================================================================================
void QuntisControl::OnOff() {
    SendCommand(QUNTIS_CMD_ONOFF, true);
}

//=================================================================================================
// Dim
//=================================================================================================
void QuntisControl::Dim(bool up, bool repeat) {
    SendCommand(QUNTIS_CMD_DIM | (byte)(up ? QUNTIS_CMD_UP : QUNTIS_CMD_DOWN), repeat);
}

//=================================================================================================
// Color
//=================================================================================================
void QuntisControl::Color(bool up, bool repeat) {
    SendCommand(QUNTIS_CMD_COLOR | (byte)(up ? QUNTIS_CMD_UP : QUNTIS_CMD_DOWN), repeat);
}

//=================================================================================================
// SendCommand
//=================================================================================================
void QuntisControl::SendCommand(byte cmd, bool repeat) {
    _payload[PL_CMD] = cmd;
    _payload[PL_INDEX] = _index++;

    Serial.printf("[RF] SendCommand cmd=0x%02X idx=%d repeat=%s payload=", cmd, _payload[PL_INDEX], repeat ? "true" : "false");
    for (int i = 0; i < PAYLOAD_LENGTH; i++) {
        Serial.printf("%02X ", _payload[i]);
    }
    Serial.println();

    if (repeat) {
        for (int i = 0; i < TX_REPEAT; i++) {
            bool ok = _radio.XN297_WritePayload(_payload, PAYLOAD_LENGTH);
            if (i == 0) {
                Serial.printf("[RF] XN297_WritePayload result: %s (sent %d/%d repeats)\n", ok ? "OK" : "FAIL", i + 1, TX_REPEAT);
            }
            delay(TX_REPEAT_DELAY);
        }
        Serial.printf("[RF] Sent %d repeats total, packets=%ld\n", TX_REPEAT, _radio.GetPacketCount());
    } else {
        bool ok = _radio.XN297_WritePayload(_payload, PAYLOAD_LENGTH);
        Serial.printf("[RF] XN297_WritePayload result: %s, packets=%ld\n", ok ? "OK" : "FAIL", _radio.GetPacketCount());
    }
}

//=================================================================================================
// ShowNrOfPacketsSend
//=================================================================================================
void QuntisControl::ShowNrOfPacketsSend() {
    Serial.print("Packets send:#");
    Serial.println(_radio.GetPacketCount());
}

//=================================================================================================
// ResetNrOfPacketsSend
//=================================================================================================
void QuntisControl::ResetNrOfPacketsSend() {
    _radio.ResetPacketCount();
}
