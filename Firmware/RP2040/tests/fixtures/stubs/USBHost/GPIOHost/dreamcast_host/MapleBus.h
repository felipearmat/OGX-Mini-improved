// Stands in for the Maple Bus (PIO + DMA) in the fixture replay: the Dreamcast output's bus loop
// never runs on the PC, only its pad -> controller condition conversion is tested.
#pragma once

#include <cstdint>

#include "USBHost/GPIOHost/dreamcast_host/MaplePacket.hpp"
#include "USBHost/GPIOHost/dreamcast_host/maple_config.h"

class MapleBus {
public:
    enum class Phase : uint8_t { IDLE = 0, READ_FAILED, READ_COMPLETE };
    struct Status {
        Phase phase;
        const uint32_t* readBuffer;
        uint32_t readBufferLen;
        MaplePacket::ByteOrder rxByteOrder;
    };
    MapleBus(uint32_t, int32_t = -1, bool = true) {}
    bool write(const MaplePacket&, bool) { return false; }
    bool startRead(uint64_t, MaplePacket::ByteOrder) { return false; }
    Status processEvents(uint64_t) { return Status{Phase::IDLE, nullptr, 0, MaplePacket::ByteOrder::HOST}; }
    bool isBusy() const { return false; }
};
