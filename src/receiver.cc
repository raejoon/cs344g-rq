#include <iostream>
#include <RaptorQ.hpp>
#include "unistd.h"

#include "tub.hh"
#include "common.hh"
#include "wire_format.hh"
#include "progress.hh"

const static std::chrono::duration<int64_t, std::milli> TEAR_DOWN_DURATION =
        2 * HEART_BEAT_INTERVAL;

int DEBUG_F;

void printUsage(char *command) 
{
    std::cerr << "Usage: " << command << " [-dh]" << std::endl;
    std::cerr << "\t-h: help" << std::endl;
    std::cerr << "\t-d: debug (per-symbol messages instead of a progress bar)" << std::endl;
}

int parseArgs(int argc, char *argv[]) 
{
    /* check the command-line arguments */
    if ( argc < 1 ) { abort(); } /* for sticklers */

    // check options
    DEBUG_F = 0;
    int c = 0;
    while ((c = getopt(argc, argv, "dh")) != -1) {
        switch (c) {
            case 'd':
                DEBUG_F = 1;
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

DCCPSocket* respondHandshake(Tub<WireFormat::HandshakeReq>& req)
{
    DCCPSocket* localSocket {new DCCPSocket};
    try {
        localSocket->bind(Address("0", 6330));
    }
    catch (unix_error e) {
        std::cerr << "Port 6330 is already used. ";
        std::cerr << "Picking a random port..." << std::endl;
        localSocket->bind(Address("0", 0));
    }
    printf("%s\n", localSocket->local_address().to_string().c_str());

    localSocket->listen();
    DCCPSocket remoteSocket = localSocket->accept();
    DCCPSocket* socket = new DCCPSocket(std::move(remoteSocket)); 

    // Wait for handshake request
    char* datagram = socket->recv();
    if (WireFormat::getOpcode(datagram) != WireFormat::HANDSHAKE_REQ) {
        std::cerr << "Expect to receive handshake request" << std::endl;
        exit(EXIT_FAILURE);
    }

    new (&req) Tub<WireFormat::HandshakeReq>(datagram);
    free(datagram);

    printf("Received handshake request: {connection id = %u, file name = %s, "
           "file size = %zu, OTI_COMMON = %lu, OTI_SCHEME_SPECIFIC = %u}\n",
           req->connectionId, req->fileName, req->fileSize, req->otiCommon,
           req->otiScheme);

    // Send handshake response
    sendInWireFormat<WireFormat::HandshakeResp>(
        socket, uint32_t(req->connectionId));
    printf("Sent handshake response: {connection id = %u}\n",
           req->connectionId);

    return socket;
}

void receive(RaptorQDecoder& decoder,
             DCCPSocket* socket,
             void* recvfile_start)
{
    // Start receiving symbols
    size_t decoderPaddedSize = 0;

    std::vector<Alignment*> blockStart(decoder.blocks() + 1);
    blockStart[0] = reinterpret_cast<Alignment*>(recvfile_start);
    for (uint8_t sbn = 0; sbn < decoder.blocks(); sbn++) {
        blockStart[sbn + 1] = blockStart[sbn] +
            decoder.block_size(sbn) / ALIGNMENT_SIZE;
        decoderPaddedSize += decoder.block_size(sbn);
    }

    // Initialize progress bar
    progress_t progress {decoderPaddedSize, DEBUG_F};
    progress.show();

    std::chrono::time_point<std::chrono::system_clock> nextAckTime =
        std::chrono::system_clock::now() + HEART_BEAT_INTERVAL;
    Bitmask256 decodedBlocks;
    uint32_t numSymbolRecv[MAX_BLOCKS] {0};
    uint32_t maxSymbolRecv[MAX_BLOCKS] {0};
    uint32_t repairSymbolInterval = INIT_REPAIR_SYMBOL_INTERVAL;

    char* datagram;

    while (1) {
        datagram = socket->recv();
        // sender has closed the connection
        if (!datagram)
            break;

        Tub<WireFormat::DataPacket> dataPacket(datagram);
        free(datagram);

        if (dataPacket->header.opcode != WireFormat::Opcode::DATA_PACKET) {
            std::cerr << "Expect to receive data packet" << std::endl;
            // exit(1);
        }

        uint8_t sbn = downCast<uint8_t>(dataPacket->id >> 24);
        uint32_t esi = (dataPacket->id << 8) >> 8;

        if (DEBUG_F) {
        //printf("Received sbn = %u, esi = %u\n", static_cast<uint32_t>(sbn), esi);
        }
        numSymbolRecv[sbn]++;
        maxSymbolRecv[sbn] = std::max(maxSymbolRecv[sbn], esi);

        auto currTime = std::chrono::system_clock::now();
        if (currTime > nextAckTime) {
            // Send heartbeat ACK
            if (DEBUG_F) printf("Sent Heartbeat ACK\n");
            sendInWireFormat<WireFormat::Ack>(socket,
                    decodedBlocks.bitset,
                    repairSymbolInterval);
            nextAckTime = currTime + HEART_BEAT_INTERVAL;
        }

        if (decodedBlocks.test(sbn)) {
            continue;
        }

        Alignment* begin = reinterpret_cast<Alignment*>(dataPacket->raw);
        decoder.add_symbol(begin,
                reinterpret_cast<Alignment*>(dataPacket->raw + SYMBOL_SIZE),
                dataPacket->id);

        begin = blockStart[sbn];
        if (decoder.decode(begin, blockStart[sbn + 1], sbn) > 0) {
            progress.update(decoder.block_size(sbn));

            // send ACK for block sbn
            if (DEBUG_F) 
                printf("Block %u decoded.\n", static_cast<int>(sbn));
            decodedBlocks.set(sbn);

            // Update the repair symbol transmission interval to be sent in the
            // next ACK
            float packetLossRate;
            if (numSymbolRecv[sbn] == maxSymbolRecv[sbn] + 1) {
                packetLossRate = 0.0f;
                repairSymbolInterval = ~0u;
            } else {
                packetLossRate = 1.0f -
                    numSymbolRecv[sbn] * 1.0f / (maxSymbolRecv[sbn] + 1);
                assert(packetLossRate > 0.0f);
                repairSymbolInterval = static_cast<uint32_t >(std::min(
                            std::ceil(1.0f / packetLossRate - 1),
                            1.0f * (((uint32_t)~0u) - 1)));
            }
            if (DEBUG_F) {
                printf("Packet loss rate = %.2f, Repair symbol interval = %u.\n",
                        packetLossRate, repairSymbolInterval);
            }
        }
    }

    assert(decodedBlocks.count() == decoder.blocks());
    printf("File decoded successfully.\n");
}

int main(int argc, char *argv[])
{
    if (parseArgs(argc, argv) == -1)
        return EXIT_FAILURE;

    DEBUG_F = 1;
    // Wait for handshake request and send back handshake response
    Tub<WireFormat::HandshakeReq> req;
    DCCPSocket* socket = respondHandshake(req);

    // Set up the RaptorQ decoder
    RaptorQDecoder decoder(req->otiCommon, req->otiScheme);
    size_t decoderPaddedSize = 0;
    for (int i = 0; i < decoder.blocks(); i++) {
        decoderPaddedSize += decoder.block_size(i);
    }

    // Create the receiving file
    int fd = SystemCall("open the file to be written",
    //       open(req->fileName, O_RDWR | O_CREAT | O_TRUNC, (mode_t)0600));
             open("demo.out", O_RDWR | O_CREAT | O_TRUNC, (mode_t)0600));
    SystemCall("lseek", lseek(fd, decoderPaddedSize - 1, SEEK_SET));
    SystemCall("write", write(fd, "", 1));
    void* start = mmap(NULL, decoderPaddedSize, PROT_WRITE, MAP_SHARED, fd, 0);
    if (start == MAP_FAILED) {
        printf("mmap failed:%s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    // Receive file
    receive(decoder, socket, start);

    SystemCall("msync", msync(start, decoderPaddedSize, MS_SYNC));
    SystemCall("munmap", munmap(start, decoderPaddedSize));
    SystemCall("truncate the padding at the end of the file",
            ftruncate(fd, req->fileSize));
    SystemCall("close fd", close(fd));

    free(socket);
    return EXIT_SUCCESS;
}
