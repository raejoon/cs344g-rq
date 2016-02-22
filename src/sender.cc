/*
  udp-sender

  Given a host and port on the command line,
  this program should:

  (1) Construct a UDP socket
  (2) Connect the socket to the given host and port
  (3) Use the socket to send a payload of the
      string: "Hello, world."
*/

#include <iostream>
#include <fstream>
#include <RaptorQ.hpp>

#include "buffer.hh"
#include "common.hh"
#include "socket.hh"
#include "wire_format.hh"

template<typename Alignment>
size_t readFile(std::string filename, std::vector<Alignment>& content)
{
    std::ifstream infile;
    infile.open(filename, std::ios::in | std::ios::binary);
    infile.unsetf(std::ios::skipws);

    std::streampos filesz;
    infile.seekg(0, std::ios::end);
    filesz = infile.tellg();
    infile.seekg(0, std::ios::beg);

    size_t dataLength = static_cast<size_t>(
            std::ceil(static_cast<float>(filesz) / sizeof(Alignment)));
    content.reserve(dataLength);

    std::istreambuf_iterator<char> it(infile);
    std::istreambuf_iterator<char> eos;

    uint8_t shift = 0;
    Alignment tmp = 0;
    while (it != eos) {
        tmp += static_cast<uint8_t>(*it) << (shift * 8);
        shift++;
        if (shift >= sizeof(Alignment)) {
            content.push_back(tmp);
            shift = 0;
            tmp = 0;
        }
        it++;
    }
    if (shift != 0) content.push_back(tmp);

    return static_cast<size_t>(filesz);
}

int main( int argc, char *argv[] )
{
    /* check the command-line arguments */
    if ( argc < 1 ) { abort(); } /* for sticklers */

    if ( argc != 4 ) {
        std::cerr << "Usage: " << argv[ 0 ] << " HOST PORT FILE" << std::endl;
        return EXIT_FAILURE;
    }

    /* read file */
    std::vector<Alignment> dataToSend;
    size_t fileSize = readFile(argv[3], dataToSend);

    /* setting parameters */
    const uint16_t subSymbolSize = 1 << 13;
    const uint16_t symbolSize = subSymbolSize;
    const uint32_t numSymPerBlock = 100;
    RaptorQ::Encoder<typename std::vector<Alignment>::iterator,
            typename std::vector<Alignment>::iterator>
            encoder(dataToSend.begin(),
                    dataToSend.end(),
                    subSymbolSize,
                    symbolSize,
                    numSymPerBlock * symbolSize);

    // TODO: have two Buffers, one for sending, one for receiving?
    Buffer payload;
    WireFormat::HandshakeReq* p = payload.emplaceAppend<WireFormat::HandshakeReq>();
    p->fileSize = fileSize;
    p->commonData = encoder.OTI_Common();
    p->schemeSpecificData = encoder.OTI_Scheme_Specific();

    /* fetch command-line arguments */
    const std::string host { argv[ 1 ] }, port { argv[ 2 ] };

    /* construct UDP socket */
    UDPSocket udp_socket;

    /* connect the socket to the given host and port */
    udp_socket.connect(Address(host, port));

    /* send handshake req */
    std::cout << "Sending " << payload.size() << " bytes." << std::endl;
    std::cout << "fileSize = " << fileSize << std::endl;
    std::cout << "OTI_COMMON = " << encoder.OTI_Common() << std::endl;
    std::cout << "OTI_SCHEME_SPECIFIC = " << encoder.OTI_Scheme_Specific() << std::endl;
    udp_socket.sendbytes(payload.c_str(), payload.size());

    /* precompute intermediate symbols */
    encoder.precompute(1, false);

    /* receive handshake ack */
    UDPSocket::received_datagram handshakeAck = udp_socket.recv();
    Buffer buffer(handshakeAck.payload, handshakeAck.recvlen);
    const WireFormat::HandshakeResp* resp = buffer.get<WireFormat::HandshakeResp>(0);
    std::cout << "Received handshake ack: ip = " << std::string(resp->addr)
              << " port = " << resp->port << std::endl;

    /* start sending encoding symbols in a round-robin fashion */
    using SYM_ITER = decltype((*encoder.begin()).begin_source());
    std::vector<std::pair<SYM_ITER, SYM_ITER>> symIters;
    for (uint8_t sbn = 0; sbn < encoder.blocks(); sbn++) {
        auto block = *encoder.begin().operator++(sbn);
        symIters.emplace_back(block.begin_source(), block.end_source());
    }

    size_t numOfAlignPerSymbol = static_cast<size_t>(
            std::ceil(static_cast<float>(symbolSize) / sizeof(Alignment)));
    std::vector<Alignment> symbol;
    symbol.reserve(numOfAlignPerSymbol);
    symbol.insert(symbol.begin(), numOfAlignPerSymbol, 0);

    // TODO: change to while-loop and properly check ack
    for (uint16_t esi = 0; esi < 999; esi++) {
        if (esi >= encoder.begin().operator*().symbols()) {
            break;
        }

//    while (true) {
        for (uint8_t sbn = 0; sbn < encoder.blocks(); sbn++) {
            usleep(10000);
            // TODO: check if this block has been successfully decoded by receiver

            /* Switch to repair symbol if we have drained the source symbols */
            if (symIters[sbn].first == symIters[sbn].second) {
                auto block = *encoder.begin().operator++(sbn);
                new (&symIters[sbn].first) SYM_ITER(block.begin_repair());
                new (&symIters[sbn].second) SYM_ITER(block.end_repair(
                        block.max_repair()));
            }

            auto& symIter = symIters[sbn].first;
            // TODO: extract a getSymbol method
            auto begin = symbol.begin();
            (*symIter)(begin, symbol.end());
            /* convert symbol to binary blob */
            payload.clear();
            *payload.append<uint32_t>() = (*symIter).id();
            for (Alignment al : symbol) {
                *payload.append<Alignment>() = al;
            }
            udp_socket.sendbytes(payload.c_str(), payload.size());
            ++symIter;
        }
    }

    return EXIT_SUCCESS;
}