#include <iostream>
#include <cstdlib>
#include <fstream>
#include "socket.hh"
#include "RaptorQ.hpp"

UDPSocket connectWrapper(Address addr) {
    UDPSocket sock = UDPSocket();
    sock.connect(addr);
    sock.send("Sender: I have something to send\n");

    return sock;
}

int requestHandshake(UDPSocket& sock, OTI_Common_Data OTICommon, OTI_Scheme_Specific_Data OTIScheme) {
    // TODO: add failure code and return -1
    while (true) {
        std::string common_s = to_string(OTICommon);
        std::string scheme_s = to_string(OTIScheme);
        std::string payload = common_s + scheme_s;
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
    if ( argc != 3 ) {
      std::cerr << "Usage: " << argv[ 0 ] << " HOST PORT" << std::endl;
      return EXIT_FAILURE;
    }

    // prepare file
    std::vector<uint32_t> dataToSend;
    size_t filesz = readFile("demo.txt", dataToSend);

    // prepare encoder
    const uint16_t subSymbolSize = 1 << 10;
    const uint16_t symbolSize = 1 << 10;
    
    size_t numOfAlignPerSymbol = static_cast<size_t>(
        std::ceil(static_cast<float>(symbolSize) / sizeof(Alignment)));

    RaptorQ::Encoder<typename std::vector<Alignment>::iterator,
        typename std::vector<Alignment>::iterator>
            encoder(dataToSend.begin(), 
                dataToSend.end(),
                subSymbolSize,
                symbolSize,
                1000000000,
                );

    std::vector<std::pair<SymbolId, std::vector<Alignment>>> encodedSymbols;

    encoder.precompute(1, false);
  
    // connection
    const std::string host { argv[ 1 ] }, port { argv[ 2 ] };
    UDPSocket sock = connectWrapper(Address(host, port));
   
    // handshake
    int result = requestHandshake(sock, encoder.OTI_Common(), encoder.OTI_Scheme_Specific());
    if (result == -1) std::cerr << "Something is wrong!" << std::endl;
  
    // TODO: need to add ack polling code
    int block_cnt = 0;
    for (const auto& block : encoder) {
        block_cnt++;
        for (auto iter = block.begin_source(); iter != block.end_source(); ++iter) {
            std::vector<Alignment> sourceSymbol;
            sourceSymbol.reserve(numOfAlignPerSymbol);
            sourceSymbol.insert(sourceSymbol.begin(), numOfAlignPerSymbol, 0);
            auto begin = sourceSymbol.begin();
            (*iter) (begin, sourceSymbol.end());

            // TODO: add send code (but can't use keith's code! sending as a
            // string cuts the payload because of interpreting 0x00 as null terminator)

        }

        for (auto iter = block.begin_repair(); iter != block.end_repair(block.max_repair()); ++iter) {
            std::vector<Alignment> repairSymbol;
            repairSymbol.reserve(numOfAlignPerSymbol);
            repairSymbol.insert(repairSymbol.begin(), numOfAlignPerSymbol, 0);
            auto begin = repairSymbol.begin();
            (*iter) (begin, repairSymbol.end());

            // TODO: add send code (but can't use keith's code! sending as a
            // string cuts the payload because of interpreting 0x00 as null terminator)

        }
    }

    // clean up
    infile.close();

    return EXIT_SUCCESS;
}
