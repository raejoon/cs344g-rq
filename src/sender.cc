#include <iostream>
#include <cstdlib>
#include "socket.hh"

//char* getMetadata();
//char* getNextSymbol();

UDPSocket connect_wrapper(Address addr) {
    UDPSocket sock = UDPSocket();
    sock.connect(addr);
    sock.send("I have something to send.");

    return sock;
}

int requestHandshake(UDPSocket sock) {
    // TODO: metadata packetization code
    while (true) {
        sock.send("This is the metadata for decoding");

        const unsigned char* recvPayload = (const unsigned char*) sock.recv().payload.c_str();
        if (recvPayload[0] == 0xf2) break;
    }

    std::cout << "Metadata sent" << std::endl;

    return 0;
}

int main(int argc, char* argv[]) {
    // check the command-line arguments
    if (argc < 1) abort(); 
    if (argc != 3 ) {
      std::cerr << "Usage: " << argv[0] << " HOST PORT " << std::endl;
      return EXIT_FAILURE;
    }

    // fetch command-line arguments
    const std::string host {argv[1]}, port {argv[2]};

    // establish connection
    UDPSocket sk = connect_wrapper(Address(host, port));

    // get file
    /*
    std::string filename;    
    std::vector<alignment>::iterator fileBegin = getFileBegin(filename);
    std::vector<alignment>::iterator fileEnd = getFileEnd(filename);
    */

    // encode and send
    //RaptorQ::Encoder encode(fileBegin, fileEnd, subSymbolSize, symbolSize, maxBlockSize);
    
    // send metadata
    //int result = requestHandshake(sk, metadata); // block until ACK, might need total num of blocks
    //if (result == -1) {}

    // send symbols
    /*
    for (block_iter : block) {
        while (true) {
            sk.send(getNextSymbol());
            if (pollIfFinalAckArrives()) {
                break;
            }         
        }
    }
    */

    return EXIT_SUCCESS;
}
