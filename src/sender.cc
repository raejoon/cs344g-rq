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
#include <RaptorQ.hpp>

#include "socket.hh"

int main( int argc, char *argv[] )
{
    /* check the command-line arguments */
    if ( argc < 1 ) { abort(); } /* for sticklers */

    if ( argc != 3 ) {
        std::cerr << "Usage: " << argv[ 0 ] << " HOST PORT" << std::endl;
        return EXIT_FAILURE;
    }

    /* fetch command-line arguments */
    const std::string host { argv[ 1 ] }, port { argv[ 2 ] };

    /* XXX your code here */

    /* construct UDP socket */
    UDPSocket udp_socket;

    /* connect the socket to the given host and port */
    udp_socket.connect(Address(host, static_cast<uint16_t>(stoi(port))));

    /* send payload */
    udp_socket.send("Hello, world.");

    return EXIT_SUCCESS;
}