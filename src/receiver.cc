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

    /* XXX your code here */

    /* construct UDP socket */
    UDPSocket udp_socket;

    /* "bind" the socket to host "0", port "0" */
    udp_socket.bind(Address("0", 0));

    /* print out the local address to standard error (std::cerr) */
    /* the output should look something like "0.0.0.0 12345\n" */
    Address local_addr = udp_socket.local_address();
    std::cerr << local_addr.ip() << " " << local_addr.port() << std::endl;

    /* receive one UDP datagram, and print out the payload */
    UDPSocket::received_datagram datagram = udp_socket.recv();
    Address senderAddr = datagram.source_address;
    Buffer payload(datagram.payload, datagram.recvlen);
    const WireFormat::HandshakeReq* req = payload.get<WireFormat::HandshakeReq>(0);

    std::cout << "Sender address: " << senderAddr.to_string() << std::endl;
    std::cout << "Received handshake request " << datagram.recvlen << " bytes." << std::endl;
    std::cout << "fileSize = " << req->fileSize << std::endl;
    std::cout << "OTI_COMMON = " << req->commonData << std::endl;
    std::cout << "OTI_SCHEME_SPECIFIC = " << req->schemeSpecificData << std::endl;

    /* initialize decoder */
    RaptorQ::Decoder<typename std::vector<Alignment>::iterator,
            typename std::vector<Alignment>::iterator>
            decoder(req->commonData, req->schemeSpecificData);

    /* send back HandshakeResp */
    payload.clear();
    WireFormat::HandshakeResp* resp = payload.emplaceAppend<WireFormat::HandshakeResp>();
    std::memcpy(resp->addr, local_addr.ip().c_str(), local_addr.ip().length());
    resp->port = local_addr.port();
    udp_socket.sendbytesto(senderAddr, payload.c_str(), payload.size());

    /* start receiving symbols */
    size_t numOfAlignPerSymbol = static_cast<size_t>(
            std::ceil(static_cast<float>(decoder.symbol_size()) / sizeof(Alignment)));
    std::vector<Alignment> symbol;
    symbol.reserve(numOfAlignPerSymbol);
    symbol.insert(symbol.begin(), numOfAlignPerSymbol, 0);

    std::vector<std::vector<Alignment>> blocks;
    for (uint8_t sbn = 0; sbn < decoder.blocks(); sbn++) {
        blocks.emplace_back();
        std::vector<Alignment>& block = blocks[sbn];
        int numAlignPerBlock = decoder.block_size(sbn) / sizeof(Alignment);
        block.reserve(numAlignPerBlock);
        block.insert(block.begin(), numAlignPerBlock, 0);
    }
    uint8_t numDecodedBlocks = 0;
    while (numDecodedBlocks < decoder.blocks()) {
        datagram = udp_socket.recv();
        assert(datagram.source_address == senderAddr);
        uint32_t id = *reinterpret_cast<uint32_t*>(datagram.payload);
        uint8_t sbn = id >> 24;
        std::cout << "Received sbn = " << static_cast<int>(sbn) << ", esi = " << ((id << 8) >> 8) << std::endl;
        for (size_t i = 0; i < numOfAlignPerSymbol; i++) {
            symbol[i] = *(reinterpret_cast<Alignment*>(datagram.payload) + i);
        }
        auto begin = symbol.begin();
        decoder.add_symbol(begin, symbol.end(), id);

        auto begin2 = blocks[sbn].begin();
        if (decoder.decode(begin2, blocks[sbn].end(), sbn) > 0) {
            /* send ACK for block sbn */
            std::cout << "Block " << static_cast<int>(sbn) << " decoded" << std::endl;
            numDecodedBlocks++;
            payload.clear();
            WireFormat::Ack* ack = payload.emplaceAppend<WireFormat::Ack>();
            (*ack) = {0, 0, 0, 0};
            ack->mask[sbn / 64] = 1 << (sbn % 64);
            udp_socket.sendbytesto(senderAddr, payload.c_str(), payload.size());
        }
    }

    return EXIT_SUCCESS;
}