#include <iostream>
#include <RaptorQ.hpp>

#include "socket.hh"

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
    std::cout << udp_socket.recv().payload;

    return EXIT_SUCCESS;
}