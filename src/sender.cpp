char* getMetadata();
char* getNextSymbol();

int main(int argc, char* argv[]) {
    // establish connection
    UDPSocket sk = connect_wrapper(address, port);

    // get file
    std::string filename;    
    std::vector<alignment>::iterator fileBegin = getFileBegin(filename);
    std::vector<alignment>::iterator fileEnd = getFileEnd(filename);

    // encode and send
    RaptorQ::Encoder encode(fileBegin, fileEnd, subSymbolSize, symbolSize, maxBlockSize);
    
    // send metadata
    int result = requestHandshake(sk, metadata); // block until ACK, might need total num of blocks
    if (result == -1) { … }

    // send symbols
    for (block_iter : block) {
         while (true) {
            sk.send(getNextSymbol());
              if (pollIfFinalAckArrives()) {
    break;
              }         
         }
    }

    return 0;
}
