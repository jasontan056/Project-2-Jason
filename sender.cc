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
#include <sys/stat.h>
#include <iostream>
#include <vector>
#include <sys/time.h>
#include "checksum.c"
#include "packetstruct.h"

using namespace std;

#define MAXBUFLEN 100

void error(char *msg)
{
  perror(msg);
  exit(1);
}

void sendPacketWithLossAndCorruption( double probLoss, double probCor, int sockfd,
				      struct packet* p, struct sockaddr &their_addr ) {
  bool corruptedPacket = false;
  if ( ( (float) rand() )/RAND_MAX < probLoss ) {
    printf( "Dropped Packet with Sequence Number %i\n", p->dPacket.seqNum );
  } else {
    if ( ( (float) rand() )/RAND_MAX < probCor ) {
      // make the packet appear corrupted
      p->checksum++;
      corruptedPacket = true;
      printf( "Corrupting Packet %i\n", p->dPacket.seqNum );
    }
    if ( sendto( sockfd, (char*) p, sizeof(*p), 0, &their_addr, sizeof( their_addr ) ) == -1 ) {
      perror("sendto");
      exit(1);
    }
    printf( "Sent Packet with Sequence Number %i\n", p->dPacket.seqNum );

    if ( corruptedPacket ) {
      // fix the packet we corrupted
      p->checksum--;
      corruptedPacket = false;
    }
  }
}
				      
int main( int argc, char *argv[] )	  
{
  int sockfd;
  FILE *fp;
  struct sockaddr_in send_addr;
  struct sockaddr their_addr;
  int numbytes;
  int portno;
  int seqNum;
  char buf[ MAXBUFLEN ];
  socklen_t addr_len;
  struct packet pTemp;
  struct packet* p;
  vector<struct packet> packetBuffer;
  int windowSize, base, nextSeqNum;
  fd_set readfds;
  struct timeval tv;
  char packetBuf[ sizeof( struct packet ) ];
  timeval oldTime, newTime;
  bool timerRunning = false;
  double probLoss, probCor;
  bool corruptedPacket = false;

  tv.tv_sec = 0;
  tv.tv_usec = 0;

  // arguments:
  // portnumber, CWnd, Pl, PC
  if ( argc != 5 ) {
    fprintf( stderr,"Invalid Arguments\n" );
    exit( 1 );
  }

  probLoss = atof( argv[ 3 ] );
  probCor = atof( argv[ 4 ] );

  // create a socket
  sockfd = socket( AF_INET, SOCK_DGRAM, 0 );
  if ( sockfd < 0 )
    error( (char*) "ERROR opening socket" );
  bzero( (char*) &send_addr, sizeof( send_addr ) );
  portno = atoi( argv[ 1 ] );
  send_addr.sin_family = AF_INET; // IPv4
  send_addr.sin_addr.s_addr = INADDR_ANY; //bind to own IP
  send_addr.sin_port = htons( portno );

  if ( bind( sockfd, (struct sockaddr*) &send_addr,
	   sizeof(send_addr)) < 0) 
    error( (char*) "ERROR on binding" );

  printf( "sender waiting for file name...\n" );

  addr_len = sizeof their_addr;
  if ( ( numbytes = recvfrom( sockfd, buf, MAXBUFLEN, 0,
			      &their_addr, &addr_len ) ) == -1 ) {
    perror( "recvfrom" );
    exit( 1 );
  }

  // Open the file requested
  fp = fopen( buf, "r" );
  if ( fp  == NULL ) {
    // send back an error message if file was not found
    pTemp.dPacket.type = -1;
    strcpy( pTemp.dPacket.data, "File Not Found\n" );
  } else {
    // otherwise, let the client know that file will be sent
    pTemp.dPacket.type = 3;
    strcpy( pTemp.dPacket.data, "File Will Be Sent\n" );
  }
  if ( sendto( sockfd, (char*) &pTemp, sizeof( pTemp ), 0,
	       &their_addr, sizeof( their_addr ) ) == -1 ) {
    perror("sendto");
    exit(1);
  }
  if ( fp == NULL ) {
    close( sockfd );
    exit(1);
  }

  printf( "Serving file: %s\n", buf );

  // build all the packets. sequence number and checksum will be filled later
  while ( !feof( fp ) ) {
    // construct a packet
    pTemp.dPacket.dataLength = fread( pTemp.dPacket.data, sizeof( pTemp.dPacket.data[0] ),
				  MAXDATALENGTH/sizeof( pTemp.dPacket.data[0] ), fp );
    if ( feof( fp ) ) {
      pTemp.dPacket.type = 2;
    } else {
      pTemp.dPacket.type = 0;
    }

    // add it to the packet buffer
    packetBuffer.push_back( pTemp );
  }
  fclose( fp );

  // send all the packets
  windowSize = atoi( argv[ 2 ] );
  base = 0;
  nextSeqNum = 0;
  while ( 1 ) {
    if ( nextSeqNum < base + windowSize && nextSeqNum < packetBuffer.size() ) {
      // set sequence number and checksum of packet
      p = &packetBuffer[ nextSeqNum ];
      p->dPacket.seqNum = nextSeqNum;
      p->checksum = checksum( (char*) &(p->dPacket), sizeof( p->dPacket ) );

      // send the packet
      sendPacketWithLossAndCorruption( probLoss, probCor, sockfd, p, their_addr );
      
      if ( base == nextSeqNum ) {
	timerRunning = true;	
	gettimeofday( &oldTime, 0 );
      }
      nextSeqNum++;
    }

    // check timer.
    gettimeofday( &newTime, 0 );
    if ( timerRunning && ( newTime.tv_usec - oldTime.tv_usec > 100000  ||
			   newTime.tv_sec > oldTime.tv_sec ) ) {
      printf( "TIMEOUT\n" );
      gettimeofday( &oldTime, 0 );
      timerRunning = true;
      // retransmit packets
      for ( int i = base; i < nextSeqNum; i++ ) {
	p = &packetBuffer[ i ];
	// send the packet
	sendPacketWithLossAndCorruption( probLoss, probCor, sockfd, p, their_addr );
      }
    }

    // read ack
    FD_ZERO( &readfds );
    FD_SET( sockfd, &readfds );
    select( sockfd + 1, &readfds, NULL, NULL, &tv );
    if ( FD_ISSET( sockfd, &readfds ) ) {
      if ( ( numbytes = recvfrom( sockfd, packetBuf, sizeof( struct packet ), 0,
				  &their_addr, &addr_len ) ) == -1 ) {
	perror( "recvfrom" );
	exit( 1 );
      }
      p = (struct packet*) packetBuf;
      if ( p->checksum == checksum( (char*) &(p->dPacket), sizeof( p->dPacket) ) ) {
	base = p->dPacket.ackNum + 1;
	if ( base == nextSeqNum )
	  timerRunning = false;
	else {
	  gettimeofday( &oldTime, 0 );
	  timerRunning = true;
	}
	
	printf( "Received Ack %i\n", p->dPacket.ackNum );
	// if received the last ack, file transfer is done
	if ( base >= packetBuffer.size() )
	  break;
      }
    }
  }
 
  close( sockfd );

  printf( "File Successfully Sent\n" );

  return 0;
}
