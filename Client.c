#include <stdio.h>
#include <stdlib.h> // exit()
#include <string.h> // memset, bzero
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>

#include "Client.h"

// socket sources
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
// end

//
// Client worker
//
void clientDoWork(int clientid, int32_t managerport)
{
	int sockfd;
	struct addrinfo hints, *servinfo;
  //char *ipaddr = NULL; // TODO not sure I need this

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

	printf("(clientDoWork) connected to the manager!\n");

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
  printf("DBG client TCP received %d bytes\n", recv_bytes);
  printf("totalRecv = %d\n", totalRecv);

  p_buffer = (u_char *)&buffer;
  client = (struct Client *)deserializeClient((struct Client *)p_buffer);

  ///*DEBUG*/printf("Client %d:\n", client->m_id);
  ///*DEBUG*/printf("pktdelay %d, pktprob %d, numfiles %d, numtasks %d\n", client->m_pktdelay,
  ///*DEBUG*/  client->m_pktprob, client->m_numfiles, client->m_numtasks);
  ///*DEBUG*/for (int i = 0; i < client->m_numfiles; i++) {
  ///*DEBUG*/  printf("file: %s\n", client->m_files[i]);
  ///*DEBUG*/} 
  ///*DEBUG*/for (int i = 0; i < client->m_numtasks; i++) {
  ///*DEBUG*/  printf("task %d:\n", i);
  ///*DEBUG*/  struct Task *task = (struct Task *)&(client->m_tasks[i]);
  ///*DEBUG*/  printf("%s %d %d\n", task->m_file, task->m_starttime, task->m_share);
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
    printf("DBG client TCP received %d bytes\n", recv_bytes);
    int32_t n_trackerport = 0;
    memcpy(&n_trackerport, &portmsg, sizeof(int32_t));
    trackerport = ntohl(n_trackerport);
    printf("client got tracker port %d from manager\n", trackerport);
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

  // send a test message to the tracker
  struct sockaddr_in si_other;
  int udpsock;
  socklen_t slen;
  slen = sizeof(si_other);
	memset(&msg, '\0', sizeof(msg));
	memset(&buffer, '\0', sizeof(buffer));
  if ((udpsock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
    perror("ERROR (clientDoWork) UDP socket creation");
    exit(1);
  }
  memset((char *) &si_other, 0, sizeof(si_other));
  si_other.sin_family = AF_INET;
  si_other.sin_port = htons(trackerport); // send to the tracker

  sprintf(msg, "FROM_CLIENT %d test datagram", clientid);
  printf("client about to send %s\n", msg);
  bytes_sent = 0;
  if ((bytes_sent = sendto(udpsock, msg, sizeof(msg), 0, (struct sockaddr*)&si_other, slen)) == -1) {
    perror("ERROR (clientDoWork) sendto");
    exit(1);
  }
  
  printf("client %d send %d bytes to tracker\n", clientid, bytes_sent);

  // let tracker know my interest GROUP_SHOW_INTEREST
  // packet formats
  // packet 1
  // msgtype (16bit)  client_id(16bit)
  // packet 2
  // number of files(16bit)
  // packet 3
  // filename(32bytes = 32char)
  // type(16bit)

  // TODO must wait till our starttime

  // TODO figure out current file status
  struct FileInfo files[MAX_FILES];
  int numHaveFiles = 0;
  for (int i = 0; i < client->m_numfiles; i++) {
    char *filename = (char *)&(client->m_files[i]);
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
  /*DEBUG*/printf("Has %d files\n", numHaveFiles);
  /*DEBUG*/for (int i = 0; i < numHaveFiles; i++) {
  /*DEBUG*/  struct FileInfo *fi = &files[i];
  /*DEBUG*/  printf("Have file %s of size %lu with %d segments\n", fi->name, fi->size, fi->numsegments);
  /*DEBUG*/}

  // TODO 

  
  

}

//
// Serialize client to be sent over the network
//
struct Client* serializeClient(struct Client *client)
{
  struct Client *s = malloc(sizeof(struct Client));
  s->m_id = htonl(client->m_id);
  s->m_pktdelay = htonl(client->m_pktdelay);
  s->m_pktprob = htonl(client->m_pktprob);
  s->m_numfiles = htonl(client->m_numfiles);
  s->m_numtasks = htonl(client->m_numtasks);
  for (int i = 0; i < client->m_numfiles; i++) {
    memcpy(s->m_files[i], &client->m_files[i], MAX_FILENAME);
  }
  for (int i = 0; i < client->m_numtasks; i++) {
    struct Task *task = (struct Task*)&(client->m_tasks[i]);
    struct Task *st = (struct Task*)&(s->m_tasks[i]);
    st->m_starttime = htonl(task->m_starttime);
    st->m_share = htonl(task->m_share);
    memcpy(st->m_file, &task->m_file, MAX_FILENAME);
  }

  return s;
}

//
// Deserialize client received over the network
//
struct Client* deserializeClient(struct Client *client)
{
  struct Client *s = malloc(sizeof(struct Client));
  s->m_id = ntohl(client->m_id);
  s->m_pktdelay = ntohl(client->m_pktdelay);
  s->m_pktprob = ntohl(client->m_pktprob);
  s->m_numfiles = ntohl(client->m_numfiles);
  s->m_numtasks = ntohl(client->m_numtasks);
  for (int i = 0; i < s->m_numfiles; i++) {
    memcpy(s->m_files[i], &client->m_files[i], MAX_FILENAME);
  }
  for (int i = 0; i < s->m_numtasks; i++) {
    struct Task *task = (struct Task*)&(client->m_tasks[i]);
    struct Task *st = (struct Task*)&(s->m_tasks[i]);
    st->m_starttime = htonl(task->m_starttime);
    st->m_share = htonl(task->m_share);
    memcpy(st->m_file, &task->m_file, MAX_FILENAME);
  }

  return s;
}


