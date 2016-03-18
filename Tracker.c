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

  // keep track of files and clients
  struct ClientAddr clientAddrs[MAX_CLIENTS];
  int numClientAddr = 0;

  struct FileTracker files[MAX_FILES];
  int numFiles = 0;

  while (true) {
    // Format of the packets the Tracker will be receiving from clients
    // pktsize(16bit)
    // msgtype (16bit)
    // client_id(16bit)
    // numfiles(16bit)
    // n * ( filename(32bytes) type(16bit) )
    u_char buffer[256];
    int bytesRecv = recvfrom(udpsock, buffer, sizeof(buffer), 0, (struct sockaddr*)&addr, &fromlen);
    if (bytesRecv == -1) {
      perror("ERROR initial manager recv failed");
      exit(1);
    }
    int16_t n_pktsize = 0;
    memcpy(&n_pktsize, &buffer, sizeof(int16_t));
    int16_t pktsize = ntohs(n_pktsize);
    u_char *realBuffer = malloc(pktsize);
    memcpy(realBuffer, &buffer, bytesRecv); // copy first portion of packet

    // get the rest of the packet
    int totalBytesRecv = bytesRecv;
    while (totalBytesRecv < pktsize) {
      bytesRecv = recvfrom(udpsock, (realBuffer + bytesRecv), sizeof(*realBuffer), 0, (struct sockaddr*)&addr, &fromlen); // TODO does sizeof(*pointer) work?
      if (bytesRecv == -1) {
        perror("ERROR additional manager recvfrom failed");
        exit(1);
      }
      totalBytesRecv += bytesRecv;
    }


    // deserialize message from client
    int16_t n_msgtype;
    int16_t n_cid;
    int16_t n_numfiles;

    memcpy(&n_msgtype, realBuffer+2, sizeof(int16_t));
    int16_t msgtype = ntohs(n_msgtype);

    memcpy(&n_cid, realBuffer+4, sizeof(int16_t));
    int16_t cid = ntohs(n_cid);

    memcpy(&n_numfiles, realBuffer+6, sizeof(int16_t));
    int16_t numfiles = ntohs(n_numfiles);

    //get client ip and port
    struct ClientAddr clientaddr;
    clientaddr.id = cid;
    clientaddr.port = ntohs(addr.sin_port);
    char *ip = inet_ntoa(addr.sin_addr);
    memcpy(&clientaddr.ip, ip, MAX_IP);

    //
    // update tracker client addresses
    //
    bool haveClientAddr = false;
    for (int i = 0; i < numClientAddr; i++) {
      struct ClientAddr *ca = &clientAddrs[i];
      if (ca->id == cid) {
        haveClientAddr = true;
        break;
      }
    }
    if (!haveClientAddr) {
      clientAddrs[numClientAddr] = clientaddr;
      numClientAddr++;
    }
    
    /*DEBUG*/printf("Tracker received %d bytes from client %d at %s on port %d\n", totalBytesRecv, clientaddr.id, clientaddr.ip, clientaddr.port);
    /*DEBUG*/printf("  msgtype = %d, numfiles = %d\n", msgtype, numfiles);

    realBuffer += 8; // set pointer to first filename
    for (int16_t i = 0; i < numfiles; i++) {
      realBuffer += (i*MAX_FILENAME)+(i*2);
      char filename[MAX_FILENAME];
      memcpy(&filename, realBuffer, MAX_FILENAME);

      int16_t n_type;
      memcpy(&n_type, realBuffer+MAX_FILENAME, 2);
        
      int16_t type = ntohs(n_type);
      printf("  file = %s type = %d\n", filename, type);

      //
      // Update FileTracker array
      //
      bool trackingFile = false;
      for (int i = 0; i < numFiles; i++) {
        struct FileTracker *ft = &files[i];
        if (strcmp(ft->filename, filename)) {
          // already tracking this file so update sharing and downloading clients
          if (type == 0) {
            // download only
            ft->downClients[cid] = 1;
          } else if (type == 1) {
            // download and share
            ft->downClients[cid] = 1;
            ft->sharingClients[cid] = 1;
          } else if (type == 2) {
            // share only
            ft->sharingClients[cid] = 1;
          }
          trackingFile = true;
          break;
        }
      }
      if (!trackingFile) {
        // start tracking it!
        struct FileTracker *ft = &files[numFiles];
        numFiles++;
        strncpy(ft->filename, filename, sizeof(filename));
        // initialize client arrays
        for (int i = 0; i < MAX_CLIENTS; i++) {
          ft->downClients[i] = 0;
          ft->sharingClients[i] = 0;
        }
        // update according to this client
        if (type == 0) {
          // download only
          ft->downClients[cid] = 1;
        } else if (type == 1) {
          // download and share
          ft->downClients[cid] = 1;
          ft->sharingClients[cid] = 1;
        } else if (type == 2) {
          // share only
          ft->sharingClients[cid] = 1;
        }
      }

      //
      // Reply to client with group information for the specified file
      //
      //int16_t pktsize = ((sizeof(int16_t)*4) + ((MAX_FILENAME + sizeof(int16_t)) * numfiles));
      //u_char *pkt = malloc(pktsize);
      //printf("client %d: size of one file packet = %d\n", client->id, pktsize);
      //n_pktsize = htons(pktsize);
	    //memset(pkt, 0, pktsize);
      //memcpy(pkt, &n_pktsize, sizeof(int16_t));
      //memcpy(pkt+2, &n_msgtype, sizeof(int16_t));
      //memcpy(pkt+4, &n_cid, sizeof(int16_t));
      //memcpy(pkt+6, &n_numfiles, sizeof(int16_t));
      //memcpy(pkt+8, &t->file, MAX_FILENAME);
      //memcpy(pkt+8+MAX_FILENAME, &n_type, sizeof(int16_t));
      u_char *pkt = (u_char *)"TRACK_TO_CLNT_UPDATE";
      int16_t pktsize = 20;

      socklen_t slen;
      slen = sizeof(addr);
      int bytesSent = -1;
      int totalBytesSent = 0;
      while (bytesSent < totalBytesSent) {
        if ((bytesSent = sendto(udpsock, pkt, pktsize, 0, (struct sockaddr*)&addr, slen)) == -1) {
          perror("ERROR (clientDoWork) sendto");
          exit(1);
        }
        totalBytesSent += bytesSent;
      }
      printf("tracker sent %d bytes to client\n", totalBytesSent);
      //free(pkt);
    }
  }

  // TODO need to maintain a table of filename to all clients interested in sharing the file.
  // interest means wants to download or has the file and wants to share
  
  // TODO listen on UDP port for service request - do as requested repeat

  // TODO terminate if no messages from clients for 30 seconds
  
}

