#include <stdio.h>
#include <stdlib.h> // exit()
#include <string.h> // memset, bzero
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <stdbool.h> // bool

#include "Tracker.h"

// socket sources
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
// end

//
// Tracker worker
//
void trackerDoWork(int udpsock, int32_t trackerport)
{
  // write out log information
  FILE *fp;
  const char *filename = "tracker.out";
  fp = fopen(filename, "w");
  if (fp == NULL) {
    perror("ERROR opening client log file");
    exit(1);
  }
  fprintf(fp, "type Tracker\n");
  fprintf(fp, "pid %d\n", getpid());
  fprintf(fp, "tPort %d\n", trackerport);

  socklen_t fromlen;
  struct sockaddr_in addr;
  fromlen = sizeof(addr);
  while (true) {
    // Format of the packets the Tracker will be receiving from clients
    // pktsize(16bit)
    // msgtype (16bit)
    // client_id(16bit)
    // numfiles(16bit)
    // n * filename(32bytes)
    // type(16bit)
    u_char buffer[256];
    int bytesRecv = recvfrom(udpsock, buffer, sizeof(buffer), 0, (struct sockaddr*)&addr, &fromlen);
    if (bytesRecv == -1) {
      perror("ERROR initial manager recv failed");
      exit(1);
    }
    printf("DEBUG tracker received a UDP message..\n");
    int16_t n_pktsize = 0;
    memcpy(&n_pktsize, &buffer, sizeof(int16_t));
    int16_t pktsize = ntohs(n_pktsize);
    u_char *realBuffer = malloc(pktsize);
    memcpy(realBuffer, &buffer, bytesRecv); // copy first portion of packet

    // get the rest of the packet
    int totalBytesRecv = 0;
    while (totalBytesRecv < pktsize) {
      bytesRecv = recvfrom(udpsock, (realBuffer + bytesRecv), sizeof(*realBuffer), 0, (struct sockaddr*)&addr, &fromlen); // TODO does sizeof(*pointer) work?
      if (bytesRecv == -1) {
        perror("ERROR additional manager recvfrom failed");
        exit(1);
      }
      totalBytesRecv += bytesRecv;
    }

    //get client id to test if this is working
    int16_t n_cid;
    memcpy(&n_cid, (realBuffer + 4), sizeof(int16_t)); // copy first portion of packet
    int16_t cid = ntohs(n_cid);
    /*DEBUG*/printf("Tracker received %d bytes from client %d\n", totalBytesRecv, cid);

  }

  sleep(3);
  //struct ClientAddr clientAddrs[MAX_CLIENTS];
  //int numClientAddr = 0;

  //struct FileTracker files[MAX_FILES];
  //int numFiles = 0;

  // TODO need to maintain a table of filename to all clients interested in sharing the file.
  // interest means wants to download or has the file and wants to share
  
  // TODO listen on UDP port for service request - do as requested repeat

  // TODO terminate if no messages from clients for 30 seconds
  
}

