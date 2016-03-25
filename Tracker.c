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
#include "timers-c.h"

// socket sources
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
// end

#define TRACKER_FILENAME "tracker.out"

// File Contents:
// All functions that the tracker process needs to complete it's job of
// tracking which clients have which files and supplying this information
// to the client processes.


// keep track of files and clients
static struct ClientAddr st_clientAddrs[MAX_CLIENTS];
static int st_numClientAddrs = 0;

static struct Group st_groups[MAX_FILES];
static int st_numGroups = 0;

static struct Group st_reqGroups[MAX_FILES];
static int st_numReqGroups = 0;

static bool st_terminate = false;

int checkIfShouldTerminate()
{
  if (st_terminate == true) {
    /*DEBUG*/printf("Tracker terminated after 30 seconds of no client group updates\n");
    exit(0);
  }
  st_terminate = true;

  return 0;
}


void dumpTrackerUpdateMsg(u_char *pktDontTouch)
{
  // GROUP_ASSIGN, // tracker tells client about other clients
  // pktsize, msgtype, number files, filename, file size, num neighbors, neigh. id, neigh. ip, neigh port, ...

  u_char *pkt = pktDontTouch;

  int16_t n_pktsize;
  memcpy(&n_pktsize, pkt, sizeof(int16_t));
  pkt += 2; // move past pktsize and msgtype
  int16_t pktsize = ntohs(n_pktsize);

  int16_t n_msgtype;
  memcpy(&n_msgtype, pkt, sizeof(int16_t));
  pkt += 2; // move past pktsize and msgtype
  int16_t msgtype = ntohs(n_msgtype);

  int16_t n_numfiles;
  memcpy(&n_numfiles, pkt, sizeof(int16_t));
  int16_t numfiles = ntohs(n_numfiles);
  pkt += 2;

  //printf("Tracker sending GROUP_ASSIGN: pktsize %d, msgtype %d, numfiles %d\n", pktsize, msgtype, numfiles);

  for (int16_t i = 0; i < numfiles; i++) {
    struct Group newGroup;
    for (int i = 0; i < MAX_CLIENTS; i++) {
      newGroup.downClients[i] = 0;
      newGroup.sharingClients[i] = 0;
    }
    memcpy(&newGroup.filename, pkt, MAX_FILENAME);
    pkt += MAX_FILENAME;

    uint16_t n_filesize;
    memcpy(&n_filesize, pkt, sizeof(n_filesize));
    pkt += sizeof(n_filesize);
    uint16_t filesize = ntohs(n_filesize);
    newGroup.filesize = filesize;


    uint16_t n_numneighbors;
    memcpy(&n_numneighbors, pkt, sizeof(int16_t));
    pkt += sizeof(int16_t);
    uint16_t numneighbors = ntohs(n_numneighbors);

    //printf("File %d: %s, filesize %u, numneighbors %d: ", i, newGroup.filename, newGroup.filesize, numneighbors);
    for (int i = 0; i < numneighbors; i++) {
      struct ClientAddr clientaddr;

      uint16_t n_id;
      memcpy(&n_id, pkt, sizeof(int16_t));
      pkt += sizeof(int16_t);
      uint16_t id = ntohs(n_id);
      clientaddr.id = id;
      newGroup.sharingClients[id] = 1;

      char ip[MAX_IP];
      memcpy(&ip, pkt, MAX_IP);
      pkt += MAX_IP;
      memcpy(&clientaddr.ip, ip, MAX_IP);

      uint16_t n_port;
      memcpy(&n_port, pkt, sizeof(n_port));
      pkt += sizeof(int16_t);
      uint16_t port = ntohs(n_port);
      clientaddr.port = port;
      //printf("%d %s %d\n", id, ip, port);
    }
    //printf("\n");
  }
}

//void dumpGroupAssignMsg(u_char *pktDontTouch)
//{
//  u_char *pkt = pktDontTouch;
//
//}

void dumpRequestedGroups()
{
  printf("Tracker has been requested info about %d groups\n", st_numReqGroups);
  for (int i = 0; i < st_numReqGroups; i++) {
    struct Group *g = &st_reqGroups[i];
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
  //pktsize += ((MAX_FILENAME + (2 * sizeof(int16_t))) * st_numReqGroups); // (filename, file size, num neighbors) * n
  for (int i = 0; i < st_numReqGroups; i++) {
    pktsize += (MAX_FILENAME + (2 * sizeof(int16_t))); // filename, file size, num neighbors
    struct Group *reqGroup = &st_reqGroups[i];
    for (int j = 0; j < st_numGroups; j++) {
      struct Group *group = &st_groups[j];
      if (strncmp(reqGroup->filename, group->filename, MAX_FILENAME) == 0) {
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
  int16_t n_numfiles = htons(st_numReqGroups);

  u_char *pkt = malloc(pktsize);
  u_char *returnpkt = pkt;

  memset(pkt, 0, pktsize); // zero out

  memcpy(pkt, &n_pktsize, sizeof(int16_t));
  pkt += 2;
  memcpy(pkt, &n_msgtype, sizeof(int16_t));
  pkt += 2;
  memcpy(pkt, &n_numfiles, sizeof(int16_t));
  pkt += 2;

  for (int i = 0; i < st_numReqGroups; i++) {
    struct Group *reqGroup = &st_reqGroups[i];

    memcpy(pkt, &reqGroup->filename, MAX_FILENAME);
    pkt += MAX_FILENAME;

    uint16_t filesize = 0;
    int16_t numNeighbors = 0;
    struct Group *group = NULL;
    for (int j = 0; j < st_numGroups; j++) {
      group = &st_groups[i];
      if (strncmp(reqGroup->filename, group->filename, MAX_FILENAME) == 0) {
        filesize = group->filesize;
        for (int k = 0; k < MAX_CLIENTS; k++) {
          if (group->sharingClients[k] == 1) {
            numNeighbors++;
          }
        }
        break;
      }
    }
    uint16_t n_filesize = htons(filesize);
    memcpy(pkt, &n_filesize, sizeof(n_filesize));
    pkt += sizeof(n_filesize);
    int16_t n_numNeighbors = htons(numNeighbors);
    memcpy(pkt, &n_numNeighbors, sizeof(int16_t));
    pkt += 2;
    
    if (group != NULL) {
      for (int j = 0; j < MAX_CLIENTS; j++) {
        if (group->sharingClients[j] == 1) {
          for (int k = 0; k < st_numClientAddrs; k++) {
            struct ClientAddr *ca = &st_clientAddrs[k];
            if (ca->id == j) {
              int16_t n_id = htons(j);
              memcpy(pkt, &n_id, sizeof(int16_t));
              pkt += sizeof(int16_t);

              memcpy(pkt, &ca->ip, MAX_IP);
              pkt += MAX_IP;

              uint16_t n_port = htons(ca->port);
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

void handleGroupUpdateRequest(u_char *pktDontTouch, struct sockaddr_in cliaddr)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);

  memset(&st_reqGroups, 0, sizeof(st_reqGroups));
  st_numReqGroups = 0;

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

  //printf("Client %d message: pktsize %d, msgtype %d, numfiles %d\n", id, pktsize, msgtype, numfiles);

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
  for (int i = 0; i < st_numClientAddrs; i++) {
    struct ClientAddr *ca = &st_clientAddrs[i];
    if (ca->id == id) {
      haveClientAddr = true;
      break;
    }
  }
  if (haveClientAddr == false) {
    st_clientAddrs[st_numClientAddrs++] = clientaddr;
  }
    

  char logstr[(numfiles*MAX_FILENAME) + 10];
  memset(&logstr, '\0', sizeof(logstr));

  struct Group reqGroup;
  memset(&reqGroup, 0, sizeof(reqGroup));

  for (int i = 0; i < numfiles; i++) {
    char filename[MAX_FILENAME];
    memcpy(filename, pkt, MAX_FILENAME);
    pkt += MAX_FILENAME;

    // save requested filenames so we know what to send back
    memcpy(&reqGroup.filename, &filename, MAX_FILENAME);
    st_reqGroups[st_numReqGroups++] = reqGroup;

    uint16_t n_filesize;
    memcpy(&n_filesize, pkt, sizeof(n_filesize));
    pkt += sizeof(n_filesize);
    uint16_t filesize = ntohs(n_filesize);

    int16_t n_type;
    memcpy(&n_type, pkt, sizeof(int16_t));
    int16_t type = ntohs(n_type);
    pkt += 2;
    //printf("filename %s, filesize %d, type %d\n", filename, filesize, type);

    // for logging save off filename
    strcat(logstr, filename);
    strcat(logstr, " ");
    
    //
    // Update group database
    //
    bool knowGroupAlready = false;
    for (int i = 0; i < st_numGroups; i++) {
      struct Group *group = &st_groups[i];
      if (strncmp(group->filename, filename, MAX_FILENAME) == 0) {
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
    if (knowGroupAlready == false) {
      // start tracking this group!
      struct Group *group = &st_groups[st_numGroups++];
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
  //printf("\n\n");

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
  printf("Tracker has addresses for %d clients:\n", st_numClientAddrs);
  for (int i = 0; i < st_numClientAddrs; i++) {
    struct ClientAddr *ca = &st_clientAddrs[i];
    printf("ClientAddr %d: %s:%d\n", ca->id, ca->ip, ca->port);
  }
  printf("\n");
}

void dumpTrackerGroups()
{
  printf("Tracker knows about %d groups\n", st_numGroups);
  for (int i = 0; i < st_numGroups; i++) {
    struct Group *g = &st_groups[i];
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
  memset(st_clientAddrs, 0, sizeof(st_clientAddrs));
  st_numClientAddrs = 0;
  memset(st_groups, 0, sizeof(st_groups));
  st_numGroups = 0;

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

  struct sockaddr_in clientaddr;
  socklen_t fromlen = sizeof(clientaddr);

	struct timeval tmv;
	int status;
  fd_set active_fd_set, read_fd_set;
  FD_ZERO(&active_fd_set);
  FD_SET(udpsock, &active_fd_set);

  int (*funcp)();
  funcp = checkIfShouldTerminate;
  Timers_AddTimer((30*1000), funcp, (int*)1); // 30 second timer


  // I've reused the code from test-app-c.c exampled provided by the timers
  // library in the following while loop.
  while (1) {
		Timers_NextTimerTime(&tmv);
		if (tmv.tv_sec == 0 && tmv.tv_usec == 0) {
		  /* The timer at the head on the queue has expired  */
      Timers_ExecuteNextTimer();
			continue;
		}

		tmv.tv_sec = 0; tmv.tv_usec = 200; // TODO figure out what to set timer to here
    read_fd_set = active_fd_set;
		status = select(FD_SETSIZE, &read_fd_set, NULL, NULL, &tmv);
		
		
		if (status < 0) {
		  /* This should not happen */
			fprintf(stderr, "Select returned %d\n", status);
		} else {
			//if (status == 0) { // TODO why can't I have this on?
			//	/* Timer expired, Hence process it  */
		  //  Timers_ExecuteNextTimer();
			//	/* Execute all timers that have expired.*/
			//	Timers_NextTimerTime(&tmv);
			//	while(tmv.tv_sec == 0 && tmv.tv_usec == 0) {
			//	  /* Timer at the head of the queue has expired  */
		  //    Timers_ExecuteNextTimer();
			//		Timers_NextTimerTime(&tmv);
			//	}
			//}
			if (status > 0) {
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
        if (FD_ISSET(udpsock, &read_fd_set)) {
          st_terminate = false; // reset termination timer since we heard from a client

          u_char initBuff[512]; // make this large to make life easy
          int bytesRecv = recvfrom(udpsock, initBuff, sizeof(initBuff), 0, (struct sockaddr*)&clientaddr, &fromlen);
          if (bytesRecv == -1) {
            perror("ERROR initial manager recv failed");
            exit(1);
          }

          int16_t n_pktsize = 0;
          memcpy(&n_pktsize, &initBuff, sizeof(int16_t));
          int16_t pktsize = ntohs(n_pktsize);
          //printf("Tracker expects full packet to be size of %d\n", pktsize);
          u_char *buffer = malloc(pktsize);
          memcpy(buffer, &initBuff, bytesRecv); // copy first portion of packet into full size buffer

          // get the rest of the packet
          int totalBytesRecv = bytesRecv;
          while (totalBytesRecv < pktsize) {
            bytesRecv = recvfrom(udpsock, (buffer + bytesRecv), sizeof(*buffer), 0, (struct sockaddr*)&clientaddr, &fromlen);
            if (bytesRecv == -1) {
              perror("ERROR additional manager recvfrom failed");
              exit(1);
            }
            totalBytesRecv += bytesRecv;
          }

          //
          // Deserialize message from client
          //
          handleGroupUpdateRequest(buffer, clientaddr);
          free(buffer);
          ///*DEBUG*/dumpTrackerClientAddrs();
          ///*DEBUG*/dumpTrackerGroups();
          ///*DEBUG*/dumpRequestedGroups();

          //
          // Send client request group information
          //
          int16_t sendPktSize = 0;
          u_char *sendPkt = createGroupAssignPkt(&sendPktSize);
          /*DEBUG*/dumpTrackerUpdateMsg(sendPkt);

          int bytesSent = 0;
          int totalBytesSent = 0;
          while (totalBytesSent < sendPktSize) {
            if ((bytesSent = sendto(udpsock, sendPkt, sendPktSize, 0, (struct sockaddr*)&clientaddr, fromlen)) == -1) {
              perror("ERROR (clientDoWork) sendto");
              exit(1);
            }
            totalBytesSent += bytesSent;
          }

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
      }
		}
  }
}

