#include <stdio.h>
#include <stdlib.h> // exit()
#include <string.h> // memset, bzero
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <stdbool.h> // bool
#include <sys/time.h> // bool

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
  fclose(fp);

  socklen_t fromlen;
  struct sockaddr_in addr;
  fromlen = sizeof(addr);

  // keep track of files and clients
  struct ClientAddr clientAddrs[MAX_CLIENTS];
  int numClientAddrs = 0;

  struct Group groups[MAX_FILES];
  int numGroups = 0;

  while (true) {

    //
    // Receive client packet, update client and file databases, send requested
    // group info to client
    //

    // Format of the packets the Tracker will be receiving from clients
    // pktsize(16bit)
    // msgtype (16bit)
    // client_id(16bit)
    // numfiles(16bit)
    // n * ( filename(32bytes) type(16bit) )

    u_char initBuff[512]; // make this large to make life easy
    int bytesRecv = recvfrom(udpsock, initBuff, sizeof(initBuff), 0, (struct sockaddr*)&addr, &fromlen);
    if (bytesRecv == -1) {
      perror("ERROR initial manager recv failed");
      exit(1);
    }

    // TODO get timestamp

    int16_t n_pktsize = 0;
    memcpy(&n_pktsize, &initBuff, sizeof(int16_t));
    int16_t pktsize = ntohs(n_pktsize);
    printf("Tracker expects full packet to be size of %d\n", pktsize);
    u_char *buffer = malloc(pktsize);
    u_char *buffer0 = buffer; // save position 0 for free
    memcpy(buffer, &initBuff, bytesRecv); // copy first portion of packet into full size buffer

    // get the rest of the packet
    int totalBytesRecv = bytesRecv;
    while (totalBytesRecv < pktsize) {
      bytesRecv = recvfrom(udpsock, (buffer + bytesRecv), sizeof(*buffer), 0, (struct sockaddr*)&addr, &fromlen);
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

    memcpy(&n_msgtype, buffer+2, sizeof(int16_t));
    int16_t msgtype = ntohs(n_msgtype);

    memcpy(&n_cid, buffer+4, sizeof(int16_t));
    int16_t cid = ntohs(n_cid);

    memcpy(&n_numfiles, buffer+6, sizeof(int16_t));
    int16_t numfiles = ntohs(n_numfiles);

    // get client ip and port
    struct ClientAddr clientaddr;
    clientaddr.id = cid;
    clientaddr.port = ntohs(addr.sin_port);
    char *ip = inet_ntoa(addr.sin_addr);
    memcpy(&clientaddr.ip, ip, MAX_IP);

    //
    // update client address database
    //
    bool haveClientAddr = false;
    for (int i = 0; i < numClientAddrs; i++) {
      struct ClientAddr *ca = &clientAddrs[i];
      if (ca->id == cid) {
        haveClientAddr = true;
        break;
      }
    }
    if (!haveClientAddr) {
      clientAddrs[numClientAddrs++] = clientaddr;
    }
    
    /*DEBUG*/printf("Tracker received %d bytes from client %d at %s on port %d\n", totalBytesRecv, clientaddr.id, clientaddr.ip, clientaddr.port);
    /*DEBUG*/printf("  msgtype = %d, numfiles = %d\n", msgtype, numfiles);

    //
    // Pull out group request information
    //
    struct Group reqGroups[numfiles];

    char logstr[(numfiles*MAX_FILENAME) + 10];
    memset(&logstr, '\0', sizeof(logstr));

    buffer = buffer + 8; // set pointer to first filename
    for (int16_t i = 0; i < numfiles; i++) {
      buffer += (i*MAX_FILENAME)+(i*2);

      struct Group reqGroup;
      memcpy(&reqGroup.filename, buffer, MAX_FILENAME);
      memcpy(&reqGroups[i], &reqGroup, sizeof(reqGroup));

      // for logging save off filename
      //strcat(logstr, filename);
      //strcat(logstr, " ");

      int16_t n_type;
      memcpy(&n_type, buffer+MAX_FILENAME, 2);
      int16_t type = ntohs(n_type);
      printf("  file = %s type = %d\n", reqGroup.filename, type);

      //
      // Update groups database
      //
      bool knowGroupAlready = false;
      for (int i = 0; i < numGroups; i++) {
        struct Group *group = &groups[i];
        if (strcmp(group->filename, reqGroup.filename)) {
          // already tracking this group so update sharing and downloading clients
          if (type == 0) {
            // download only
            group->downClients[cid] = 1;
          } else if (type == 1) {
            // download and share
            group->downClients[cid] = 1;
            group->sharingClients[cid] = 1;
          } else if (type == 2) {
            // share only
            group->sharingClients[cid] = 1;
          }
          knowGroupAlready = true;
          break;
        }
      }
      if (!knowGroupAlready) {
        // start tracking this group!
        struct Group *group = &groups[numGroups++];
        strncpy(group->filename, reqGroup.filename, sizeof(reqGroup.filename));
        // initialize client arrays
        for (int i = 0; i < MAX_CLIENTS; i++) {
          group->downClients[i] = 0;
          group->sharingClients[i] = 0;
        }
        // update according to this clients group update
        if (type == 0) {
          // download only
          group->downClients[cid] = 1;
        } else if (type == 1) {
          // download and share
          group->downClients[cid] = 1;
          group->sharingClients[cid] = 1;
        } else if (type == 2) {
          // share only
          group->sharingClients[cid] = 1;
        }
      }
    }

    //
    // Update request group information with database
    //
    for (int i = 0; i < numfiles; i++) {
      struct Group *reqGroup = &reqGroups[i];
      for (int i = 0; i < MAX_CLIENTS; i++) {
        reqGroup->downClients[i] = 0;
        reqGroup->sharingClients[i] = 0;
      }

      for (int j = 0; j < numGroups; j++) {
        struct Group *group = &groups[j];
        if (strcmp(reqGroup->filename, group->filename)) {
          // populate the requested group with client information
          for (int k = 0; k < MAX_CLIENTS; k++) {
            if (group->sharingClients[k] == 1) {
              reqGroup->sharingClients[k] = 1;
            }
          }
          break;
        }
      }
    }

    //
    // Send client request group information
    //
    int16_t clientPktSize = (sizeof(int16_t) * 4); // pktsize, msgtype, numfiles, numneighbors
    clientPktSize += (MAX_FILENAME * numfiles); // filenames
    for (int i = 0; i < numfiles; i++) {
      struct Group *reqGroup = &reqGroups[i];
      for (int j = 0; j < MAX_CLIENTS; j++) {
        if (reqGroup->sharingClients[j] == 1) {
          clientPktSize += (sizeof(int16_t) * 2) + MAX_IP; // neighbor id, port, ip
        }
      }
    }
    u_char *clientPkt = malloc(clientPktSize);
    u_char *clientPkt0 = clientPkt;
    printf("Tracker sending pkt of size %d\n", clientPktSize);

    int16_t n_clientPktSize = htons(clientPktSize);
    int16_t n_clientMsgType = htons(42);
    int16_t n_clientNumFiles = htons(numfiles);
	  memset(clientPkt, 0, clientPktSize); // zero out
    memcpy(clientPkt, &n_clientPktSize, sizeof(int16_t));
    memcpy(clientPkt+2, &n_clientMsgType, sizeof(int16_t));
    memcpy(clientPkt+4, &n_clientNumFiles, sizeof(int16_t));

    clientPkt += 6;
    for (int i = 0; i < numfiles; i++) {
      struct Group *reqGroup = &reqGroups[i];

      memcpy(clientPkt, &reqGroup->filename, MAX_FILENAME);
      clientPkt += MAX_FILENAME;
      int16_t numNeighbors = 0;
      for (int j = 0; j < MAX_CLIENTS; j++) {
        if (reqGroup->sharingClients[j] == 1) {
          numNeighbors++;
        }
      }
      int16_t n_numNeighbors = htons(numNeighbors);
      memcpy(clientPkt, &n_numNeighbors, sizeof(int16_t));
      clientPkt += sizeof(int16_t);
      
      for (int j = 0; j < MAX_CLIENTS; j++) {
        if (reqGroup->sharingClients[j] == 1) {
          for (int k = 0; k < numClientAddrs; k++) {
            struct ClientAddr *ca = &clientAddrs[i];
            if (ca->id == j) {
              int16_t n_id = htons(j);
              memcpy(clientPkt, &n_id, sizeof(n_id));
              clientPkt += sizeof(int16_t);

              memcpy(clientPkt, &ca->ip, MAX_IP);
              clientPkt += MAX_IP;

              int16_t n_port = htons(ca->port);
              memcpy(clientPkt, &n_port, sizeof(n_port));
              clientPkt += sizeof(int16_t);
            }
          }
        }
      }
    }

    //socklen_t slen;
    //slen = sizeof(addr);
    //int bytesSent = -1;
    //int totalBytesSent = 0;
    //while (bytesSent < totalBytesSent) {
    //  if ((bytesSent = sendto(udpsock, clientPkt0, clientPktSize, 0, (struct sockaddr*)&addr, slen)) == -1) {
    //    perror("ERROR (clientDoWork) sendto");
    //    exit(1);
    //  }
    //  totalBytesSent += bytesSent;
    //}
    //printf("Tracker sent %d bytes to client\n", totalBytesSent);
    // TODO write sent message to log file

    //
    // Log received packet
    //
    //struct timeval tv;
    //gettimeofday(&tv, NULL);

    //fp = fopen(filename, "a");
    //if (fp == NULL) {
    //  perror("ERROR opening client log file");
    //  exit(1);
    //}
    //fprintf(fp, "%d\t%lu.%d\t%s\n", clientaddr.id, tv.tv_sec, tv.tv_usec, logstr);
    //fprintf(fp, "---- ");
    //for (int i = 0; i < pktsize; i++) {
    //  fprintf(fp, "%02x ", *(buffer0+i));
    //}
    //fprintf(fp, "\n");
    //fclose(fp);

    free(buffer0);
    free(clientPkt0);
  }

  // TODO need to maintain a table of filename to all clients interested in sharing the file.
  // interest means wants to download or has the file and wants to share
  
  // TODO listen on UDP port for service request - do as requested repeat

  // TODO terminate if no messages from clients for 30 seconds
  
}

