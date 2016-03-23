#include <stdio.h>
#include <stdlib.h> // exit()
#include <string.h> // memset, bzero
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <stdbool.h> // bool

#include "Utils.h"
#include "Client.h"
#include "timers-c.h"

// socket sources
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
// end
/*
GROUP_SHOW_INTEREST, // client tells tracker about itself
  pktsize, msgtype, client id, number files, filename, file size, type, ...

GROUP_ASSIGN, // tracker tells client about other clients
  pktsize, msgtype, number files, filename, file size, num neighbors, neigh. id, neigh. ip, neigh port, ...

CLNT_INFO_REQ, // client asks other client for file info
  pktsize, msgtype, filename

CLNT_INFO_REP, // client tells other client about file segments it has
  pktsize, msgtype, filename, num segments, segment number, ...

CLNT_SEG_REQ, // client asks other client for a file segment
  pktsize, msgtype, filename, segment number

CLNT_SEG_REP // client sends other client file segment
  pktsize, msgtype, filename, raw file data
*/

// Each forked process gets a copy of these static variables so there is no collision
static struct CommInfo s_commInfo;

static bool s_downloadState[MAX_FILES];

static struct Download s_downloads[MAX_FILES];
static long s_numDownloads = 0;

static struct FileInfo s_ownedFiles[MAX_FILES];
static int s_numOwnedFiles = 0;

static struct ClientAddr sc_clientAddrs[MAX_CLIENTS];
static int sc_numClientAddrs = 0;

static struct Group sc_groups[MAX_FILES];
static int sc_numGroups = 0;

// TODO dumpClientDownloads()
// TODO dumpClientOwnedFiles()

void dumpClientCommInfo()
{
  printf("Client %d s_commInfo: ", s_commInfo.clientid);
  printf("port %d, udpsock %d, pktdelay %d, pktprob %d, trackerport %d\n", s_commInfo.myport, s_commInfo.udpsock, s_commInfo.pktdelay, s_commInfo.pktprob, s_commInfo.trackerport);
}

void dumpClientClientAddrs()
{
  printf("Client %d has addresses for %d clients:\n", s_commInfo.clientid, sc_numClientAddrs);
  for (int i = 0; i < sc_numClientAddrs; i++) {
    struct ClientAddr *ca = &sc_clientAddrs[i];
    printf("ClientAddr %d: %s:%d\n", ca->id, ca->ip, ca->port);
  }
}

void dumpClientGroups(struct Group groups[], int numGroups)
{
  printf("Client %d knows about %d groups\n", s_commInfo.clientid, numGroups);
  for (int i = 0; i < numGroups; i++) {
    struct Group *g = &groups[i];
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
}

void dumpDeserializedMsg(u_char *pktDontTouch)
{
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
  for (int i = 0; i < numfiles; i++) {
    char filename[MAX_FILENAME];
    memcpy(filename, pkt, MAX_FILENAME);
    pkt += MAX_FILENAME;

    int16_t n_filesize;
    memcpy(&n_filesize, pkt, sizeof(int16_t));
    int16_t filesize = ntohs(n_filesize);
    pkt += 2;

    int16_t n_type;
    memcpy(&n_type, pkt, sizeof(int16_t));
    int16_t type = ntohs(n_type);
    pkt += 2;
    printf("filename %s, filesize %d, type %d\n", filename, filesize, type);
  }

}

int enableDownload(int i)
{
  /*DEBUG*/printf("Fired enableDownload(int i) timer for download i = %d\n", i);
  s_downloadState[i] = true;
  return -1; // negative reurn will remove timer after completion
}

//
// Send tracker group interest
//
int sendInterestToTracker()
{
  //
  // GROUP_SHOW_INTEREST (client -> tracker)
  //
  // MSG FORMAT = pktsize, msgtype, client id, number files, filename, file size, type, ...

  if (s_numDownloads == 0)
    return -1;

  char logstr[(s_numDownloads*MAX_FILENAME) + 56];
  memset(&logstr, '\0', sizeof(logstr));

  // Serialize message
  int16_t pktsize = ((sizeof(int16_t)*4) + ((MAX_FILENAME + (2*sizeof(int16_t))) * s_numDownloads));
  int16_t n_pktsize = htons(pktsize);

  int16_t n_msgtype = htons(0); // GROUP_SHOW_INTEREST

  int16_t n_cid = htons(s_commInfo.clientid);

  // downloads holds files that I will just be sharing too
  int16_t n_numfiles = htons(s_numDownloads);

  u_char *pkt = malloc(pktsize);
  memset(pkt, 0, pktsize);
  u_char *pkt0 = pkt; // keep pointer to beginning so we can free later

  // fill up u_char for sending!
  memcpy(pkt, &n_pktsize, sizeof(int16_t));
  memcpy(pkt+2, &n_msgtype, sizeof(int16_t));
  memcpy(pkt+4, &n_cid, sizeof(int16_t));
  memcpy(pkt+6, &n_numfiles, sizeof(int16_t));
  pkt += 8;

  for (int i = 0; i < s_numDownloads; i++) {
    struct Download *d = &s_downloads[i];
    d->filesize = 0;

    bool haveFileAlready = false;

    // check if we know the file size
    for (int i = 0; i < s_numOwnedFiles; i++) {
      struct FileInfo *fi = &s_ownedFiles[i];
      if (strcmp(fi->name, d->filename) == 0) {
        haveFileAlready = true;
        d->filesize = fi->size;
        break;
      }
    }

    // figure out type
    int16_t n_type;
    if (!haveFileAlready && d->share == 0) {
      // request a group, but not willing to share
      n_type = htons(0);
    } else if (!haveFileAlready && d->share == 1) {
      // request a group, and willing to share
      n_type = htons(1);
    } else if (haveFileAlready && d->share == 1) {
      // show interest only, do not need a group, willing to share
      n_type = htons(2);
    }

    memcpy(pkt, &d->filename, MAX_FILENAME);
    pkt += MAX_FILENAME;

    int16_t n_filesize = htons(d->filesize);
    memcpy(pkt, &n_filesize, sizeof(int16_t));
    pkt += 2;

    memcpy(pkt, &n_type, sizeof(int16_t));
    pkt += 2;

    // for logging save off filename
    strcat(logstr, d->filename);
    strcat(logstr, " ");
  }

  /*DEBUG*/
  dumpDeserializedMsg(pkt0);

  struct sockaddr_in si_other;
  socklen_t slen;
  slen = sizeof(si_other);
  memset((char *) &si_other, 0, sizeof(si_other));
  si_other.sin_family = AF_INET;
  si_other.sin_port = htons(s_commInfo.trackerport); // set tracker port we want to send to

  int bytesSent = -1, totalBytesSent = 0;
  while (bytesSent < totalBytesSent) {
    if ((bytesSent = sendto(s_commInfo.udpsock, pkt0, pktsize, 0, (struct sockaddr*)&si_other, slen)) == -1) {
      perror("ERROR (sendInterestToTracker) sendto");
      exit(1);
    }
    totalBytesSent += bytesSent;
  }
  free(pkt0);
  
  //
  // Log the packet that was just sent to tracker
  //
  struct timeval tv;
  gettimeofday(&tv, NULL);

  FILE *fp;
  char filename[20];
  sprintf(filename, "%02d.out", s_commInfo.clientid);
  fp = fopen(filename, "a");
  if (fp == NULL) {
    perror("ERROR opening client log file for append");
    exit(1);
  }
  fprintf(fp, "%lu.%d\tTo\tT\tGROUP_SHOW_INTEREST\t%s\n", tv.tv_sec, tv.tv_usec, logstr);
  fclose(fp);

  return -1;
}



//
// Client worker
//
void clientDoWork(int clientid, int32_t managerport)
{
  // clear out databases in case another process had already populated these
  memset(s_downloadState, 0, sizeof(s_downloadState));
  memset(s_downloads, 0, sizeof(s_downloads));
  s_numDownloads = 0;
  memset(s_ownedFiles, 0, sizeof(s_ownedFiles));
  s_numOwnedFiles = 0;
  memset(&s_commInfo, 0, sizeof(s_commInfo));
  memset(sc_clientAddrs, 0, sizeof(sc_clientAddrs));
  sc_numClientAddrs = 0;
  memset(sc_groups, 0, sizeof(sc_groups));
  sc_numGroups = 0;

  //
  // Setup TCP socket for receiving configuration information from the manager
  //
	int sockfd;
	struct addrinfo hints, *servinfo;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

  char mgrport[10];
  sprintf(mgrport, "%d", managerport);
	if (getaddrinfo(NULL, mgrport, &hints, &servinfo) != 0) {
		perror("ERROR (clientDoWork) on tracker getaddrinfo");
		exit(1);
	}

	if ((sockfd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol)) < 0) {
		perror("ERROR (clientDoWork) on tracker tcp socket");
		exit(1);
	}

	if (connect(sockfd, servinfo->ai_addr, servinfo->ai_addrlen) < 0) {
		perror("ERROR (clientDoWork) on tracker tcp connect");
		exit(1);
	}


  // Get my configuration information from the manager
  int32_t trackerport = 0;
  int clientport = 0;
  struct Client *client;

  // request format: CID 0 NEED_CFG
  char msg[256];
  sprintf(msg, "CID %d NEED_CFG", clientid);
  int bytes_sent = -1, len = strlen(msg);
  while (bytes_sent != len) {
    bytes_sent = send(sockfd, msg, len, 0);
  }
  //printf("client sent %d byte msg\n", bytes_sent);

  // receive number of files
  u_char buffer[sizeof(struct Client)];
  u_char *p_buffer = (u_char *)&buffer;
  bzero(buffer, sizeof(buffer));
  int totalRecv = 0, recv_bytes = 0;
  while (totalRecv < sizeof(struct Client)) {
    recv_bytes = recv(sockfd, (p_buffer+totalRecv), sizeof(buffer), 0);
    totalRecv += recv_bytes;
    if (recv_bytes == 0) {
      printf("Client TCP socket closed.\n");
    } else if (recv_bytes == -1) {
      perror("ERROR client recv failed");
      exit(1);
    }
  }
  ///*DEBUG*/printf("DBG client TCP received %d bytes\n", recv_bytes);
  ///*DEBUG*/printf("totalRecv = %d\n", totalRecv);

  p_buffer = (u_char *)&buffer;
  client = (struct Client *)deserializeClient((struct Client *)p_buffer);
  s_commInfo.pktdelay = client->pktdelay;
  s_commInfo.pktprob = client->pktprob;
  s_commInfo.clientid = client->id;

  ///*DEBUG*/printf("Client %d:\n", client->id);
  ///*DEBUG*/printf("pktdelay %d, pktprob %d, numfiles %d, numtasks %d\n", client->pktdelay,
  ///*DEBUG*/  client->pktprob, client->numfiles, client->numtasks);
  ///*DEBUG*/for (int i = 0; i < client->numfiles; i++) {
  ///*DEBUG*/  printf("file: %s\n", client->files[i]);
  ///*DEBUG*/} 
  ///*DEBUG*/for (int i = 0; i < client->numtasks; i++) {
  ///*DEBUG*/  printf("task %d:\n", i);
  ///*DEBUG*/  struct Task *task = (struct Task *)&(client->tasks[i]);
  ///*DEBUG*/  printf("%s %d %d\n", task->file, task->starttime, task->share);
  ///*DEBUG*/} 

  // receive tracker port
  u_char portmsg[sizeof(int32_t)];
  memset(&portmsg, 0, sizeof(portmsg));

  recv_bytes = recv(sockfd, portmsg, sizeof(portmsg), 0);
  if (recv_bytes == 0) {
    printf("Client TCP socket closed.\n");
  } else if (recv_bytes == -1) {
    perror("ERROR client recv failed");
    exit(1);
  } else {
    ///*DEBUG*/printf("DBG client TCP received %d bytes\n", recv_bytes);
    int32_t n_trackerport = 0;
    memcpy(&n_trackerport, &portmsg, sizeof(int32_t));
    trackerport = ntohl(n_trackerport);
    s_commInfo.trackerport = trackerport;
    ///*DEBUG*/printf("client got tracker port %d from manager\n", trackerport);
  }

  /* Get the port number for communicating with spawned processes */
  struct sockaddr_in serv_addr;
  socklen_t serv_addr_size = sizeof(serv_addr);
  if (getsockname(sockfd, (struct sockaddr *) &serv_addr, &serv_addr_size) == -1) {
     perror("ERROR (clientDoWork) getsockname");
     exit(1);
  }
  clientport = ntohs(serv_addr.sin_port);
  s_commInfo.myport = clientport;

  //
  // Create UDP socket using the port number everyone know us by
  //
  struct sockaddr_in udp_serv_addr;
  memset((char *) &udp_serv_addr, 0, sizeof(udp_serv_addr));

  int udpsock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (udpsock < 0) {
    perror("ERROR (setupTrackerUDPComms) on tracker UDP socket");
    exit(1);
  }
  s_commInfo.udpsock = udpsock;


  /*DEBUG*/dumpClientCommInfo();

  // lose the pesky "Address already in use" error message
  int yes = 1;
  if (setsockopt(udpsock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) < 0) {
    perror("ERROR: clientDoWork setsockopt failed");
    exit(1);
  }

  udp_serv_addr.sin_family = AF_INET;
  udp_serv_addr.sin_addr.s_addr = INADDR_ANY;
  udp_serv_addr.sin_port = htons(clientport);

  if (bind(udpsock, (struct sockaddr *) &udp_serv_addr, sizeof(udp_serv_addr)) < 0) {
     perror("ERROR (clientDoWork) binding UDP port");
     exit(1);
  }

  //
  // Write out log information
  //
  FILE *fp;
  char filename[20];
  sprintf(filename, "%02d.out", clientid);
  fp = fopen(filename, "w");
  if (fp == NULL) {
    perror("ERROR opening client log file");
    exit(1);
  }
  fprintf(fp, "type Client\n");
  fprintf(fp, "myID %d\n", clientid);
  fprintf(fp, "pid %d\n", getpid());
  fprintf(fp, "tPort %d\n", trackerport);
  fprintf(fp, "myPort %d\n", clientport);
  printf("Client %d wrote log file %s\n", clientid, filename);
  fclose(fp);

  //// send a test message to the tracker
  //struct sockaddr_in si_other;
  //int udpsock;
  //socklen_t slen;
  //slen = sizeof(si_other);
  //if ((udpsock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
  //  perror("ERROR (clientDoWork) UDP socket creation");
  //  exit(1);
  //}
  //memset((char *) &si_other, 0, sizeof(si_other));
  //si_other.sin_family = AF_INET;
  //si_other.sin_port = htons(trackerport); // set tracker port we want to send to

	//memset(&msg, '\0', sizeof(msg));
  //sprintf(msg, "FROM_CLIENT %d test datagram", clientid);
  //bytes_sent = 0;
  //if ((bytes_sent = sendto(udpsock, msg, sizeof(msg), 0, (struct sockaddr*)&si_other, slen)) == -1) {
  //  perror("ERROR (clientDoWork) sendto");
  //  exit(1);
  //}
  //printf("client %d send %d bytes to tracker\n", clientid, bytes_sent);


  //
  // Populate datastructure with the current files I own already
  // 
  for (int i = 0; i < client->numfiles; i++) {
    char *filename = (char *)&(client->files[i]);
    FILE *fp = fopen(filename, "rb");
    if (fp == NULL) {
      printf("Cannot open file %s\n", filename);
      exit(1);
    }
    fseek(fp, 0, SEEK_END);
    long filesize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    unsigned char *filecontents = (unsigned char*)malloc(filesize); // TODO free me!
    memset(filecontents, 0, filesize);
    fread(filecontents, filesize, 1, fp);
    fclose(fp);

    struct FileInfo fileinfo;
    fileinfo.size = filesize;
    fileinfo.numsegments = (filesize / SEGMENT_SIZE) + 1;
    strncpy(fileinfo.name, filename, MAX_FILENAME);
    fileinfo.fp = filecontents;
    s_ownedFiles[i] = fileinfo;
    s_numOwnedFiles++;
  }
  ///*DEBUG*/dumpClientOwnedFiles();
  /*DEBUG*/printf("Client %d: owns %d files\n", client->id, s_numOwnedFiles);
  /*DEBUG*/for (int i = 0; i < s_numOwnedFiles; i++) {
  /*DEBUG*/  struct FileInfo *fi = &s_ownedFiles[i];
  /*DEBUG*/  printf(" File %d: %s of size %lu with %d segments\n", i, fi->name, fi->size, fi->numsegments);
  /*DEBUG*/}

  //
  // Populate datastructure with s_downloads to complete
  //
  for (int i = 0; i < MAX_FILES; i++) {
    s_downloadState[i] = false;
  }
  for (int i = 0; i < client->numtasks; i++) {
    struct Task *t = (struct Task *)&client->tasks[i];
    struct Download d;
    d.starttime = t->starttime;
    d.share = t->share;
    d.enabled = false;
    memcpy(&d.filename, &t->file, MAX_FILENAME);
    memcpy(&s_downloads[s_numDownloads], &d, sizeof(struct Download));

    int (*fp)();
   	fp = enableDownload;
    Timers_AddTimer(d.starttime, fp, (long*)s_numDownloads);
    s_numDownloads++;
  }
  ///*DEBUG*/dumpClientDownloads();
  /*DEBUG*/printf("Client %d: has %ld downloads\n", s_commInfo.clientid, s_numDownloads);
  /*DEBUG*/for (int i = 0; i < s_numDownloads; i++) {
  /*DEBUG*/  struct Download *d = &s_downloads[i];
  /*DEBUG*/  printf(" Download %d: %s\n", i, d->filename);
  /*DEBUG*/}


	struct timeval tmv;
	int status;
  fd_set active_fd_set, read_fd_set;
  FD_ZERO(&active_fd_set);
  FD_SET(udpsock, &active_fd_set);

	while (1) {
		Timers_NextTimerTime(&tmv);
		if (tmv.tv_sec == 0 && tmv.tv_usec == 0) {
      /* The timer at the head on the queue has expired  */
      Timers_ExecuteNextTimer();
      continue;
		}

    //
    // Group update to tracker
    //
    int (*fp)();
    fp = sendInterestToTracker;
    Timers_AddTimer(s_commInfo.pktdelay, fp, (int*)1);

		tmv.tv_sec = 0; tmv.tv_usec = 200; // TODO figure out what to set timer to here
    read_fd_set = active_fd_set;
		status = select(FD_SETSIZE, &read_fd_set, NULL, NULL, &tmv);
		
		if (status < 0) {
		  /* This should not happen */
			fprintf(stderr, "Select returned %d\n", status);
		} else {
			if (status == 0) {
				/* Timer expired, Hence process it  */
        Timers_ExecuteNextTimer();
				/* Execute all timers that have expired.*/
				Timers_NextTimerTime(&tmv);
				while(tmv.tv_sec == 0 && tmv.tv_usec == 0){
				  /* Timer at the head of the queue has expired  */
	        Timers_ExecuteNextTimer();
					Timers_NextTimerTime(&tmv);
				}
			}
			if (status > 0) {
				/* The socket has received data.
				   Perform packet processing. */
        if (FD_ISSET(udpsock, &read_fd_set)) {
          u_char initBuff[512]; // make this large to make life easy
          int bytesRecv = recvfrom(udpsock, initBuff, sizeof(initBuff), 0, (struct sockaddr*)&udp_serv_addr, &serv_addr_size);
          if (bytesRecv == -1) {
            perror("ERROR initial manager recv failed");
            exit(1);
          }

          // get timestamp
          struct timeval tv;
          gettimeofday(&tv, NULL);

          int16_t n_pktsize = 0;
          memcpy(&n_pktsize, &initBuff, sizeof(int16_t));
          int16_t pktsize = ntohs(n_pktsize);
          u_char *pkt = malloc(pktsize);
          memcpy(pkt, &initBuff, bytesRecv); // copy first portion of packet into full size buffer

          // get the rest of the packet
          int totalBytesRecv = bytesRecv;
          while (totalBytesRecv < pktsize) {
            bytesRecv = recvfrom(udpsock, (pkt + bytesRecv), pktsize-512, 0, (struct sockaddr*)&udp_serv_addr, &serv_addr_size);
            if (bytesRecv == -1) {
              perror("ERROR additional recvfrom failed");
              exit(1);
            }
            totalBytesRecv += bytesRecv;
          }

          // deserialize msgtype from client
          int16_t n_msgtype;
          memcpy(&n_msgtype, pkt+2, sizeof(int16_t));
          int16_t msgtype = ntohs(n_msgtype);

          if (msgtype == 42) {
            // tracker -> client group update = 42
            handleTrackerUpdate(pkt, pktsize, tv);
          } else if (msgtype == 0) {
            // client -> tracker group update = 0
          } else if (msgtype == 1) {
            // client -> client info req = 1
          } else if (msgtype == 2) {
            // client -> client info res = 2
          } else if (msgtype == 3) {
            // client -> client seg req = 3
          } else if (msgtype == 4) {
            // client -> client seg res = 4
          }
          free(pkt);
			  }
			}
		}
	}

  // TODO should terminate if downloading tasks are complete and no
  // segment requests from other clients for 2 rounds of group update

}

void handleTrackerUpdate(u_char *pktDontTouch, int16_t buffersize, struct timeval tv)
{
  // GROUP_ASSIGN, // tracker tells client about other clients
  // pktsize, msgtype, number files, filename, file size, num neighbors, neigh. id, neigh. ip, neigh port, ...

  u_char *pkt = pktDontTouch;
  pkt += 4; // move past pktsize and msgtype

  int16_t n_numfiles;
  memcpy(&n_numfiles, pkt, sizeof(int16_t));
  int16_t numfiles = ntohs(n_numfiles);
  pkt += 2;

  struct Group newGroups[numfiles];

  for (int16_t i = 0; i < numfiles; i++) {
    struct Group newGroup;
    for (int i = 0; i < MAX_CLIENTS; i++) {
      newGroup.downClients[i] = 0;
      newGroup.sharingClients[i] = 0;
    }
    memcpy(&newGroup.filename, pkt, MAX_FILENAME);
    pkt += MAX_FILENAME;

    int16_t n_filesize;
    memcpy(&n_filesize, pkt, sizeof(int16_t));
    pkt += sizeof(int16_t);
    int16_t filesize = ntohs(n_filesize);
    newGroup.filesize = filesize;


    int16_t n_numneighbors;
    memcpy(&n_numneighbors, pkt, sizeof(int16_t));
    pkt += sizeof(int16_t);
    int16_t numneighbors = ntohs(n_numneighbors);

    for (int i = 0; i < numneighbors; i++) {
      struct ClientAddr clientaddr;

      int16_t n_id;
      memcpy(&n_id, pkt, sizeof(int16_t));
      pkt += sizeof(int16_t);
      int16_t id = ntohs(n_id);
      clientaddr.id = id;
      newGroup.sharingClients[id] = 1;

      char ip[MAX_IP];
      memcpy(&ip, pkt, MAX_IP);
      pkt += MAX_IP;
      memcpy(&clientaddr.ip, ip, MAX_IP);

      int16_t n_port;
      memcpy(&n_port, pkt, sizeof(n_port));
      pkt += sizeof(int16_t);
      int16_t port = ntohs(n_port);
      clientaddr.port = port;

      //
      // update client address database
      //
      bool haveClientAddr = false;
      for (int i = 0; i < sc_numClientAddrs; i++) {
        struct ClientAddr *ca = &sc_clientAddrs[i];
        if (ca->id == clientaddr.id) {
          haveClientAddr = true;
          break;
        }
      }
      if (!haveClientAddr) {
        sc_clientAddrs[sc_numClientAddrs++] = clientaddr;
      }
    }

    //
    // update group knowledge (which clients are sharing
    //
    bool knowGroupAlready = false;
    for (int i = 0; i < sc_numGroups; i++) {
      struct Group *group = &sc_groups[i];
      if (strcmp(group->filename, newGroup.filename) == 0) {
        for (int j = 0; j < MAX_CLIENTS; j++) {
          if (newGroup.sharingClients[j] == 1) {
            group->sharingClients[j] = 1;
          }
        }
        if (group->filesize == 0 && newGroup.filesize != 0) { // TODO might be to strict, just update always
          group->filesize = newGroup.filesize;
        }
        knowGroupAlready = true;
        break;
      }
    }
    if (!knowGroupAlready) {
      sc_groups[sc_numGroups++] = newGroup;
    }

    memcpy(&newGroups[i], &newGroup, sizeof(newGroup));
  }

  //
  // Write log of received packet
  //
  FILE *fp;
  char filename[20];
  sprintf(filename, "%02d.out", s_commInfo.clientid);
  fp = fopen(filename, "a");
  if (fp == NULL) {
    perror("ERROR opening client log file for append");
    exit(1);
  }
  fprintf(fp, "%lu.%d\tFrom\tT\tGROUP_ASSIGN\t", tv.tv_sec, tv.tv_usec);
  for (int i = 0; i < numfiles; i++) {
    struct Group *ng = &newGroups[i];
    fprintf(fp, "%s ", ng->filename);
    for (int j = 0; j < MAX_CLIENTS; j++) {
      if (ng->sharingClients[j] == 1 && j != s_commInfo.clientid) {
        fprintf(fp, "%d ", j);
      }
    }
  }
  printf("\n");
  fclose(fp);
}


//
// Serialize client to be sent over the network
//
struct Client* serializeClient(struct Client *client)
{
  struct Client *s = malloc(sizeof(struct Client));
  s->id = htonl(client->id);
  s->pktdelay = htonl(client->pktdelay);
  s->pktprob = htonl(client->pktprob);
  s->numfiles = htonl(client->numfiles);
  s->numtasks = htonl(client->numtasks);
  for (int i = 0; i < client->numfiles; i++) {
    memcpy(s->files[i], &client->files[i], MAX_FILENAME);
  }
  for (int i = 0; i < client->numtasks; i++) {
    struct Task *task = (struct Task*)&(client->tasks[i]);
    struct Task *st = (struct Task*)&(s->tasks[i]);
    st->starttime = htonl(task->starttime);
    st->share = htonl(task->share);
    memcpy(st->file, &task->file, MAX_FILENAME);
  }

  return s;
}

//
// Deserialize client received over the network
//
struct Client* deserializeClient(struct Client *client)
{
  struct Client *s = malloc(sizeof(struct Client));
  s->id = ntohl(client->id);
  s->pktdelay = ntohl(client->pktdelay);
  s->pktprob = ntohl(client->pktprob);
  s->numfiles = ntohl(client->numfiles);
  s->numtasks = ntohl(client->numtasks);
  for (int i = 0; i < s->numfiles; i++) {
    memcpy(s->files[i], &client->files[i], MAX_FILENAME);
  }
  for (int i = 0; i < s->numtasks; i++) {
    struct Task *task = (struct Task*)&(client->tasks[i]);
    struct Task *st = (struct Task*)&(s->tasks[i]);
    st->starttime = htonl(task->starttime);
    st->share = htonl(task->share);
    memcpy(st->file, &task->file, MAX_FILENAME);
  }

  return s;
}


