#include <iostream>
#include <RaptorQ.hpp>

#include "tub.hh"
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
    printf("%s\n", udpSocket->local_address().to_string().c_str());

    UDPSocket::received_datagram datagram = udpSocket->recv();
    Address senderAddr = datagram.source_address;
    Tub<WireFormat::HandshakeReq> req(datagram.payload);
    printf("Recevied handshake request: {connection Id = %u, file size = %zu, "
                   "OTI_COMMON = %lu, OTI_SCHEME_SPECIFIC = %u}\n",
           req->connectionId, req->fileSize, req->otiCommon, req->otiScheme);
    sendInWireFormat<WireFormat::HandshakeResp>(
            udpSocket.get(), senderAddr, uint32_t(req->connectionId));

    // Start receiving symbols
    RaptorQDecoder decoder(req->otiCommon, req->otiScheme);
    std::vector<RaptorQBlock> blocks(decoder.blocks());
    for (uint8_t sbn = 0; sbn < decoder.blocks(); sbn++) {
        blocks[sbn] = RaptorQBlock(
                decoder.block_size(sbn) / sizeof(Alignment), 0);
    }

    RaptorQSymbol symbol {0};
    Bitmask256 decodedBlocks;
    while (decodedBlocks.count() < decoder.blocks()) {
        datagram = udpSocket->recv();
        Tub<WireFormat::DataPacket> dataPacket(datagram.payload);
        uint8_t sbn = dataPacket->id >> 24;
        printf("Received sbn = %u, esi = %u\n",
               static_cast<uint32_t>(sbn), ((dataPacket->id << 8) >> 8));
        std::memcpy(symbol.data(), dataPacket->raw, SYMBOL_SIZE);

        Alignment* begin = symbol.begin();
        decoder.add_symbol(begin, symbol.end(), dataPacket->id);

        begin = blocks[sbn].data();
        if (decoder.decode(begin, begin + blocks[sbn].size(), sbn) > 0) {
            // send ACK for block sbn
            printf("Block %u decoded.\n", static_cast<int>(sbn));
            decodedBlocks.set(sbn);
            sendInWireFormat<WireFormat::Ack>(udpSocket.get(), senderAddr,
                    decodedBlocks.bitset);
        }
    }

    // Start teardown phase: keep sending ACK back until we receive
    // teardown request
    assert(decodedBlocks.count() == decoder.blocks());
//    while (true) {
//        sendInWireFormat<WireFormat::Ack>(udpSocket.get(), senderAddr,
//                 decodedBlocks.bitset);
//    }

    // TODO: Temporary hack to avoid "recvmsg: connection refused" on the sender side
    usleep(1000000);
    return EXIT_SUCCESS;
}