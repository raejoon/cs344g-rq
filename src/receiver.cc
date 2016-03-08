#include <iostream>
#include <RaptorQ.hpp>
#include <unistd.h>
#include <semaphore.h>

#include "tub.hh"
#include "common.hh"
#include "wire_format.hh"
#include "progress.hh"

const static std::chrono::duration<int64_t, std::milli> TEAR_DOWN_DURATION =
        2 * HEARTBEAT_INTERVAL;

int DEBUG_F;

const int SHARED_QUEUE_SIZE = 10000;
std::unique_ptr<WireFormat::DataPacket> sharedQ[SHARED_QUEUE_SIZE];
int qIn = 0, qOut = 0;
sem_t qNonfull, qNonempty;
pthread_mutex_t decodedBlocksLock, repairSymbolIntervalLock;

struct DecoderThreadArgs {
    RaptorQDecoder* decoder;
    std::vector<Alignment*>* blockStart;
    Bitmask256* decodedBlocks;
    uint32_t* repairSymbolInterval;
};

void* decoderThread(void* decoderThreadArgs) {
    DecoderThreadArgs* args = (DecoderThreadArgs*) decoderThreadArgs;

    RaptorQDecoder& decoder = *args->decoder;
    std::vector<Alignment*>& blockStart = *args->blockStart;
    Bitmask256& decodedBlocks = *args->decodedBlocks;
    uint32_t& repairSymbolInterval = *args->repairSymbolInterval;

    // Initialize progress bar
    progress_t progress {decoder.blocks(), DEBUG_F};
    progress.show();
   
    uint32_t numSymbolRecv[MAX_BLOCKS] {0};
    uint32_t maxSymbolRecv[MAX_BLOCKS] {0};

    std::unique_ptr<WireFormat::DataPacket> dataPacket; 
    while (1) {
        if (decodedBlocks.count() == decoder.blocks()) {
            break;
        }

        sem_wait(&qNonempty);
        dataPacket = std::move(sharedQ[qOut]); 
        qOut = (qOut + 1) % SHARED_QUEUE_SIZE;
        sem_post(&qNonfull);

        uint8_t sbn = downCast<uint8_t>(dataPacket->id >> 24);
        uint32_t esi = (dataPacket->id << 8) >> 8;
        if (DEBUG_F) {
            printf("Received sbn = %u, esi = %u\n", static_cast<uint32_t>(sbn), esi);
        }

        numSymbolRecv[sbn]++;
        maxSymbolRecv[sbn] = std::max(maxSymbolRecv[sbn], esi); 

        if (decodedBlocks.test(sbn)) {
            continue;
        }

        Alignment* begin = reinterpret_cast<Alignment*>(dataPacket->raw);
        if (!decoder.add_symbol(begin,
                    reinterpret_cast<Alignment*>(dataPacket->raw + SYMBOL_SIZE),
                    dataPacket->id)) {
            continue;
        }
 
        begin = blockStart[sbn];
        if (decoder.decode(begin, blockStart[sbn + 1], sbn) > 0) {
            // send ACK for block sbn
            if (DEBUG_F) 
                printf("Block %u decoded.\n", static_cast<int>(sbn));

            pthread_mutex_lock(&decodedBlocksLock);
            decodedBlocks.set(sbn);
            pthread_mutex_unlock(&decodedBlocksLock);

            progress.update(decodedBlocks.count());
            
            // Update the repair symbol transmission interval to be sent in the
            // next ACK
            pthread_mutex_lock(&repairSymbolIntervalLock);
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
            pthread_mutex_unlock(&repairSymbolIntervalLock);
        }
    }

    return NULL;
}

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

bool pollin(DCCPSocket* socket, int timeoutMs = -1)
{
    struct pollfd ufds {socket->fd_num(), POLLIN, 0};
    int rv = SystemCall("poll", poll(&ufds, 1, timeoutMs));
    if (rv == 0) {
        if (DEBUG_F) printf("poll timeout in %d ms!", timeoutMs);
        return false;
    } else {
        return true;
    }
}

std::unique_ptr<DCCPSocket>
respondHandshake(std::unique_ptr<WireFormat::HandshakeReq>& req)
{
    DCCPSocket localSocket;
    try {
        localSocket.bind(Address("0", 6330));
    }
    catch (unix_error e) {
        std::cerr << "Port 6330 is already used. ";
        std::cerr << "Picking a random port..." << std::endl;
        localSocket.bind(Address("0", 0));
    }
    printf("%s\n", localSocket.local_address().to_string().c_str());

    localSocket.listen();
    DCCPSocket* socket = new DCCPSocket(localSocket.accept());

    // Wait for handshake request
    pollin(socket);
    req = receive<WireFormat::HandshakeReq>(socket);

    printf("Received handshake request: {connection id = %u, file name = %s, "
           "file size = %zu, OTI_COMMON = %lu, OTI_SCHEME_SPECIFIC = %u}\n",
           req->connectionId, req->fileName, req->fileSize, req->otiCommon,
           req->otiScheme);

    // Send handshake response
    sendInWireFormat<WireFormat::HandshakeResp>(
            socket, uint32_t(req->connectionId));
    printf("Sent handshake response: {connection id = %u}\n",
            req->connectionId);

    return std::unique_ptr<DCCPSocket>(socket);
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

    std::chrono::time_point<std::chrono::system_clock> nextAckTime =
        std::chrono::system_clock::now() + HEARTBEAT_INTERVAL;
    Bitmask256 decodedBlocks;
    uint32_t repairSymbolInterval = INIT_REPAIR_SYMBOL_INTERVAL;

    int decoder_blocks = decoder.blocks();
    // Create decoder thread
    DecoderThreadArgs decoderThreadArgs {&decoder, &blockStart, 
                                         &decodedBlocks, &repairSymbolInterval};
    pthread_t decoderThreadId;
    pthread_create(&decoderThreadId, NULL, decoderThread, &decoderThreadArgs);

    std::unique_ptr<WireFormat::DataPacket> dataPacket;

    while (1) {
        pthread_mutex_lock(&decodedBlocksLock);
        if (decodedBlocks.count() == decoder_blocks) {
            pthread_mutex_unlock(&decodedBlocksLock);
            break;
        }
        pthread_mutex_unlock(&decodedBlocksLock);

        auto currTime = std::chrono::system_clock::now();
        if (currTime > nextAckTime) {
            // Send heartbeat ACK
            if (DEBUG_F) 
                printf("Sent Heartbeat ACK\n");

            pthread_mutex_lock(&repairSymbolIntervalLock);
            uint32_t currRepairSymbolInterval = repairSymbolInterval; 
            pthread_mutex_unlock(&repairSymbolIntervalLock);

            pthread_mutex_lock(&decodedBlocksLock);
            sendInWireFormat<WireFormat::Ack>(socket,
                                              decodedBlocks.bitset,
                                              currRepairSymbolInterval);
            pthread_mutex_unlock(&decodedBlocksLock);

            nextAckTime = currTime + HEARTBEAT_INTERVAL;
        }

        if (!pollin(socket,
                downCast<int>(HEARTBEAT_INTERVAL.count() / 2))) {
            continue;
        }

        dataPacket = receive<WireFormat::DataPacket>(socket);

        sem_wait(&qNonfull);
        sharedQ[qIn] = std::move(dataPacket);
        qIn = (qIn + 1) % SHARED_QUEUE_SIZE;
        sem_post(&qNonempty);
    }

    pthread_join(decoderThreadId, NULL);
    printf("File decoded successfully.\n");
}

int main(int argc, char *argv[])
{
    if (parseArgs(argc, argv) == -1)
        return EXIT_FAILURE;

    DEBUG_F = 1;
    // Wait for handshake request and send back handshake response
    std::unique_ptr<WireFormat::HandshakeReq> req;
    std::unique_ptr<DCCPSocket> socket = respondHandshake(req);

    // Set up the RaptorQ decoder
    RaptorQDecoder decoder(req->otiCommon, req->otiScheme);
    size_t decoderPaddedSize = 0;
    for (int i = 0; i < decoder.blocks(); i++) {
        decoderPaddedSize += decoder.block_size(i);
    }

    // Create the receiving file
    int fd = SystemCall("open the file to be written",
             open(req->fileName, O_RDWR | O_CREAT | O_TRUNC, (mode_t)0600));
    SystemCall("lseek", lseek(fd, decoderPaddedSize - 1, SEEK_SET));
    SystemCall("write", write(fd, "", 1));
    void* start = mmap(NULL, decoderPaddedSize, PROT_WRITE, MAP_SHARED, fd, 0);
    if (start == MAP_FAILED) {
        printf("mmap failed:%s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    sem_init(&qNonfull, 0, SHARED_QUEUE_SIZE);
    sem_init(&qNonempty, 0, 0);
    pthread_mutex_init(&decodedBlocksLock, NULL);
    pthread_mutex_init(&repairSymbolIntervalLock, NULL);

    // Receive file
    receive(decoder, socket.get(), start);

    SystemCall("msync", msync(start, decoderPaddedSize, MS_SYNC));
    SystemCall("munmap", munmap(start, decoderPaddedSize));
    SystemCall("truncate the padding at the end of the file",
            ftruncate(fd, req->fileSize));
    SystemCall("close fd", close(fd));

    sem_destroy(&qNonfull);
    sem_destroy(&qNonempty);
    pthread_mutex_destroy(&decodedBlocksLock);
    pthread_mutex_destroy(&repairSymbolIntervalLock);

    return EXIT_SUCCESS;
}
