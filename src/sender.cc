#include <iostream>
#include <fstream>
#include <RaptorQ.hpp>
#include "unistd.h"

#include "tub.hh"
#include "common.hh"
#include "wire_format.hh"
#include "progress.hh"

int DEBUG_F;

void printUsage(char *command) 
{
    std::cerr << "Usage: " << command << " HOST [PORT] FILE" << std::endl;
}

int parseArgs(int argc,
              char *argv[],
              std::string& host,
              std::string& port,
              char*& filename)
{
    /* check the command-line arguments */
    if ( argc < 1 ) { abort(); } /* for sticklers */

    /* fetch command-line arguments */
    if ( argc == 3 ) {
        host = argv[1];
        port = "6330";
        filename = argv[2];
    }
    else if ( argc == 4 ) {
        host = argv[1];
        port = argv[2];
        filename = argv[3];
    }
    else {
        printUsage(argv[0]);
        return -1;
    }

    return 0;
}

/**
 * Starts the handshake procedure with the receiver. This method needs to
 * handle retries automatically in the face of lost handshake request and/or
 * response.
 * TODO: implement retry strategy to counteract lost request/response.
 *
 * \return
 *      A reference to a blocking DCCP socket if the handshake procedure
 *      succeeds; nullptr otherwise.
 */
DCCPSocket* initiateHandshake(const RaptorQEncoder& encoder,
                              const std::string& host,
                              const std::string& port,
                              const FileWrapper<Alignment>& file)
{
    DCCPSocket* socket {new DCCPSocket};
    socket->connect(Address(host, port));

    // Send handshake request
    uint32_t connectionId = generateRandom();
    sendInWireFormat<WireFormat::HandshakeReq>(
            socket,
            connectionId, file.name(), file.size(), 
            encoder.OTI_Common(), encoder.OTI_Scheme_Specific());
    printf("Sent handshake request: {connection id = %u, file name = %s, "
           "file size = %zu, OTI_COMMON = %lu, OTI_SCHEME_SPECIFIC = %u}\n",
           connectionId, file.name(), file.size(), encoder.OTI_Common(),
           encoder.OTI_Scheme_Specific());

    // Wait for handshake response
    char* datagram = socket->recv();
    if (WireFormat::getOpcode(datagram) != WireFormat::HANDSHAKE_RESP) {
        std::cerr << "Expect to receive handshake response" << std::endl;
        exit(EXIT_FAILURE);
    }

    Tub<WireFormat::HandshakeResp> resp(datagram);
    free(datagram);
    
    if (connectionId == resp->connectionId) {
        printf("Received handshake response: {connection id = %u}\n",
               resp->connectionId);
        return socket;
    }

    return nullptr;
}

/**
 * Send a single symbol to the given DCCP socket.
 *
 * \param[in]
 *      The DCCP socket.
 * \param[in,out] symbolIterator
 *      A symbol iterator referencing the symbol about to send; the position
 *      of the iterator will be advanced by one after this function is called.
 */
void sendSymbol(DCCPSocket *socket,
                RaptorQSymbolIterator &symbolIterator)
{
    static RaptorQSymbol symbol {0};
    auto begin = symbol.begin();
    (*symbolIterator)(begin, symbol.end());

    usleep(5000);
    sendInWireFormat<WireFormat::DataPacket>(socket,
            (*symbolIterator).id(), symbol.data());
    ++symbolIterator;
}

void transmit(RaptorQEncoder& encoder,
              DCCPSocket* socket)
{
    // Initialize progress bar
    progress_t progress {encoder.blocks(), DEBUG_F};
    progress.show();

    // Set up repair symbol iterators of all blocks
    std::vector<RaptorQSymbolIterator> repairSymbolIters;
    for (const auto& block : encoder) {
        repairSymbolIters.push_back(block.begin_repair());
    }

    // Represents blocks that are decoded by the receiver
    Bitmask256 decodedBlocks;
    std::chrono::time_point<std::chrono::system_clock> nextPollTime =
            std::chrono::system_clock::now() + HEART_BEAT_INTERVAL;
    uint32_t sourceSymbolCounter = 0;
    uint32_t repairSymbolInterval = INIT_REPAIR_SYMBOL_INTERVAL;

    char* datagram;

    for (uint8_t currBlock = 0; currBlock < encoder.blocks(); currBlock++) {
        const auto &block = *encoder.begin().operator++(currBlock);
        RaptorQSymbolIterator sourceSymbolIter = block.begin_source();
        for (int esi = 0; esi < block.symbols(); esi++) {
            // Send i-th source symbol of block sbn
            sendSymbol(socket, sourceSymbolIter);
            sourceSymbolCounter++;

            auto currTime = std::chrono::system_clock::now();
            if (currTime > nextPollTime) {
                // Poll to see if any ACK arrives
                while (poll(socket, datagram)) {
                    Tub<WireFormat::Ack> ack(datagram);
                    free(datagram);
                    uint8_t oldCount = decodedBlocks.count();
                    decodedBlocks.bitwiseOr(Bitmask256(ack->bitmask));
                    repairSymbolInterval = ack->repairSymbolInterval;
                    //printf("Received ACK: %u\n", decodedBlocks.count());
                    progress.update(decodedBlocks.count() - oldCount);
                }
                nextPollTime = currTime + HEART_BEAT_INTERVAL;
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
            Tub<WireFormat::Ack> ack(datagram);
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
    std::string host, port;
    char *filename;

    if (parseArgs(argc, argv, host, port, filename) == -1)
        return EXIT_FAILURE;

    DEBUG_F = 1;
    // Read the file to transfer
    FileWrapper<Alignment> file {filename};
    printf("Done reading file\n");

    // Setup parameters of the RaptorQ protocol
    std::unique_ptr<RaptorQEncoder> encoder = getEncoder(file);

    // Precompute intermediate symbols in background
    encoder->precompute(1, true);

    // Initiate handshake process
    DCCPSocket* socket = initiateHandshake(
            *encoder, host, port, file);
    if (!socket) {
        printf("Handshake failure!\n");
        return EXIT_SUCCESS;
    }

    // Start transmission
    transmit(*encoder, socket);

    free(socket);
    return EXIT_SUCCESS;
}
