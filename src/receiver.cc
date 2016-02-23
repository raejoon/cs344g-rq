#include <iostream>
#include <RaptorQ.hpp>

#include "buffer.hh"
#include "common.hh"
#include "socket.hh"
#include "wire_format.hh"

int main( int argc, char *argv[] )
{
    /* check the command-line arguments */
    if ( argc < 1 ) { abort(); } /* for sticklers */

    if ( argc != 1 ) {
        std::cerr << "Usage: " << argv[ 0 ] << std::endl;
        return EXIT_FAILURE;
    }

    // Wait for handshake request and send back handshake response
    std::unique_ptr<UDPSocket> udpSocket{new UDPSocket};
    udpSocket->bind(Address("0", 0));

    UDPSocket::received_datagram datagram = udpSocket->recv();
    Address senderAddr = datagram.source_address;
    Buffer recvBuffer(datagram.payload, datagram.recvlen);
    const WireFormat::HandshakeReq* req = recvBuffer.get<WireFormat::HandshakeReq>(0);
    printf("Sending handshake request: {connection Id = %u, file size = %zu, "
                   "OTI_COMMON = %lu, OTI_SCHEME_SPECIFIC = %u}\n",
           req->connectionId, req->fileSize, req->otiCommon, req->otiScheme);

    RaptorQDecoder decoder(req->otiCommon, req->otiScheme);

    Buffer sendBuffer;
    sendBuffer.emplaceAppend<WireFormat::HandshakeResp>(req->connectionId);
    udpSocket->sendbytesto(senderAddr, sendBuffer.c_str(), sendBuffer.size());

    // Start receiving symbols
    RaptorQSymbol symbol(NUM_ALIGN_PER_SYMBOL, 0);

    std::vector<std::vector<Alignment>> blocks(decoder.blocks());
//    for (auto& block : blocks) {
//        block.insert(block.begin(), )
//    }

    for (uint8_t sbn = 0; sbn < decoder.blocks(); sbn++) {
        int numAlignPerBlock = decoder.block_size(sbn) / sizeof(Alignment);
        blocks[sbn].insert(blocks[sbn].begin(), numAlignPerBlock, 0);
    }

    Bitmask256 decodedBlocks;
    while (decodedBlocks.count() < decoder.blocks()) {
        datagram = udpSocket->recv();
        recvBuffer.set(datagram.payload, datagram.recvlen);
        const WireFormat::DataPacket* dataPacket =
                recvBuffer.get<WireFormat::DataPacket>(0);
        uint8_t sbn = dataPacket->id >> 24;
        printf("Received sbn = %u, esi = %u\n",
               static_cast<uint32_t>(sbn), ((dataPacket->id << 8) >> 8));
        std::memcpy(symbol.data(), dataPacket->raw, SYMBOL_SIZE);

        auto begin = symbol.begin();
        decoder.add_symbol(begin, symbol.end(), dataPacket->id);

        auto begin2 = blocks[sbn].begin();
        if (decoder.decode(begin2, blocks[sbn].end(), sbn) > 0) {
            // send ACK for block sbn
            std::cout << "Block " << static_cast<int>(sbn) << " decoded" << std::endl;
            decodedBlocks.set(sbn);
            sendBuffer.clear();
            sendBuffer.emplaceAppend<WireFormat::Ack>(decodedBlocks.bitset);
            udpSocket->sendbytesto(senderAddr, sendBuffer.c_str(), sendBuffer.size());
        }
    }

    // TODO: Temporary hack to avoid "recvmsg: connection refused" on the sender side
    usleep(1000000);
    return EXIT_SUCCESS;
}