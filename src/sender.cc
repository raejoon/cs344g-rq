#include <iostream>
#include <fstream>
#include <RaptorQ.hpp>

#include "tub.hh"
#include "common.hh"
#include "wire_format.hh"
#include "progress.hh"

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
    std::unique_ptr<UDPSocket> socket {new UDPSocket};
    socket->connect(Address(host, port));

    // Send handshake request
    uint32_t connectionId = generateRandom();
    sendInWireFormat<WireFormat::HandshakeReq>(
            socket.get(), socket->peer_address(),
            connectionId, file.name(), file.size(), encoder.OTI_Common(),
            encoder.OTI_Scheme_Specific());
    printf("Sending handshake request: {connection Id = %u, file size = %zu, "
                   "OTI_COMMON = %lu, OTI_SCHEME_SPECIFIC = %u}\n",
           connectionId, file.size(), encoder.OTI_Common(),
           encoder.OTI_Scheme_Specific());

    // Wait for handshake response
    UDPSocket::received_datagram recvDatagram = socket->recv();
    Tub<WireFormat::HandshakeResp> resp(recvDatagram.payload);
    // TODO: the right thing to do after receiving a datagram is to check
    // the opcode first
//    assert(resp.size() == recvDatagram.recvlen);
    if (connectionId == resp->connectionId) {
        printf("Received handshake response: {connectionId = %u}\n",
               resp->connectionId);
        return socket;
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
//    usleep(1000);
}

/**
 * Send a single symbol to the given UDP socket.
 *
 * \param[in]
 *      The UDP socket.
 * \param[in,out] symbolIterator
 *      A symbol iterator referencing the symbol about to send; the position
 *      of the iterator will be advanced by one after this function is called.
 */
void sendSymbol(UDPSocket *socket,
                RaptorQSymbolIterator &symbolIterator)
{
    static RaptorQSymbol symbol {0};
    auto begin = symbol.begin();
    (*symbolIterator)(begin, symbol.end());

    // Wait until the congestion controller gives us a pass
    congestionControl();
    sendInWireFormat<WireFormat::DataPacket>(socket,
            socket->peer_address(), (*symbolIterator).id(), symbol.data());
    ++symbolIterator;
}

void transmit(RaptorQEncoder& encoder,
              UDPSocket* socket)
{
    // Initialize progress bar
    progress_t progress {encoder.blocks()};
    progress.show();

    // Set up repair symbol iterators of all blocks
    std::vector<RaptorQSymbolIterator> repairSymbolIters;
    for (const auto& block : encoder) {
        repairSymbolIters.push_back(block.begin_repair());
    }

    // Represents blocks that are decoded by the receiver
    Bitmask256 decodedBlocks;
    const uint32_t POLL_INTERVAL = 5;
    uint32_t sourceSymbolCounter = 0;
    uint32_t repairSymbolInterval = INIT_REPAIR_SYMBOL_INTERVAL;
    UDPSocket::received_datagram datagram {Address(), 0, 0, 0};
    for (uint8_t currBlock = 0; currBlock < encoder.blocks(); currBlock++) {
        const auto &block = *encoder.begin().operator++(currBlock);
        RaptorQSymbolIterator sourceSymbolIter = block.begin_source();
        for (int esi = 0; esi < block.symbols(); esi++) {
            // Send i-th source symbol of block sbn
            sendSymbol(socket, sourceSymbolIter);
            sourceSymbolCounter++;

            if (sourceSymbolCounter % POLL_INTERVAL == 0) {
                // Poll to see if any ACK arrives
                while (poll(socket, datagram)) {
                    Tub<WireFormat::Ack> ack(datagram.payload);
                    uint8_t oldCount = decodedBlocks.count();
                    decodedBlocks.bitwiseOr(Bitmask256(ack->bitmask));
                    repairSymbolInterval = ack->repairSymbolInterval;
                    //printf("Received ACK: %u\n", decodedBlocks.count());
                    progress.update(decodedBlocks.count() - oldCount);
                }
            }

            if (sourceSymbolCounter % repairSymbolInterval == 0) {
                // Send repair symbols of previous blocks
                for (uint8_t prevBlock = 0; prevBlock < currBlock; prevBlock++) {
                    if (!decodedBlocks.test(prevBlock)) {
                        sendSymbol(socket, repairSymbolIters[prevBlock]);
                    }
                }
            }
        }
    }

    while (decodedBlocks.count() < encoder.blocks()) {
        // Send repair symbols for in round-robin
        for (uint8_t sbn = 0; sbn < encoder.blocks(); sbn++) {
            if (!decodedBlocks.test(sbn)) {
                sendSymbol(socket, repairSymbolIters[sbn]);
            }
        }

        // Poll to see if any ACK arrives
        while (poll(socket, datagram)) {
            Tub<WireFormat::Ack> ack(datagram.payload);
            uint8_t oldCount = decodedBlocks.count();
            decodedBlocks.bitwiseOr(Bitmask256(ack->bitmask));
            //printf("Received ACK: %u\n", decodedBlocks.count());
            progress.update(decodedBlocks.count() - oldCount);
        }
    }
}

/**
 * Instantiates a RaptorQ encoder with an (near) optimal setting.
 */
template<typename Alignment>
std::unique_ptr<RaptorQEncoder> getEncoder(FileWrapper<Alignment>& file)
{
    int numOfSymbolsPerBlock = 64;
    while (numOfSymbolsPerBlock <= 1024) {
        std::unique_ptr<RaptorQEncoder> encoder {
                new RaptorQEncoder(file.begin(),
                                   file.end(),
                                   SYMBOL_SIZE, /* no interleaving */
                                   SYMBOL_SIZE,
                                   numOfSymbolsPerBlock * SYMBOL_SIZE)
        };
        if (*encoder) {
            return encoder;
        }
        numOfSymbolsPerBlock += 64;
    }
    printf("Unable to instantiate a RaptorQ encoder.\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
    /* check the command-line arguments */
    if ( argc < 1 ) { abort(); } /* for sticklers */

    char *filename;
    std::string host, port;

    /* fetch command-line arguments */
    if ( argc == 3 ) {
        filename = argv[2];
        host = argv[1];
        port = "6330";
    }
    else if ( argc == 4 ) {
        filename = argv[3];
        host = argv[1];
        port = argv[2];
    }
    else {
        std::cerr << "Usage: " << argv[ 0 ] << " HOST [PORT] FILE" << std::endl;
        return EXIT_FAILURE;
    }

    // Read the file to transfer
    FileWrapper<Alignment> file {filename};
    printf("Done reading file\n");

    // Setup parameters of the RaptorQ protocol
    std::unique_ptr<RaptorQEncoder> encoder = getEncoder(file);

    // Precompute intermediate symbols in background
    encoder->precompute(1, true);

    // Initiate handshake process
    std::unique_ptr<UDPSocket> socket = initiateHandshake(
            *encoder, host, port, file);
    if (!socket) {
        printf("Handshake failure!\n");
        return EXIT_SUCCESS;
    }

    // Start transmission
    transmit(*encoder, socket.get());

    return EXIT_SUCCESS;
}
