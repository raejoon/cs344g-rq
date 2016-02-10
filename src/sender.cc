#include <iostream>
#include <cstdlib>
#include <fstream>
#include "socket.hh"

UDPSocket connectWrapper(Address addr) {
    UDPSocket sock = UDPSocket();
    sock.connect(addr);
    sock.send("Sender: I have something to send\n");

    return sock;
}

int requestHandshake(UDPSocket& sock, size_t filesz) {
    // TODO: add failure code and return -1
    // TODO: add metadata code for libraptorq
    while (true) {
        std::string payload = to_string(filesz);
        sock.send(payload);
        const unsigned char* recvPayload = (const unsigned char*) sock.recv().payload.c_str();
        if (recvPayload[0] == 0xf2) {
          cout << "Metadata: received ack\n";
          break;
        }
    }
    return 0;
}

size_t getFileSize(std::ifstream& infile) {
    std::streampos filesz;
    infile.seekg(0, std::ios::end);
    filesz = infile.tellg();
    infile.seekg(0, std::ios::beg);

    return (size_t) filesz;
}

int main( int argc, char *argv[] )
{
    /* check the command-line arguments */
    if ( argc < 1 ) { abort(); } /* for sticklers */
    if ( argc != 3 ) {
      std::cerr << "Usage: " << argv[ 0 ] << " HOST PORT" << std::endl;
      return EXIT_FAILURE;
    }


    // prepare file
    std::ifstream infile;
    infile.open("demo.txt", std::ios::in | std::ios::binary);
    infile.unsetf(std::ios::skipws);
    size_t filesz = getFileSize(infile);
  
    // connection
    const std::string host { argv[ 1 ] }, port { argv[ 2 ] };
    UDPSocket sock = connectWrapper(Address(host, port));
   
    // handshake
    int result = requestHandshake(sock, filesz);
    if (result == -1) std::cerr << "Something is wrong!" << std::endl;
  
    // file transfer
    // TODO: change for libraptorq
    size_t bufsz = 8;
    char *buffer = new char[bufsz + 1];
    buffer[8] = '\0';
    size_t sendsz = 0;
    while(true) {
      infile.read(buffer, (int)bufsz);

      std::string payload = (const char *) buffer;
      sock.send(payload);

      sendsz += bufsz;
      if (sendsz >= filesz) break;
    }

    // clean up
    delete[] buffer;
    infile.close();

    return EXIT_SUCCESS;
}
