
#include <chrono>
#include <iostream>
#include <RaptorQ.hpp>

#include "tub.hh"
#include "common.hh"
#include "socket.hh"
#include "wire_format.hh"

const static std::chrono::duration<int64_t, std::milli> HEART_BEAT_INTERVAL =
        std::chrono::milliseconds(500);

const static std::chrono::duration<int64_t, std::milli> TEAR_DOWN_DURATION =
        2 * HEART_BEAT_INTERVAL;

int main( int argc, char *argv[] )
{
    /* check the command-line arguments */
    if ( argc < 1 ) { abort(); } /* for sticklers */

    if ( argc != 1 ) {
        std::cerr << "Usage: " << argv[ 0 ] << std::endl;
        return EXIT_FAILURE;
    }

    // Wait for handshake request and send back handshake response
    std::unique_ptr<UDPSocket> udpSocket{new UDPSocket};
    udpSocket->bind(Address("0", 0));
    printf("%s\n", udpSocket->local_address().to_string().c_str());

    UDPSocket::received_datagram datagram = udpSocket->recv();
    Address senderAddr = datagram.source_address;
    Tub<WireFormat::HandshakeReq> req(datagram.payload);
    printf("Recevied handshake request: {connection Id = %u, file size = %zu, "
                   "OTI_COMMON = %lu, OTI_SCHEME_SPECIFIC = %u}\n",
           req->connectionId, req->fileSize, req->otiCommon, req->otiScheme);
    sendInWireFormat<WireFormat::HandshakeResp>(
            udpSocket.get(), senderAddr, uint32_t(req->connectionId));

    // Start receiving symbols
    RaptorQDecoder decoder(req->otiCommon, req->otiScheme);

    // Create the receiving file
    int fd = SystemCall("open the file to be written",
            open("demo.out", O_RDWR | O_CREAT | O_TRUNC, (mode_t)0600));
    size_t paddedSize = getPaddedSize(req->fileSize);
    SystemCall("Stretch the file: lseek", lseek(fd, paddedSize - 1, SEEK_SET));
    SystemCall("Stretch the file: write a NULL char", write(fd, "", 1));
    void* start = mmap(NULL, paddedSize, PROT_WRITE, MAP_SHARED, fd, 0);
    if (start == MAP_FAILED) {
        printf("mmap failed:%s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    std::vector<Alignment*> blockStart(decoder.blocks() + 1);
    blockStart[0] = reinterpret_cast<Alignment*>(start);
    for (uint8_t sbn = 0; sbn < decoder.blocks(); sbn++) {
        blockStart[sbn + 1] = blockStart[sbn] +
                decoder.block_size(sbn) / sizeof(Alignment);
    }

    std::chrono::time_point<std::chrono::system_clock> nextAckTime =
            std::chrono::system_clock::now() + HEART_BEAT_INTERVAL;
    Bitmask256 decodedBlocks;
    while (decodedBlocks.count() < decoder.blocks()) {
        datagram = udpSocket->recv();
        Tub<WireFormat::DataPacket> dataPacket(datagram.payload);
        uint8_t sbn = dataPacket->id >> 24;
        printf("Received sbn = %u, esi = %u\n",
               static_cast<uint32_t>(sbn), ((dataPacket->id << 8) >> 8));

        auto currTime = std::chrono::system_clock::now();
        if (currTime > nextAckTime) {
            printf("Sent Heartbeat ACK\n");
            sendInWireFormat<WireFormat::Ack>(udpSocket.get(), senderAddr,
                                              decodedBlocks.bitset);
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
            // send ACK for block sbn
            printf("Block %u decoded.\n", static_cast<int>(sbn));
            decodedBlocks.set(sbn);
        }
    }
    SystemCall("msync", msync(start, paddedSize, MS_SYNC));
    SystemCall("munmap", munmap(start, paddedSize));
    SystemCall("truncate the padding at the end of the file",
            ftruncate(fd, req->fileSize));
    SystemCall("close fd", close(fd));

    assert(decodedBlocks.count() == decoder.blocks());
    printf("File decoded successfully.\n");

    // Teardown phase: keep sending ACK until we do not get any packet from
    // the sender for a while

    // Clean up the udp socket recv buffer first
    fcntl(udpSocket->fd_num(), F_SETFL, O_NONBLOCK);
    while (poll(udpSocket.get(), datagram)) { }
    std::chrono::time_point<std::chrono::system_clock> stopTime =
            std::chrono::system_clock::now() + TEAR_DOWN_DURATION;
    while (true) {
        if (poll(udpSocket.get(), datagram)) {
            stopTime = std::chrono::system_clock::now() + TEAR_DOWN_DURATION;
        }

        if (std::chrono::system_clock::now() < stopTime) {
            sendInWireFormat<WireFormat::Ack>(udpSocket.get(), senderAddr,
                                              decodedBlocks.bitset);
            std::this_thread::sleep_for(HEART_BEAT_INTERVAL);
        } else {
            break;
        }
    }

    return EXIT_SUCCESS;
}