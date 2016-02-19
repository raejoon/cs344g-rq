/*
  udp-sender

  Given a host and port on the command line,
  this program should:

  (1) Construct a UDP socket
  (2) Connect the socket to the given host and port
  (3) Use the socket to send a payload of the
      string: "Hello, world."
*/

#include <iostream>
#include <fstream>
#include <RaptorQ.hpp>

#include "buffer.hh"
#include "socket.hh"
#include "wire_format.hh"

typedef uint32_t  Alignment;

template<typename Alignment>
size_t readFile(std::string filename, std::vector<Alignment>& content)
{
    std::ifstream infile;
    infile.open(filename, std::ios::in | std::ios::binary);
    infile.unsetf(std::ios::skipws);

    std::streampos filesz;
    infile.seekg(0, std::ios::end);
    filesz = infile.tellg();
    infile.seekg(0, std::ios::beg);

    size_t dataLength = static_cast<size_t>(
            std::ceil(static_cast<float>(filesz) / sizeof(Alignment)));
    content.reserve(dataLength);

    std::istreambuf_iterator<char> it(infile);
    std::istreambuf_iterator<char> eos;

    uint8_t shift = 0;
    Alignment tmp = 0;
    while (it != eos) {
        tmp += static_cast<uint8_t>(*it) << (shift * 8);
        shift++;
        if (shift >= sizeof(Alignment)) {
            content.push_back(tmp);
            shift = 0;
            tmp = 0;
        }
        it++;
    }
    if (shift != 0) content.push_back(tmp);

    return static_cast<size_t>(filesz);
}

int main( int argc, char *argv[] )
{
    /* check the command-line arguments */
    if ( argc < 1 ) { abort(); } /* for sticklers */

    if ( argc != 4 ) {
        std::cerr << "Usage: " << argv[ 0 ] << " HOST PORT FILE" << std::endl;
        return EXIT_FAILURE;
    }

    /* read file */
    std::vector<Alignment> dataToSend;
    size_t fileSize = readFile(argv[3], dataToSend);

    /*  */
    const uint16_t subSymbolSize = 1 << 10;
    const uint16_t symbolSize = subSymbolSize;
    const uint32_t numSymPerBlock = 100;
    RaptorQ::Encoder<typename std::vector<Alignment>::iterator,
            typename std::vector<Alignment>::iterator>
            encoder(dataToSend.begin(),
                    dataToSend.end(),
                    subSymbolSize,
                    symbolSize,
                    numSymPerBlock * symbolSize);

    Buffer payload;
    WireFormat::HandshakeReq* p = payload.emplaceAppend<WireFormat::HandshakeReq>();
    p->fileSize = fileSize;
    p->commonData = encoder.OTI_Common();
    p->schemeSpecificData = encoder.OTI_Scheme_Specific();

    /* fetch command-line arguments */
    const std::string host { argv[ 1 ] }, port { argv[ 2 ] };

    /* XXX your code here */

    /* construct UDP socket */
    UDPSocket udp_socket;

    /* connect the socket to the given host and port */
    udp_socket.connect(Address(host, port));

    /* send payload */
    std::cout << "Sending " << payload.size() << " bytes." << std::endl;
    std::cout << "fileSize = " << fileSize << std::endl;
    std::cout << "OTI_COMMON = " << encoder.OTI_Common() << std::endl;
    std::cout << "OTI_SCHEME_SPECIFIC = " << encoder.OTI_Scheme_Specific() << std::endl;
    udp_socket.sendbytes(payload.c_str(), payload.size());

    return EXIT_SUCCESS;
}