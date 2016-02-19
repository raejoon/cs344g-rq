#ifndef WIREFORMAT_HH
#define WIREFORMAT_HH

#include <RaptorQ.hpp>

namespace WireFormat {

struct HandshakeReq {
    size_t fileSize;
    RaptorQ::OTI_Common_Data commonData;
    RaptorQ::OTI_Scheme_Specific_Data schemeSpecificData;
} __attribute__((packed));

struct Ack {
    // RaptorQ supports at most 256 blocks
    uint64_t mask0;
    uint64_t mask1;
    uint64_t mask2;
    uint64_t mask3;
} __attribute__((packed));

}

#endif /* WIREFORMAT_HH */
