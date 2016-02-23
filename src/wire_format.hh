#ifndef WIREFORMAT_HH
#define WIREFORMAT_HH

#include <RaptorQ.hpp>

#include "common.hh"

namespace WireFormat {

enum class Opcode : uint8_t {
    HANDSHAKE_REQ       = 5,
    HANDSHAKE_RESP      = 6,
    DATA_PACKET         = 7,
    ACK                 = 8,
};

struct Header {
    Opcode opcode;
};

struct HandshakeReq {
    Header header;
    uint32_t connectionId;
    size_t fileSize;
    RaptorQ::OTI_Common_Data otiCommon;
    RaptorQ::OTI_Scheme_Specific_Data otiScheme;

    HandshakeReq(uint32_t connectionId,
                 size_t fileSize,
                 RaptorQ::OTI_Common_Data otiCommon,
                 RaptorQ::OTI_Scheme_Specific_Data otiScheme)
        : header {Opcode::HANDSHAKE_REQ}
        , connectionId(connectionId)
        , fileSize(fileSize)
        , otiCommon(otiCommon)
        , otiScheme(otiScheme)
    {}
} __attribute__((packed));

struct HandshakeResp {
    Header header;
    uint32_t connectionId;

    HandshakeResp(uint32_t connectionId)
        : header {Opcode::HANDSHAKE_RESP}
        , connectionId(connectionId)
    {}
} __attribute__((packed));

struct DataPacket {
    Header header;
    uint32_t id;
    char raw[SYMBOL_SIZE];

    DataPacket(uint32_t id, void* data)
        : header {Opcode::DATA_PACKET}
        , id(id)
    {
        std::memcpy(raw, data, SYMBOL_SIZE);
    }
} __attribute__((packed));

struct Ack {
    Header header;

    // RaptorQ supports at most 256 blocks
    uint64_t bitmask[4];

    Ack(std::bitset<64> bitset[4])
        : header {Opcode::ACK}
        , bitmask {bitset[0].to_ullong(),
                   bitset[1].to_ullong(),
                   bitset[2].to_ullong(),
                   bitset[3].to_ullong()}
    {}
} __attribute__((packed));

}

#endif /* WIREFORMAT_HH */
