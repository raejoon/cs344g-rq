#include <iostream>
#include <RaptorQ.hpp>

#include "buffer.hh"
#include "socket.hh"
#include "wire_format.hh"

int main( int argc, char *argv[] )
{
    /* check the command-line arguments */
    if ( argc < 1 ) { abort(); } /* for sticklers */

    if ( argc != 1 ) {
        std::cerr << "Usage: " << argv[ 0 ] << std::endl;
        return EXIT_FAILURE;
    }

    /* XXX your code here */

    /* construct UDP socket */
    UDPSocket udp_socket;

    /* "bind" the socket to host "0", port "0" */
    udp_socket.bind(Address("0", 0));

    /* print out the local address to standard error (std::cerr) */
    /* the output should look something like "0.0.0.0 12345\n" */
    Address local_addr = udp_socket.local_address();
    std::cerr << local_addr.ip() << " " << local_addr.port() << std::endl;

    /* receive one UDP datagram, and print out the payload */
    UDPSocket::received_datagram datagram = udp_socket.recv();
    Buffer payload(datagram.payload, datagram.recvlen);
    const WireFormat::HandshakeReq* req = payload.get<WireFormat::HandshakeReq>(0);

    std::cout << "Sender address: " << datagram.source_address.to_string() << std::endl;
    std::cout << "Received handshake request " << datagram.recvlen << " bytes." << std::endl;
    std::cout << "fileSize = " << req->fileSize << std::endl;
    std::cout << "OTI_COMMON = " << req->commonData << std::endl;
    std::cout << "OTI_SCHEME_SPECIFIC = " << req->schemeSpecificData << std::endl;

    /* send back HandshakeResp */
//    payload.clear();
//    WireFormat::HandshakeResp* resp = payload.emplaceAppend<WireFormat::HandshakeResp>();
//    std::memcpy(resp->addr, local_addr.ip().c_str(), local_addr.ip().length());
//    resp->port = local_addr.port();

    return EXIT_SUCCESS;
}