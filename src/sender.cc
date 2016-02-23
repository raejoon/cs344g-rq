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
#include <fcntl.h>

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

/**
 * Starts the handshake procedure with the receiver. This method needs to
 * handle retries automatically in the face of lost handshake request and/or
 * response.
 * TODO: implement retry strategy to counteract lost request/response.
 *
 * \return
 *      A reference to a non-blocking UDP socket if the handshake procedure
 *      succeeds; nullptr otherwise.
 */
template<typename Alignment>
std::unique_ptr<UDPSocket> initiateHandshake(const RaptorQEncoder& encoder,
                                             const std::string& host,
                                             const std::string& port,
                                             const FileWrapper<Alignment>& file)
{
    Buffer sendBuffer;
    uint32_t connectionId = generateRandom();
    sendBuffer.emplaceAppend<WireFormat::HandshakeReq>(
            connectionId, file.size(), encoder.OTI_Common(),
            encoder.OTI_Scheme_Specific());

    std::unique_ptr<UDPSocket> udpSocket {new UDPSocket};
    udpSocket->connect(Address(host, port));

    // Send handshake request
    printf("Sending handshake request: {connection Id = %u, file size = %zu, "
           "OTI_COMMON = %lu, OTI_SCHEME_SPECIFIC = %u}\n",
           connectionId, file.size(), encoder.OTI_Common(),
           encoder.OTI_Scheme_Specific());
    udpSocket->sendbytes(sendBuffer.c_str(), sendBuffer.size());

    // Wait for handshake response
    UDPSocket::received_datagram recvDatagram = udpSocket->recv();
    Buffer recvBuffer(recvDatagram.payload, recvDatagram.recvlen);
    const WireFormat::HandshakeResp* resp =
            recvBuffer.get<WireFormat::HandshakeResp>(0);
    if (connectionId == resp->connectionId) {
        printf("Received handshake response: {connectionId = %u}\n",
               resp->connectionId);

        // Set the socket to be non-blocking to support polling
        fcntl(udpSocket->fd_num(), F_SETFL, O_NONBLOCK);
        return udpSocket;
    }

    return nullptr;
}

/**
 * Invokes congestion control algorithm.
 *
 * This method is now called before sending each symbol and blocks until the
 * congestion controller says it's OK to send. This is subject to change.
 */
void congestionControl() {
    // TODO(Francis): implement congestion control logic here
    // Sleep-based congestion control :p
    usleep(10000);
}

void transmit(RaptorQEncoder& encoder,
              UDPSocket* udpSocket)
{
    // Send encoded symbols in a round-robin fashion
    std::vector<RaptorQSymbolIterator> iters, ends;
    for (const auto& block : encoder) {
        iters.push_back(block.begin_source());
        ends.push_back(block.end_source());
    }

    // Represents blocks that are decoded by the receiver
    Bitmask256 decodedBlocks;

    RaptorQSymbol symbol {0};
    Buffer sendBuffer;
    Buffer recvBuffer;

    uint32_t round = 0;
    const uint32_t MAX_ROUND = MAX_SYM_PER_BLOCK * 2;
    while (decodedBlocks.count() < encoder.blocks()) {
        if (round++ > MAX_ROUND) {
            printf("Error: we have sent way too many symbols; round = %u\n",
                   round);
            exit(EXIT_SUCCESS);
        }

        uint8_t sbn = std::numeric_limits<uint8_t>::max();
        for (const auto& block : encoder) {
            // Check if this block has been successfully decoded by the receiver
            sbn++;
            if (decodedBlocks.test(sbn)) {
                continue;
            }

            // If we have sent at least K symbols for this block, poll to see
            // if the receiver has acknowledged the block
            if (round >= block.symbols()) {
                try {
                    UDPSocket::received_datagram recvDatagram = udpSocket->recv();
                    recvBuffer.set(recvDatagram.payload, recvDatagram.recvlen);
                    decodedBlocks.bitwiseOr(Bitmask256(
                            recvBuffer.get<WireFormat::Ack>(0)->bitmask));
                    printf("Received ACK\n");
                    // TODO: print out the newly ack'ed blocks?
                } catch (const unix_error& e) {
                    if (e.code().value() != EAGAIN) {
                        printf("%s\n", e.what());
                    }
                }
            }

            // Switch to repair symbol if we have drained the source symbols
            RaptorQSymbolIterator* pSymbolIter = iters.data() + sbn;
            RaptorQSymbolIterator* pEndOfSymbols = ends.data() + sbn;
            if (*pSymbolIter == *pEndOfSymbols) {
                // This code is unfortunately pretty complex because
                // RaptorQ::Symbol_Iterator doesn't seem to be CopyAssignable
                // (it is CopyConstrutible though...)
                pSymbolIter->~RaptorQSymbolIterator();
                pEndOfSymbols->~RaptorQSymbolIterator();
                new (pSymbolIter) RaptorQSymbolIterator(block.begin_repair());
                new (pEndOfSymbols) RaptorQSymbolIterator(block.end_repair(
                        block.max_repair()));
            }

            RaptorQSymbolIterator& symbolIter = *pSymbolIter;
            auto begin = symbol.begin();
            (*symbolIter)(begin, symbol.end());

            // Convert symbol to binary blob
            sendBuffer.clear();
            sendBuffer.emplaceAppend<WireFormat::DataPacket>(
                    (*symbolIter).id(), symbol.data());

            // Wait until the congestion controller gives us a pass
            congestionControl();
            udpSocket->sendbytes(sendBuffer.c_str(), sendBuffer.size());
            ++symbolIter;
        }
    }
}

int main(int argc, char *argv[])
{
    /* check the command-line arguments */
    if ( argc < 1 ) { abort(); } /* for sticklers */

    if ( argc != 4 ) {
        std::cerr << "Usage: " << argv[ 0 ] << " HOST PORT FILE" << std::endl;
        return EXIT_FAILURE;
    }
    /* fetch command-line arguments */
    const std::string host { argv[ 1 ] }, port { argv[ 2 ] };

    // Read the file to transfer
    // TODO: modify readFile to return the FileWrapper directly
    std::vector<Alignment> dataToSend;
    size_t fileSize = readFile(argv[3], dataToSend);
    FileWrapper<Alignment> file {fileSize, dataToSend};

    // Setup parameters of the RaptorQ protocol
    RaptorQEncoder encoder(file.begin(),
                           file.end(),
                           SYMBOL_SIZE, /* no interleaving */
                           SYMBOL_SIZE,
                           MAX_DECODABLE_BLOCK_SIZE);

    // Precompute intermediate symbols in background
    // TODO: turn on asynchronous and parallel precomputation
    // TODO: how do I check if the precomputation is doen?
//    encoder.precompute(std::max(std::thread::hardware_concurrency(), 1), true);
    encoder.precompute(1, false);

    // Initiate handshake process
    std::unique_ptr<UDPSocket> udpSocket = initiateHandshake(
            encoder, host, port, file);
    if (!udpSocket) {
        printf("Handshake failure!\n");
        return EXIT_SUCCESS;
    }

    // Start transmission
    transmit(encoder, udpSocket.get());

    // Tear down connection
    // TODO

    return EXIT_SUCCESS;
}