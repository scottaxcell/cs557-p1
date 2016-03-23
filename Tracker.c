#include <stdio.h>
#include <stdlib.h> // exit()
#include <string.h> // memset, bzero
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <stdbool.h> // bool
#include <sys/time.h> // bool

#include "Utils.h"
#include "Tracker.h"

// socket sources
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
// end

#define TRACKER_FILENAME "tracker.out"

// keep track of files and clients
static struct ClientAddr s_clientAddrs[MAX_CLIENTS];
static int s_numClientAddrs = 0;

static struct Group s_groups[MAX_FILES];
static int s_numGroups = 0;

static struct Group s_reqGroups[MAX_FILES];
static int s_numReqGroups = 0;


void dumpGroupAssignMsg(u_char *pktDontTouch)
{
  u_char *pkt = pktDontTouch;

}

void dumpRequestedGroups()
{
  printf("Tracker has been requested info about %d groups\n", s_numReqGroups);
  for (int i = 0; i < s_numReqGroups; i++) {
    struct Group *g = &s_reqGroups[i];
    printf("Group %s\n", g->filename);
  }
  printf("\n");
}



u_char * createGroupAssignPkt(int16_t *returnPktSize)
{
  // GROUP_ASSIGN, // tracker tells client about other clients
  // pktsize, msgtype, number files, filename, file size, num neighbors, neigh. id, neigh. ip, neigh port, ...

  // figure out packet size
  int16_t pktsize = (sizeof(int16_t) * 3); // pktsize, msgtype, num files
  pktsize += ((MAX_FILENAME + (2 * sizeof(int16_t))) * s_numReqGroups); // (filename, file size, num neighbors) * n
  for (int i = 0; i < s_numReqGroups; i++) {
    struct Group *reqGroup = &s_reqGroups[i];
    for (int j = 0; j < s_numGroups; j++) {
      struct Group *group = &s_groups[j];
      if (strcmp(reqGroup->filename, group->filename) == 0) {
        for (int k = 0; k < MAX_CLIENTS; k++) {
          if (group->sharingClients[k] == 1) {
            pktsize += (sizeof(int16_t) * 2) + MAX_IP; // neighbor id, port, ip
          }
        }
        break;
      }
    }
  }

  *returnPktSize = pktsize;
  int16_t n_pktsize = htons(pktsize);
  int16_t n_msgtype = htons(42);
  int16_t n_numfiles = htons(s_numReqGroups);

  u_char *pkt = malloc(pktsize);
  u_char *returnpkt = pkt;

  memset(pkt, 0, pktsize); // zero out

  memcpy(pkt, &n_pktsize, sizeof(int16_t));
  pkt += 2;
  memcpy(pkt, &n_msgtype, sizeof(int16_t));
  pkt += 2;
  memcpy(pkt, &n_numfiles, sizeof(int16_t));
  pkt += 2;

  for (int i = 0; i < s_numReqGroups; i++) {
    struct Group *reqGroup = &s_reqGroups[i];

    memcpy(pkt, &reqGroup->filename, MAX_FILENAME);
    pkt += MAX_FILENAME;

    int16_t filesize = 0;
    int16_t numNeighbors = 0;
    struct Group *group = NULL;
    for (int j = 0; j < s_numGroups; j++) {
      group = &s_groups[i];
      if (strcmp(reqGroup->filename, group->filename) == 0) {
        filesize = group->filesize;
        for (int k = 0; k < MAX_CLIENTS; k++) {
          if (group->sharingClients[k] == 1) {
            numNeighbors++;
          }
        }
        break;
      }
    }
    int16_t n_filesize = htons(filesize);
    memcpy(pkt, &n_filesize, sizeof(int16_t));
    pkt += 2;
    int16_t n_numNeighbors = htons(numNeighbors);
    memcpy(pkt, &n_numNeighbors, sizeof(int16_t));
    pkt += 2;
    
    if (group != NULL) {
      for (int j = 0; j < MAX_CLIENTS; j++) {
        if (group->sharingClients[j] == 1) {
          for (int k = 0; k < s_numClientAddrs; k++) {
            struct ClientAddr *ca = &s_clientAddrs[k];
            if (ca->id == j) {
              int16_t n_id = htons(j);
              memcpy(pkt, &n_id, sizeof(int16_t));
              pkt += sizeof(int16_t);

              memcpy(pkt, &ca->ip, MAX_IP);
              pkt += MAX_IP;

              int16_t n_port = htons(ca->port);
              memcpy(pkt, &n_port, sizeof(int16_t));
              pkt += sizeof(int16_t);
              break;
            }
          }
        }
      }
    }
  }

  return returnpkt;
}

void handleGroupUpdate(u_char *pktDontTouch, struct sockaddr_in cliaddr)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);

  memset(&s_reqGroups, 0, sizeof(s_reqGroups));

  // MSG FORMAT = pktsize, msgtype, client id, number files, filename, file size, type, ...
  u_char *pkt = pktDontTouch;

  int16_t n_pktsize;
  memcpy(&n_pktsize, pkt, sizeof(int16_t));
  int16_t pktsize = ntohs(n_pktsize);
  pkt += 2; 

  int16_t n_msgtype;
  memcpy(&n_msgtype, pkt, sizeof(int16_t));
  int16_t msgtype = ntohs(n_msgtype);
  pkt += 2; 

  int16_t n_id;
  memcpy(&n_id, pkt, sizeof(int16_t));
  int16_t id = ntohs(n_id);
  pkt += 2; 

  int16_t n_numfiles;
  memcpy(&n_numfiles, pkt, sizeof(int16_t));
  int16_t numfiles = ntohs(n_numfiles);
  pkt += 2; 

  printf("Client %d message: pktsize %d, msgtype %d, numfiles %d\n", id, pktsize, msgtype, numfiles);

  //
  // update client address database
  //
  // get client ip and port
  struct ClientAddr clientaddr;
  clientaddr.id = id;
  clientaddr.port = ntohs(cliaddr.sin_port);
  char *ip = inet_ntoa(cliaddr.sin_addr);
  memcpy(&clientaddr.ip, ip, MAX_IP);

  bool haveClientAddr = false;
  for (int i = 0; i < s_numClientAddrs; i++) {
    struct ClientAddr *ca = &s_clientAddrs[i];
    if (ca->id == id) {
      haveClientAddr = true;
      break;
    }
  }
  if (!haveClientAddr) {
    s_clientAddrs[s_numClientAddrs++] = clientaddr;
  }
    

  char logstr[(numfiles*MAX_FILENAME) + 10];
  memset(&logstr, '\0', sizeof(logstr));

  struct Group reqGroup;

  for (int i = 0; i < numfiles; i++) {
    char filename[MAX_FILENAME];
    memcpy(filename, pkt, MAX_FILENAME);
    pkt += MAX_FILENAME;

    // save requested filenames so we know what to send back
    memcpy(&reqGroup.filename, filename, MAX_FILENAME);
    s_reqGroups[s_numReqGroups++] = reqGroup;

    int16_t n_filesize;
    memcpy(&n_filesize, pkt, sizeof(int16_t));
    int16_t filesize = ntohs(n_filesize);
    pkt += 2;

    int16_t n_type;
    memcpy(&n_type, pkt, sizeof(int16_t));
    int16_t type = ntohs(n_type);
    pkt += 2;
    printf("filename %s, filesize %d, type %d\n", filename, filesize, type);

    // for logging save off filename
    strcat(logstr, filename);
    strcat(logstr, " ");
    
    //
    // Update group database
    //
    bool knowGroupAlready = false;
    for (int i = 0; i < s_numGroups; i++) {
      struct Group *group = &s_groups[i];
      if (strcmp(group->filename, filename) == 0) {
        // already tracking this group so update sharing and downloading clients
        if (type == 0) {
          // download only
          group->downClients[id] = 1;
        } else if (type == 1) {
          // download and share
          group->downClients[id] = 1;
          group->sharingClients[id] = 1;
        } else if (type == 2) {
          // share only
          group->sharingClients[id] = 1;
        }
        if (group->filesize == 0 && filesize != 0) {
          group->filesize = filesize;
        }
        knowGroupAlready = true;
        break;
      }
    }
    if (!knowGroupAlready) {
      // start tracking this group!
      struct Group *group = &s_groups[s_numGroups++];
      strncpy(group->filename, filename, sizeof(filename));
      group->filesize = filesize;

      // initialize client arrays
      for (int i = 0; i < MAX_CLIENTS; i++) {
        group->downClients[i] = 0;
        group->sharingClients[i] = 0;
      }
      // update according to this clients group update
      if (type == 0) {
        // download only
        group->downClients[id] = 1;
      } else if (type == 1) {
        // download and share
        group->downClients[id] = 1;
        group->sharingClients[id] = 1;
      } else if (type == 2) {
        // share only
        group->sharingClients[id] = 1;
      }
    }
  }
  printf("\n");

  //
  // Log received packet
  //
  FILE *fp;
  if ((fp = fopen(TRACKER_FILENAME, "a")) == NULL) {
    perror("ERROR opening tracker log file for append");
    exit(1);
  }
  fprintf(fp, "%d\t%lu.%d\t%s\n", id, tv.tv_sec, tv.tv_usec, logstr);
  fclose(fp);
}


void dumpTrackerClientAddrs()
{
  printf("Tracker has addresses for %d clients:\n", s_numClientAddrs);
  for (int i = 0; i < s_numClientAddrs; i++) {
    struct ClientAddr *ca = &s_clientAddrs[i];
    printf("ClientAddr %d: %s:%d\n", ca->id, ca->ip, ca->port);
  }
  printf("\n");
}

void dumpTrackerGroups()
{
  printf("Tracker knows about %d groups\n", s_numGroups);
  for (int i = 0; i < s_numGroups; i++) {
    struct Group *g = &s_groups[i];
    printf("Group %s:\n", g->filename);
    printf("Sharing Clients: ");
    for (int j = 0; j < MAX_CLIENTS; j++) {
      printf("%d:%d ", j, g->sharingClients[j]);
    }
    printf("\nDownloading Clients: ");
    for (int j = 0; j < MAX_CLIENTS; j++) {
      printf("%d:%d ", j, g->downClients[j]);
    }
    printf("\n");
  }
  printf("\n");
}

// Tracker worker
//
void trackerDoWork(int udpsock, int32_t trackerport)
{
  // zero out our databases
  memset(s_clientAddrs, 0, sizeof(s_clientAddrs));
  memset(s_groups, 0, sizeof(s_groups));

  // write out log information
  FILE *fp;
  fp = fopen(TRACKER_FILENAME, "w");
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

    //
    // Deserialize message from client
    //
    handleGroupUpdate(buffer, addr);
    free(buffer);
    /*DEBUG*/dumpTrackerClientAddrs();
    /*DEBUG*/dumpTrackerGroups();
    /*DEBUG*/dumpRequestedGroups();

    //
    // Send client request group information
    //
    int16_t sendPktSize = 0;
    u_char *sendPkt = createGroupAssignPkt(&sendPktSize);
    printf("Tracker sending pkt of size %d to client\n", sendPktSize);

    int bytesSent = -1;
    int totalBytesSent = 0;
    while (bytesSent < totalBytesSent) {
      if ((bytesSent = sendto(udpsock, sendPkt, sendPktSize, 0, (struct sockaddr*)&addr, fromlen)) == -1) {
        perror("ERROR (clientDoWork) sendto");
        exit(1);
      }
      totalBytesSent += bytesSent;
    }
    printf("Tracker sent %d bytes to client\n", totalBytesSent);

    //
    // Log sent packet
    //
    struct timeval tv;
    gettimeofday(&tv, NULL);

    if ((fp = fopen(TRACKER_FILENAME, "a")) == NULL) {
      perror("ERROR opening tracker log file");
      exit(1);
    }
    fprintf(fp, "---- ");
    for (int i = 0; i < sendPktSize; i++) {
      fprintf(fp, "%02x ", *(sendPkt+i));
    }
    fprintf(fp, "\n");
    fclose(fp);
    
    free(sendPkt);
  }

  // TODO terminate if no messages from clients for 30 seconds
  
}

