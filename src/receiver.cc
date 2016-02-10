#include <iostream>
#include <cstdlib>
#include <fstream>
#include <string>

#include "socket.hh"

UDPSocket listen_wrapper(Address& senderAddr) {
    UDPSocket sock = UDPSocket();
    sock.bind(Address("0", "0"));
    Address localaddr = sock.local_address();
    std::cerr << localaddr.ip() << " " << localaddr.port() << std::endl;

    UDPSocket::received_datagram dg = sock.recv();

    std::cout << dg.payload;

    senderAddr = dg.source_address;

    return sock;
}

size_t respondHandshake(UDPSocket& sk, Address& senderAddr) {
    size_t filesz = stoi(sk.recv().payload);     
    const char data[2] = {(char)0xf2, '\0'};
    const std::string payload = data;
    sk.sendto(senderAddr, payload);
    std::cout << "Ready to receive file of size: " << filesz << std::endl;
    return filesz;
}

int main( int argc, char *argv[] )
{
    /* check the command-line arguments */
    if ( argc < 1 ) { abort(); } /* for sticklers */
    if ( argc != 1 ) {
        cerr << "Usage: " << argv[ 0 ] << endl;
        return EXIT_FAILURE;
    }

    Address senderAddr;
    UDPSocket sk = listen_wrapper(senderAddr);
    size_t filesz = respondHandshake(sk, senderAddr);

    std::ofstream outfile;
    outfile.open("recvfile", ios::out | ios::binary);

    size_t recvsz = 0;
    while(recvsz < filesz) {
        std::string payload = sk.recv().payload;
        size_t bufsz = payload.length();
        const char* buffer = payload.c_str();

        recvsz += bufsz; 
        if (recvsz > filesz) outfile.write(buffer, bufsz - (recvsz - filesz));
        else outfile.write(buffer, bufsz);
    }

    cout << "Got entire file" << endl;
    outfile.close();
    return EXIT_SUCCESS;
}
