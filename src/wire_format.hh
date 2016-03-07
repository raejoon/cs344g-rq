#ifndef WIREFORMAT_HH
#define WIREFORMAT_HH

#include <RaptorQ.hpp>

#include "common.hh"

namespace WireFormat {

enum Opcode : uint8_t {
    EMPTY               = 0, 
    HANDSHAKE_REQ       = 5,
    HANDSHAKE_RESP      = 6,
    DATA_PACKET         = 7,
    ACK                 = 8,
};

struct Header {
    Opcode opcode;
};

Opcode getOpcode(char* datagram) {
    if (datagram)
        return *((Opcode*)datagram);
    else
        return EMPTY;
}

struct HandshakeReq {
    Header header;
    uint32_t connectionId;
    char fileName[MAX_FILENAME_LEN];
    size_t fileSize;
    RaptorQ::OTI_Common_Data otiCommon;
    RaptorQ::OTI_Scheme_Specific_Data otiScheme;

    HandshakeReq(uint32_t connectionId,
                 const char* fileName,
                 size_t fileSize,
                 RaptorQ::OTI_Common_Data otiCommon,
                 RaptorQ::OTI_Scheme_Specific_Data otiScheme)
        : header {HANDSHAKE_REQ}
        , connectionId(connectionId)
        , fileSize(fileSize)
        , otiCommon(otiCommon)
        , otiScheme(otiScheme)
    {
        std::strcpy(this->fileName, fileName);
    }
} __attribute__((packed));

struct HandshakeResp {
    Header header;
    uint32_t connectionId;

    HandshakeResp(uint32_t connectionId)
        : header {HANDSHAKE_RESP}
        , connectionId(connectionId)
    {}
} __attribute__((packed));

struct DataPacket {
    Header header;
    uint32_t id;
    char raw[SYMBOL_SIZE];

    DataPacket(uint32_t id, void* data)
        : header {DATA_PACKET}
        , id(id)
    {
        std::memcpy(raw, data, SYMBOL_SIZE);
    }
} __attribute__((packed));

struct Ack {
    Header header;

    // RaptorQ supports at most 256 blocks
    uint64_t bitmask[4];

    // How many source symbols should be sent before start sending another
    // round of repair symbols for previous blocks; this parameter is derived
    // from the packet loss rate observed by the receiver during the
    // transmission of the last decoded block.
    uint32_t repairSymbolInterval;

    Ack(std::bitset<64> bitset[4], uint32_t repairSymbolInterval)
        : header {ACK}
        , bitmask {bitset[0].to_ullong(),
                   bitset[1].to_ullong(),
                   bitset[2].to_ullong(),
                   bitset[3].to_ullong()}
        , repairSymbolInterval(repairSymbolInterval)
    {}
} __attribute__((packed));

}

#endif /* WIREFORMAT_HH */
