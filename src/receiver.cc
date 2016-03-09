#include <iostream>
#include <RaptorQ.hpp>
#include <unistd.h>
#include <semaphore.h>

#include "tub.hh"
#include "common.hh"
#include "wire_format.hh"
#include "progress.hh"

int DEBUG_F;

const int SHARED_QUEUE_SIZE = MAX_BLOCKS * 2;
std::unique_ptr<WireFormat::DataPacket> symbolQueue[SHARED_QUEUE_SIZE];
int qIn = 0, qOut = 0;
sem_t qNonfull, qNonempty;

void decodingLoop(RaptorQDecoder* decoder,      // only accessed from decoderThread
                  DCCPSocket* socket,           // POSIX thread-safe
                  const Alignment* fileStart,   // const
                  Bitmask256* decodedBlocks)    // thread-safe
{
    size_t decoderPaddedSize = 0;
    std::vector<Alignment*> blockStart(decoder->blocks() + 1);
    blockStart[0] = const_cast<Alignment*>(fileStart);
    for (uint8_t sbn = 0; sbn < decoder->blocks(); sbn++) {
        blockStart[sbn + 1] = blockStart[sbn] +
                              decoder->block_size(sbn) / ALIGNMENT_SIZE;
        decoderPaddedSize += decoder->block_size(sbn);
    }

    std::chrono::time_point<std::chrono::system_clock> nextAckTime =
            std::chrono::system_clock::now() + HEARTBEAT_INTERVAL;

    // Initialize progress bar
    progress_t progress {decoder->blocks(), DEBUG_F};
    progress.show();

    while (1) {
        // Send heartbeat ACK
        auto currTime = std::chrono::system_clock::now();
        if (currTime > nextAckTime) {
            if (DEBUG_F)
                printf("Sent Heartbeat ACK\n");

            sendInWireFormat<WireFormat::Ack>(
                    socket, decodedBlocks->toBitsetArray(), INIT_REPAIR_SYMBOL_INTERVAL);
            nextAckTime = currTime + HEARTBEAT_INTERVAL;
        }

        if (decodedBlocks->count() == decoder->blocks()) {
            break;
        }

        sem_wait(&qNonempty);
        auto dataPacket = std::move(symbolQueue[qOut]);
        qOut = (qOut + 1) % SHARED_QUEUE_SIZE;
        sem_post(&qNonfull);

        Alignment* begin = reinterpret_cast<Alignment*>(dataPacket->raw);
        if (!decoder->add_symbol(begin,
                reinterpret_cast<Alignment*>(dataPacket->raw + SYMBOL_SIZE),
                dataPacket->id)) {
            continue;
        }

        uint8_t sbn = downCast<uint8_t>(dataPacket->id >> 24);
        if (!decodedBlocks->test(sbn)) {
            Alignment* begin =  blockStart[sbn];
            if (decoder->decode(begin, blockStart[sbn + 1], sbn) > 0) {
                // send ACK for block sbn
                if (DEBUG_F)
                    printf("Block %u decoded.\n", static_cast<int>(sbn));

                decodedBlocks->set(sbn);
                sendInWireFormat<WireFormat::Ack>(
                        socket, decodedBlocks->toBitsetArray(), INIT_REPAIR_SYMBOL_INTERVAL);
                progress.update(decodedBlocks->count());
            }
        }
    }
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
        if (DEBUG_F) 
            printf("poll timeout in %d ms!\n", timeoutMs);
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
             Alignment* recvFileStart)
{
    const uint8_t numBlocks = decoder.blocks();
    Bitmask256 decodedBlocks;

    std::thread decoderThread(decodingLoop, &decoder, socket, recvFileStart, &decodedBlocks);

    std::unique_ptr<WireFormat::DataPacket> dataPacket;
    while (decodedBlocks.count() < numBlocks) {
        // Receive one symbol
        if (!pollin(socket)) {
            continue;
        }
        dataPacket = receive<WireFormat::DataPacket>(socket);
        uint8_t sbn = downCast<uint8_t>(dataPacket->id >> 24);
        uint32_t esi = (dataPacket->id << 8) >> 8;
        if (DEBUG_F) {
            printf("Received sbn = %u, esi = %u\n", static_cast<uint32_t>(sbn), esi);
        }

        if (decodedBlocks.test(sbn)) {
            // Useless symbol: block already decoded
            continue;
        }
        sem_wait(&qNonfull);
        symbolQueue[qIn] = std::move(dataPacket);
        qIn = (qIn + 1) % SHARED_QUEUE_SIZE;
        sem_post(&qNonempty);
    }

    decoderThread.join();
    printf("File decoded successfully.\n");
}

int main(int argc, char *argv[])
{
    if (parseArgs(argc, argv) == -1)
        return EXIT_FAILURE;

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

    // Receive file
    receive(decoder, socket.get(), reinterpret_cast<Alignment*>(start));

    SystemCall("msync", msync(start, decoderPaddedSize, MS_SYNC));
    SystemCall("munmap", munmap(start, decoderPaddedSize));
    SystemCall("truncate the padding at the end of the file",
            ftruncate(fd, req->fileSize));
    SystemCall("close fd", close(fd));

    sem_destroy(&qNonfull);
    sem_destroy(&qNonempty);

    return EXIT_SUCCESS;
}
