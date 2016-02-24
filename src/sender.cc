#include <iostream>
#include <fstream>
#include <RaptorQ.hpp>
#include <fcntl.h>

#include "tub.hh"
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
    std::unique_ptr<UDPSocket> udpSocket {new UDPSocket};
    udpSocket->connect(Address(host, port));

    // Send handshake request
    uint32_t connectionId = generateRandom();
    sendInWireFormat<WireFormat::HandshakeReq>(
            udpSocket.get(), udpSocket->peer_address(),
            connectionId, file.size(), encoder.OTI_Common(),
            encoder.OTI_Scheme_Specific());
    printf("Sending handshake request: {connection Id = %u, file size = %zu, "
                   "OTI_COMMON = %lu, OTI_SCHEME_SPECIFIC = %u}\n",
           connectionId, file.size(), encoder.OTI_Common(),
           encoder.OTI_Scheme_Specific());

    // Wait for handshake response
    UDPSocket::received_datagram recvDatagram = udpSocket->recv();
    Tub<WireFormat::HandshakeResp> resp(recvDatagram.payload);
    // TODO: the right thing to do after receiving a datagram is to check
    // the opcode first
//    assert(resp.size() == recvDatagram.recvlen);
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
        // The following code tests recovering entirely from repair symbols.
//        iters.push_back(block.begin_repair());
//        ends.push_back(block.end_repair(block.max_repair()));
    }

    // Represents blocks that are decoded by the receiver
    Bitmask256 decodedBlocks;

    RaptorQSymbol symbol {0};
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
                    Tub<WireFormat::Ack> ack(recvDatagram.payload);
                    decodedBlocks.bitwiseOr(Bitmask256(ack->bitmask));
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

            // Wait until the congestion controller gives us a pass
            congestionControl();
            sendInWireFormat<WireFormat::DataPacket>(udpSocket,
                    udpSocket->peer_address(), (*symbolIter).id(), symbol.data());
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
    printf("Done reading file\n");

    // Setup parameters of the RaptorQ protocol
    RaptorQEncoder encoder(file.begin(),
                           file.end(),
                           SYMBOL_SIZE, /* no interleaving */
                           SYMBOL_SIZE,
                           MAX_DECODABLE_BLOCK_SIZE);

    // Precompute intermediate symbols in background
    // TODO: is one dedicated thread for precomputation enough?
//    encoder.precompute(std::max(std::thread::hardware_concurrency(), 1), true);
    encoder.precompute(1, true);

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