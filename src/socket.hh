#ifndef SOCKET_HH
#define SOCKET_HH

#include <functional>

#include "address.hh"
#include "file_descriptor.hh"

/* class for network sockets (UDP, TCP, DCCP, etc.) */
class Socket : public FileDescriptor
{
private:
  /* get the local or peer address the socket is connected to */
  Address get_address( const std::string & name_of_function,
		       const std::function<int(int, sockaddr *, socklen_t *)> & function ) const;

protected:
  /* default constructor */
  Socket( const int domain, const int type );

  /* construct from file descriptor */
  Socket( FileDescriptor && s_fd, const int domain, const int type );

  /* set socket option */
  template <typename option_type>
  void setsockopt( const int level, const int option, const option_type & option_value );

public:
  /* bind socket to a specified local address (usually to listen/accept) */
  void bind( const Address & address );

  /* connect socket to a specified peer address */
  void connect( const Address & address );

  /* accessors */
  Address local_address( void ) const;
  Address peer_address( void ) const;

  /* allow local address to be reused sooner, at the cost of some robustness */
  void set_reuseaddr( void );
};

/* UDP socket */
class UDPSocket : public Socket
{
public:
  UDPSocket() : Socket( AF_INET6, SOCK_DGRAM ) {}

  struct received_datagram {
    Address source_address;
    uint64_t timestamp;
    char*   payload;
    ssize_t recvlen;
  };

  /* receive datagram, timestamp, and where it came from */
  received_datagram recv( void );

  /* send datagram to specified address */
  void sendto( const Address & peer, const std::string & payload );
  void sendbytesto( const Address & peer, const char *payload, size_t length);
  
  /* send datagram to connected address */
  void send( const std::string & payload );
  void sendbytes( const char *payload, size_t length);
  
  /* turn on timestamps on receipt */
  void set_timestamps( void );
};

/* TCP socket */
class TCPSocket : public Socket
{
private:
  /* private constructor used by accept() */
  TCPSocket( FileDescriptor && fd ) : Socket( std::move( fd ), AF_INET6, SOCK_STREAM ) {}

public:
  TCPSocket() : Socket( AF_INET6, SOCK_STREAM ) {}

  /* mark the socket as listening for incoming connections */
  void listen( const int backlog = 16 );

  /* accept a new incoming connection */
  TCPSocket accept( void );
};

/* DCCP socket */
class DCCPSocket : public Socket
{
private:
  /* private constructor used by accept() */
  DCCPSocket( FileDescriptor && fd ) 
    : Socket( std::move( fd ), AF_INET6, SOCK_DCCP ) 
  {}

public:
  DCCPSocket() : Socket( AF_INET6, SOCK_DCCP ) {}

  /* mark the socket as listening for incoming connections */
  void listen( const int backlog = 16 );

  /* accept a new incoming connection */
  DCCPSocket accept( void );

  /* send datagram to connected address */
  int send( const char* payload, int payload_len );

  /* receive datagram from connected address */
  char* recv( void );
};

#endif /* SOCKET_HH */
