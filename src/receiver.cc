#include <iostream>
#include <cstdlib>

#include "socket.hh"

UDPSocket listen_wrapper() {
    UDPSocket sock = UDPSocket();
    sock.bind(Address("0", "0"));
    Address localaddr = sock.local_address();
    std::cerr << localaddr.ip() << " " << localaddr.port() << std::endl;

    std::cout << sock.recv().payload;

    return sock;
}

int main(int argc, char* argv[]) {
    // check the command-line arguments
    if (argc < 1) abort();
    if (argc != 1) {
        std::cerr << "Usage: " << argv[0] << endl;
        return EXIT_FAILURE;
    }

    // listen connection
    UDPSocket sk = listen_wrapper();
    /*
    // block until next request; send back ACK
    int result = processHandshake(memory_size, common_metadata, scheme_specific_metadata);   // might need total num of blocks
    if (result == -1) {...}

    // open receive file
    ofstream ofile = openFile(std::string filename);

    int numBlocks = 0;
    while (true) {
        // double-check the metadata can be reused for different instances
        RaptorQ::Decoder decoder(common_metadata, scheme_specific_metadata);

        std::vector<Alignment> block;
        while (!decoder.decode(block.begin(), block.end())) {
            std::vector<Alignment> symbol = receiveSymbol(sk);
            decoder.add_symbol(symbol.begin(), symbol.end());
        }
        sendACK();
        size_t file_size = appendToFile(ofile, block);
        if (++numBlocks == totalNumOfBlocks) break;
    }
    */
    return EXIT_SUCCESS;
}
