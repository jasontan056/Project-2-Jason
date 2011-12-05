#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include "checksum.c"
#include "packetstruct.h"

#define MAXBUFLEN 100
#define MYPORT "3000"

void error(char *msg)
{
  perror(msg);
  exit(1);
}

int main( int argc, char *argv[] )	  
{
  int sockfd;
  FILE *fp;
  struct sockaddr_in recv_addr, send_addr;
  struct sockaddr their_addr;
  struct hostent *sender;
  int portno;
  int numbytes;
  char buf[ MAXBUFLEN ];
  char packetBuf[ sizeof( struct packet ) ];
  struct packet ackPacket;
  struct packet* p;
  socklen_t addr_len;
  int expectedSeqNum;
  word16 checksumValue;
  
  // arguments:
  // sender_hostname, sender_portnumber, filename
  if ( argc != 4 ) {
    fprintf( stderr,"Invalid Arguments\n" );
    exit( 1 );
  }

  // create a socket
  sockfd = socket( AF_INET, SOCK_DGRAM, 0 );
  if ( sockfd < 0 )
    error( "ERROR opening socket" );

  //construct receiver's address
  bzero( (char*) &recv_addr, sizeof( recv_addr ) );
  portno = atoi( MYPORT );
  recv_addr.sin_family = AF_INET; // IPv4
  recv_addr.sin_addr.s_addr = INADDR_ANY; //bind to own IP
  recv_addr.sin_port = htons( portno );

  // bind the socket to the receiver's address
  if ( bind( sockfd, (struct sockaddr*) &recv_addr,
	   sizeof(recv_addr)) < 0) 
    error("ERROR on binding");

  printf( "sending file name...\n" );

  //construct sender's address
  sender = gethostbyname( argv[ 1 ]);
  if ( sender == NULL ) {
    fprintf( stderr,"ERROR, no such host\n" );
    exit( 0 );
  }
  bzero( (char*) &send_addr, sizeof( send_addr ) );
  portno = atoi( argv[ 2 ] );
  send_addr.sin_family = AF_INET; // IPv4
  bcopy( (char *) sender->h_addr, (char *) &send_addr.sin_addr.s_addr, sender->h_length);
  send_addr.sin_port = htons( portno );

  // send file name to the sender to request the file
  if ( ( numbytes = sendto( sockfd, argv[ 3 ], strlen( argv[ 3 ]) + 1, 0,
			    (struct sockaddr*) &send_addr, sizeof( send_addr ) ) ) == -1 ) {
    perror("sendto");
    exit(1);
  }

  // wait for response to server. only proceed if server has the file
  if ( ( numbytes = recvfrom( sockfd, packetBuf, sizeof( struct packet ), 0,
			      &their_addr, &addr_len ) ) == -1 ) {
    perror( "recvfrom" );
    exit( 1 );
  }
  p = (struct packet*) packetBuf;
  if ( p->dPacket.type == -1 ) {
    fprintf( stderr, "Server Error: %s", p->dPacket.data );
    close( sockfd );
    exit( 1 );
  }

  printf ( "File Transfer Beginning\n" );

  // open/create the file requested
  fp = fopen( argv[ 3 ], "w" );

  // receives packets, writing them to the file
  expectedSeqNum = 0;
  // construct ACK
  ackPacket.dPacket.type = 1;
  ackPacket.dPacket.ackNum = -1;
  ackPacket.checksum = checksum( (byte*) &(ackPacket.dPacket), sizeof( ackPacket.dPacket ) );
  while ( 1 ) {
    if ( ( numbytes = recvfrom( sockfd, packetBuf, sizeof( struct packet ), 0,
				&their_addr, &addr_len ) ) == -1 ) {
      perror( "recvfrom" );
      exit( 1 );
    }
    p = (struct packet*) packetBuf;
    checksumValue = checksum( (byte*) &(p->dPacket), sizeof( p->dPacket) );
    if ( p->checksum == checksumValue && p->dPacket.seqNum == expectedSeqNum ) {
      printf( "Received Packet with Sequence Number %i\n", p->dPacket.seqNum );
      
      // write data to file
      fwrite( p->dPacket.data, sizeof( p->dPacket.data[0] ),
	      p->dPacket.dataLength/sizeof( p->dPacket.data[0] ), fp );
      
      // construct ACK
      ackPacket.dPacket.type = 1;
      ackPacket.dPacket.ackNum = expectedSeqNum;
      ackPacket.checksum = checksum( (byte*) &(ackPacket.dPacket), sizeof( ackPacket.dPacket ) );

      // send ack
      if ( ( numbytes = sendto( sockfd, (char*) &ackPacket, sizeof(ackPacket), 0,
				(struct sockaddr*) &send_addr, sizeof( send_addr ) ) ) == -1 ) {
	perror("sendto");
	exit(1);
      }
      printf( "Sent ACK %i\n", ackPacket.dPacket.ackNum);
      
      // increment expected sequence number
      expectedSeqNum++;

      // break if done receiving the file
      if ( p->dPacket.type == 2 ) {
	break;
      }
    }
    // if received corrupted or packet with wrong sequence number
    else {
      if ( expectedSeqNum != p->dPacket.seqNum ) {
	printf( "Expected Packet with Sequence Number %i, but Received Packet %i\n",
		expectedSeqNum, p->dPacket.seqNum );
      } else {
	printf( "Packet %i failed checksum. Checksum was %i but got %i\n", expectedSeqNum,
		p->checksum, checksumValue );
      }

      // send duplicate ack
      if ( ( numbytes = sendto( sockfd, (char*) &ackPacket, sizeof(ackPacket), 0,
				(struct sockaddr*) &send_addr, sizeof( send_addr ) ) ) == -1 ) {
	perror("sendto");
	exit(1);
      }
      printf( "Sent ACK %i\n", ackPacket.dPacket.ackNum);
    }
  }
  
  fclose( fp );
  close( sockfd );

  printf( "File Successfully Received\n" );
}
