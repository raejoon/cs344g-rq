#include <iostream>
#include <fstream>
#include <RaptorQ.hpp>
#include <unistd.h>

#include "tub.hh"
#include "common.hh"
#include "wire_format.hh"
#include "progress.hh"

int DEBUG_F;

void printUsage(char *command) 
{
    std::cerr << "Usage: " << command << " HOST [PORT] FILE [-dh]" << std::endl;
    std::cerr << "\t-h: help" << std::endl;
    std::cerr << "\t-d: debug (per-symbol messages instead of a progress bar)" << std::endl;
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
    DEBUG_F = 0;
    int c;

    int argsNum = 1;
    while (argsNum < argc) {
        if (argv[argsNum][0] == '-')
            break;
        argsNum++;
    }

    if (argsNum == 3) {
        host = argv[1];
        port = "6330";
        filename = argv[2];
    } else if (argsNum == 4) {
        host = argv[1];
        port = argv[2];
        filename = argv[3];
    } else {
        printUsage(argv[0]);
        return -1;
    }

    optind = argsNum;
    while ((c = getopt(argc, argv, "dh")) != -1) {
        switch (c) {
            case 'd':
                DEBUG_F = 1;
                printf("RIGHT\n");
                break;
            case 'h':
            case '?':
                printUsage(argv[0]);
                return -1;
            default:
                abort();
        }
    }
    if ( optind != argc ) {
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
std::unique_ptr<DCCPSocket>
initiateHandshake(const RaptorQEncoder& encoder,
                  const std::string& host,
                  const std::string& port,
                  const FileWrapper<Alignment>& file)
{
    DCCPSocket* socket = new DCCPSocket;
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
    std::unique_ptr<WireFormat::HandshakeResp> resp =
            receive<WireFormat::HandshakeResp>(socket);
    if (connectionId == resp->connectionId) {
        printf("Received handshake response: {connection id = %u}\n",
               resp->connectionId);
        return std::unique_ptr<DCCPSocket>(socket);
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
                UDPSocket* udpSocket,
                RaptorQSymbolIterator &symbolIterator,
                uint32_t& repairSymbolInterval,
                Bitmask256 &decodedBlocks,
                progress_t &progress)
{
    static RaptorQSymbol symbol {0};
    auto begin = symbol.begin();
    (*symbolIterator)(begin, symbol.end());

    struct pollfd ufds[2];
    ufds[0] = {udpSocket->fd_num(), POLLIN, 0};
    ufds[1] = {socket->fd_num(), POLLOUT, 0};
    while (1) {
        ufds[0].revents = ufds[1].revents = 0;
        SystemCall("poll", poll(ufds, 2, -1));
        if (ufds[0].revents & POLLIN) {
            std::unique_ptr<WireFormat::Ack> ack =
                    receive<WireFormat::Ack>(udpSocket);

            if (!ack) { // receiver has closed connection
                decodedBlocks.setFirstN(downCast<uint8_t>(progress.workSize));
                progress.update(decodedBlocks.count());
                break;
            } else {
                decodedBlocks.bitwiseOr(Bitmask256(ack->bitmask));
                if (DEBUG_F)
                    printf("Received ACK, count = %d\n", int(decodedBlocks.count()));
                repairSymbolInterval = ack->repairSymbolInterval;
                progress.update(decodedBlocks.count());
            }
        }

        if (ufds[1].revents & POLLOUT) {
            int rv = sendInWireFormat<WireFormat::DataPacket>(
                    socket, (*symbolIterator).id(), symbol.data());
            if (rv >= 0) {
                if (DEBUG_F) {
                    uint32_t pktId = (*symbolIterator).id(); 
                    uint8_t sbn = downCast<uint8_t>(pktId >> 24);
                    uint32_t esi = (pktId << 8) >> 8;
                    printf("Sent sbn = %u, esi = %u\n", static_cast<uint32_t>(sbn), esi);
                }

                ++symbolIterator;
                usleep(350);
                break;
            } else if (rv == -1) {
                usleep(350);
                if (DEBUG_F) 
                    printf("sendInWireFormat: failed\n");
            } else {
                decodedBlocks.setFirstN(downCast<uint8_t>(progress.workSize));
                progress.update(decodedBlocks.count());
                break;
            }
        }
    }
}

void transmit(RaptorQEncoder& encoder,
              DCCPSocket* socket,
              UDPSocket* udpSocket)
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
    uint32_t sourceSymbolCounter = 0;
    uint32_t repairSymbolInterval = INIT_REPAIR_SYMBOL_INTERVAL;

    for (uint8_t currBlock = 0; currBlock < encoder.blocks(); currBlock++) {
        const auto &block = *encoder.begin().operator++(currBlock);
        RaptorQSymbolIterator sourceSymbolIter = block.begin_source();
        for (int esi = 0; esi < block.symbols(); esi++) {
            // Send i-th source symbol of block sbn
            sendSymbol(socket, udpSocket, sourceSymbolIter,
                    repairSymbolInterval, decodedBlocks, progress);
            sourceSymbolCounter++;

            if (sourceSymbolCounter % repairSymbolInterval == 0) {
                // Send repair symbols of previous blocks
                for (uint8_t prevBlock = 0; prevBlock < currBlock; prevBlock++) {
                    if (!decodedBlocks.test(prevBlock)) {
                        sendSymbol(socket, udpSocket,
                                repairSymbolIters[prevBlock],
                                repairSymbolInterval, decodedBlocks, progress);
                    }
                }
            }
        }
    }

    while (decodedBlocks.count() < encoder.blocks()) {
        // Send repair symbols for in round-robin
        for (uint8_t sbn = 0; sbn < encoder.blocks(); sbn++) {
            if (!decodedBlocks.test(sbn)) {
                sendSymbol(socket, udpSocket, repairSymbolIters[sbn],
                        repairSymbolInterval, decodedBlocks, progress);
            }
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

//    DEBUG_F = 1;
    // Read the file to transfer
    FileWrapper<Alignment> file {filename};
    printf("Done reading file\n");

    // Setup parameters of the RaptorQ protocol
    std::unique_ptr<RaptorQEncoder> encoder = getEncoder(file);

    // Precompute intermediate symbols in background
    encoder->precompute(0, true);

    // Initiate handshake process
    std::unique_ptr<DCCPSocket> socket = initiateHandshake(
            *encoder, host, port, file);
    if (!socket) {
        printf("Handshake failure!\n");
        return EXIT_SUCCESS;
    }

    // UDPSocket for receiving ACK
    UDPSocket udpSocket;
    // TODO: avoid hardcode 6331
    udpSocket.bind(Address("0", 6331));

    // Start transmission
    transmit(*encoder, socket.get(), &udpSocket);

    return EXIT_SUCCESS;
}
