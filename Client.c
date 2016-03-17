#include <stdio.h>
#include <stdlib.h> // exit()
#include <string.h> // memset, bzero
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <stdbool.h> // bool

#include "Client.h"

// socket sources
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
// end

/*
 * timer solution
 * create a timer for starttime to wake up client for file downloading
 * int (*fp)();
 * fp = fileTask(); fork to run this.. downloads or shares a file from clients in a loop
 * Timers_AddTimer(task->starttime,fp,(int*)1); figure out what we want to pass instead of the 1
 *
 * while (true) {
 *  if timer goes off {
 *    enable a task
 *  }
 *  if files to download {
 *    group update to tracker
 *    group update to clients in group
 *  }
 *
 *  s = select()
 *  if (s > 0) {
 *    handle request from client or tracker
 *  }
 *
 * }
 */

//
// Client worker
//
void clientDoWork(int clientid, int32_t managerport)
{
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

	///*DEBUG*/printf("(clientDoWork) connected to the manager!\n");

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

  // write out log information
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

  // TODO must wait till our starttime

  // TODO figure out current file status
  struct FileInfo files[MAX_FILES];
  int numHaveFiles = 0;
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

    unsigned char *filecontents = (unsigned char*)malloc(filesize);
    memset(filecontents, 0, filesize);
    fread(filecontents, filesize, 1, fp);
    fclose(fp);

    struct FileInfo fileinfo;
    fileinfo.size = filesize;
    fileinfo.numsegments = (filesize / SEGMENT_SIZE) + 1;
    strncpy(fileinfo.name, filename, MAX_FILENAME);
    fileinfo.fp = filecontents;
    files[i] = fileinfo;
    numHaveFiles++;
  }
  /*DEBUG*/printf("client %d: Has %d files\n", client->id, numHaveFiles);
  /*DEBUG*/for (int i = 0; i < numHaveFiles; i++) {
  /*DEBUG*/  struct FileInfo *fi = &files[i];
  /*DEBUG*/  printf("   * file %s of size %lu with %d segments\n", fi->name, fi->size, fi->numsegments);
  /*DEBUG*/}

  // one-to-one file transfer testing
  if (client->numtasks > 0) {
    struct Task *t = (struct Task *)&client->tasks[0];
    //
    // GROUP_SHOW_INTEREST (c -> t)
    //
    int16_t n_msgtype = htons(31); // GROUP_SHOW_INTEREST
    int16_t n_cid = htons(client->id);
    int16_t n_numfiles = htons(1);
    int16_t n_type;
    int16_t n_pktsize;

    // figure out type
    bool haveFileAlready = false;
    for (int i = 0; i < numHaveFiles; i++) {
      struct FileInfo *fi = &files[i];
      if (strcmp(fi->name, t->file) == 0) {
        haveFileAlready = true;
        break;
      }
    }
    if (!haveFileAlready && t->share == 0) {
      // request a group, but not willing to share
      n_type = htons(0);
    } else if (!haveFileAlready && t->share == 1) {
      // request a group, and willing to share
      n_type = htons(1);
    } else if (haveFileAlready && t->share == 1) {
      // show interest only, do not need a group, willing to share
      n_type = htons(2);
    }
    printf("file %s has type %d\n", t->file, ntohs(n_type));

    // Setup UDP socket to Tracker
    struct sockaddr_in si_other;
    int udpsock;
    socklen_t slen;
    slen = sizeof(si_other);
    if ((udpsock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
      perror("ERROR (clientDoWork) UDP socket creation");
      exit(1);
    }
    memset((char *) &si_other, 0, sizeof(si_other));
    si_other.sin_family = AF_INET;
    si_other.sin_port = htons(trackerport); // set tracker port we want to send to

    {
      // Create packet and send it
      // pktsize(16bit)
      // msgtype (16bit)
      // client_id(16bit)
      // numfiles(16bit)
      // n * filename(32bytes)
      // type(16bit)
      int numfiles = 1; // TODO should be dynamic at later phase
      int16_t pktsize = ((sizeof(int16_t)*4) + ((MAX_FILENAME + sizeof(int16_t)) * numfiles));
      u_char *pkt = malloc(pktsize);
      printf("client %d: size of one file packet = %d\n", client->id, pktsize);
      n_pktsize = htons(pktsize);
	    memset(pkt, 0, pktsize);
      memcpy(pkt, &n_pktsize, sizeof(int16_t));
      memcpy(pkt+2, &n_msgtype, sizeof(int16_t));
      memcpy(pkt+4, &n_cid, sizeof(int16_t));
      memcpy(pkt+6, &n_numfiles, sizeof(int16_t));
      memcpy(pkt+8, &t->file, MAX_FILENAME);
      memcpy(pkt+8+MAX_FILENAME, &n_type, sizeof(int16_t));

      int bytesSent = -1;
      int totalBytesSent = 0;
      while (bytesSent < totalBytesSent) {
        if ((bytesSent = sendto(udpsock, pkt, pktsize, 0, (struct sockaddr*)&si_other, slen)) == -1) {
          perror("ERROR (clientDoWork) sendto");
          exit(1);
        }
        totalBytesSent += bytesSent;
      }
      printf("client %d sent %d bytes to tracker\n", client->id, totalBytesSent);
      free(pkt);
    }
    
    // TODO log the packet that was just sent to tracker
    
    {
    }

  }
  



  // TODO should terminate if downloading tasks are complete and no
  // segment requests from other clients for 2 rounds of group update

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


