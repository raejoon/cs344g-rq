#include <iostream>
#include <fstream>
#include <RaptorQ.hpp>
#include <chrono>
#include <iomanip>

#include "tub.hh"
#include "common.hh"
#include "socket.hh"
#include "wire_format.hh"

struct progress_t {
  std::chrono::time_point<std::chrono::system_clock> start;
  std::chrono::time_point<std::chrono::system_clock> current;
  double elapsed_seconds;
  uint64_t filesize;
  uint64_t sentsize;
  progress_t() :
    start(std::chrono::system_clock::now()),
    current(std::chrono::system_clock::now()),
    elapsed_seconds(0),
    filesize(0),
    sentsize(0) {}
};

void initialize_progress(progress_t& progress, uint64_t filesize) {
  progress.filesize = filesize;
  progress.start = std::chrono::system_clock::now();
}

void update_progress(progress_t& progress, uint64_t sentsize) {
  progress.sentsize = sentsize;
  progress.current = std::chrono::system_clock::now();
  std::chrono::duration<double> diff = progress.current - progress.start;
  progress.elapsed_seconds = diff.count();
}

void update_progressbar(progress_t& progress) {
  float fraction = progress.sentsize/((float) progress.filesize);
  int barwidth = 70;
  std::cout <<"[";
  int pos = barwidth * fraction;
  for (int i = 0; i < barwidth; ++i) {
    if (i < pos) std::cout << "=";
    else if (i == pos) std::cout << ">";
    else std::cout << " ";
  }
  std::cout << "] " << int(fraction * 100.0) << " % ";

  int hrs = int(progress.elapsed_seconds) / 3600;
  int mins = (int(progress.elapsed_seconds) % 3600) / 60;
  int secs = (int(progress.elapsed_seconds)) % 60;
  std::cout << std::setw(2) << std::setfill('0') << hrs << ":";
  std::cout << std::setw(2) << std::setfill('0') << mins << ":";
  std::cout << std::setw(2) << std::setfill('0') << secs << " ";

  if (progress.elapsed_seconds > 1.0) {
    double rate = progress.sentsize / progress.elapsed_seconds;
    int leftsize = progress.filesize - progress.sentsize;
    double remaining_seconds = leftsize / rate;

    hrs = int(remaining_seconds) / 3600;
    mins = (int(remaining_seconds) % 3600) / 60;
    secs = (int(remaining_seconds)) % 60;
    std::cout << "ETA: ";
    std::cout << std::setw(2) << std::setfill('0') << hrs << ":";
    std::cout << std::setw(2) << std::setfill('0') << mins << ":";
    std::cout << std::setw(2) << std::setfill('0') << secs;
  } 
  else std::cout << "ETA: --:--:--";

  std::cout << "\r";
  std::cout.flush();
  if (fraction >= 1.0) std::cout << std::endl;
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

/**
 * Send a single symbol to the given UDP socket.
 *
 * \param[in]
 *      The UDP socket.
 * \param[in,out] symbolIterator
 *      A symbol iterator referencing the symbol about to send; the position
 *      of the iterator will be advanced by one after this function is called.
 */
void sendSymbol(UDPSocket *udpSocket,
                RaptorQSymbolIterator &symbolIterator)
{
    static RaptorQSymbol symbol {0};
    auto begin = symbol.begin();
    (*symbolIterator)(begin, symbol.end());

    // Wait until the congestion controller gives us a pass
    congestionControl();
    sendInWireFormat<WireFormat::DataPacket>(udpSocket,
            udpSocket->peer_address(), (*symbolIterator).id(), symbol.data());
    ++symbolIterator;
}

void transmit(RaptorQEncoder& encoder,
              UDPSocket* udpSocket)
{
    progress_t progress;
    int sentsize = 0;
    initialize_progress(progress, encoder.blocks());

    std::vector<RaptorQSymbolIterator> repairSymbolIters;
    for (const auto& block : encoder) {
        repairSymbolIters.push_back(block.begin_repair());
    }

    // Represents blocks that are decoded by the receiver
    Bitmask256 decodedBlocks;

//    const static uint32_t MAX_SYMBOL_SENT = MAX_SYM_PER_BLOCK * encoder.blocks() * 2;
    const static uint32_t REPAIR_SYMBOL_SEND_INTERVAL = MAX_SYM_PER_BLOCK / 10;
    uint32_t numSourceSymbolSent = 0;
    UDPSocket::received_datagram datagram {Address(), 0, 0, 0};

    update_progress(progress, sentsize);
    update_progressbar(progress);
    for (uint8_t currBlock = 0; currBlock < encoder.blocks(); currBlock++) {
        const auto &block = *encoder.begin().operator++(currBlock);
        RaptorQSymbolIterator sourceSymbolIter = block.begin_source();
        for (int esi = 0; esi < block.symbols(); esi++) {
            // Send i-th source symbol of block sbn
            sendSymbol(udpSocket, sourceSymbolIter);
            numSourceSymbolSent++;
            if (numSourceSymbolSent % REPAIR_SYMBOL_SEND_INTERVAL == 0) {
                // Poll to see if any ACK arrives
                while (poll(udpSocket, datagram)) {
                    Tub<WireFormat::Ack> ack(datagram.payload);
                    decodedBlocks.bitwiseOr(Bitmask256(ack->bitmask));
                    printf("Received ACK\n");
                }

                // Send repair symbols of previous blocks
                for (uint8_t prevBlock = 0; prevBlock < currBlock; prevBlock++) {
                    if (!decodedBlocks.test(prevBlock)) {
                        sendSymbol(udpSocket, repairSymbolIters[prevBlock]);
                    }
                }
            }
        }
        sentsize += 1;
        update_progress(progress, sentsize);
        update_progressbar(progress);
    }

    while (decodedBlocks.count() < encoder.blocks()) {
        // Send repair symbols for in round-robin
        for (uint8_t sbn = 0; sbn < encoder.blocks(); sbn++) {
            if (!decodedBlocks.test(sbn)) {
                sendSymbol(udpSocket, repairSymbolIters[sbn]);
            }
        }

        // Poll to see if any ACK arrives
        while (poll(udpSocket, datagram)) {
            Tub<WireFormat::Ack> ack(datagram.payload);
            decodedBlocks.bitwiseOr(Bitmask256(ack->bitmask));
            printf("Received ACK\n");
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
    FileWrapper<Alignment> file {argv[3]};
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

    return EXIT_SUCCESS;
}
