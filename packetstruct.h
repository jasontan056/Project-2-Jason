#define MAXDATALENGTH 512

// contains data and needed information
struct dataPacket {
  int seqNum;
  int ackNum;
  // type: -1 if file not on server, 0 for data, 1 for ack, 2 for EOF, 3 if file on server
  int type;
  int dataLength;
  char data[ MAXDATALENGTH ];  
};

// contains dataPacket and checksum of the entire dataPacket
struct packet {
  word16 checksum;
  struct dataPacket dPacket;
};


