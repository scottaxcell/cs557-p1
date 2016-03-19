#include <stdio.h>
#include <stdlib.h> // exit()
#include <string.h> // memset, bzero
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <stdbool.h> // bool

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
 * timer solution
 * create a timer for starttime to wake up client for file downloading
 * int (*fp)();
 * fp = fileTask(); fork to run this.. downloads or shares a file from clients in a loop
 * Timers_AddTimer(task->starttime,fp,(int*)1); figure out what we want to pass instead of the 1
 *
 * create a delay timer for a task
 *
 * while (true) {
 *  if timer goes off {
 *    run timer task: enable task (return -1 to die)
 *    run timer task: send segment to other client (return -1 to die)
 *    run timer task: can we terminate? (return 0 to reset)
 *  }
 *
 *  if files to download {
 *    send interest to tracker
 *    sendTrackerUpdate()
 *  }
 *
 *  s = select()
 *  if (s > 0) { // receive packet
 *    client seg req: create timer for segment response to wait t ms delay (delete timer on completion)
 *      sendClientSegmentReq() this function should start a request timout timer too
 *    client seg res: add segment to file I'm downloading
 *      handleClientSegRes() should reset global variable request timeout reads
 *    client info req: create timer to send segment info to client (t ms delay) (segments I have of file)
 *      sendClientInfoRes()
 *    client info res: update local segment info (client -> segment map)
 *      handleClientInfoRes() should reset requset timout variable
 *    tracker update: update group information we received from tracker
 *      handleTrackerUpdate() should reset requset timout variable
 *  } else if (s == 0) {
 *    timer expired
 *  }
 *
 *
 * }
 */


// Each forked process gets a copy of these static variables so there is no collision
static struct CommInfo commInfo;
static bool downloadState[MAX_FILES];
static struct Download downloads[MAX_FILES];
static long numDownloads = 0;
static struct FileInfo files[MAX_FILES];
static int numHaveFiles = 0;

int enableDownload(int i)
{
  /*DEBUG*/printf("Fired enableDownload(int i) timer for download i = %d\n", i);
  downloadState[i] = true;
  return -1; // negative reurn will remove timer after completion
}

//
// Send tracker group interest
//
int sendInterestToTracker()
{
  printf("Fired sendInterestToTracker\n");
  //
  // GROUP_SHOW_INTEREST (client -> tracker)
  //
  if (numDownloads == 0)
    return -1;

  int16_t n_msgtype = htons(31); // GROUP_SHOW_INTEREST
  int16_t n_cid = htons(commInfo.clientid);
  int16_t n_numfiles = htons(numDownloads);
  // Create packet and send it
  // pktsize(16bit)
  // msgtype (16bit)
  // client_id(16bit)
  // numfiles(16bit)
  // n * ( filename(32bytes) type(16bit) )
  int16_t pktsize = ((sizeof(int16_t)*4) + ((MAX_FILENAME + sizeof(int16_t)) * numDownloads));
  u_char *pkt = malloc(pktsize);
  u_char *pkt0 = pkt; // keep pointer to beginning so we can free later
  printf("client %d: size of interest packet = %d\n", commInfo.clientid, pktsize);
  int16_t n_pktsize = htons(pktsize);
  memset(pkt, 0, pktsize);
  memcpy(pkt, &n_pktsize, sizeof(int16_t));
  memcpy(pkt+2, &n_msgtype, sizeof(int16_t));
  memcpy(pkt+4, &n_cid, sizeof(int16_t));
  memcpy(pkt+6, &n_numfiles, sizeof(int16_t));
  pkt = pkt + 8;

  for (int i = 0; i < numDownloads; i++) {
    struct Download *d = &downloads[i];

    // figure out type
    int16_t n_type;
    bool haveFileAlready = false;
    for (int i = 0; i < numHaveFiles; i++) {
      struct FileInfo *fi = &files[i];
      if (strcmp(fi->name, d->filename) == 0) {
        haveFileAlready = true;
        break;
      }
    }
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
    printf("  client %d task file %s has type %d\n", commInfo.clientid, d->filename, ntohs(n_type));

    memcpy(pkt, &d->filename, MAX_FILENAME);
    pkt = pkt + MAX_FILENAME;
    memcpy(pkt, &n_type, sizeof(int16_t));
    pkt = pkt + 2;
  }

  struct sockaddr_in si_other;
  socklen_t slen;
  slen = sizeof(si_other);
  memset((char *) &si_other, 0, sizeof(si_other));
  si_other.sin_family = AF_INET;
  printf("udpsock %d\n", commInfo.udpsock);
  printf("tracker port %d\n", commInfo.trackerport);
  si_other.sin_port = htons(commInfo.trackerport); // set tracker port we want to send to

  int bytesSent = -1, totalBytesSent = 0;
  while (bytesSent < totalBytesSent) {
    if ((bytesSent = sendto(commInfo.udpsock, pkt0, pktsize, 0, (struct sockaddr*)&si_other, slen)) == -1) {
      perror("ERROR (sendInterestToTracker) sendto");
      exit(1);
    }
    totalBytesSent += bytesSent;
  }
  printf("client %d sent %d bytes to tracker\n", commInfo.clientid, totalBytesSent);
  free(pkt0);
  
  // TODO log the packet that was just sent to tracker
  return -1;
}



//
// Client worker
//
void clientDoWork(int clientid, int32_t managerport)
{

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
  commInfo.pktdelay = client->pktdelay;
  commInfo.pktprob = client->pktprob;
  commInfo.clientid = client->id;

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
    commInfo.trackerport = trackerport;
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
  commInfo.myport = clientport;

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
  commInfo.udpsock = udpsock;

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
  // Populate datastructure with the current files I have
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
    files[i] = fileinfo;
    numHaveFiles++;
  }
  /*DEBUG*/printf("client %d: Has %d files\n", client->id, numHaveFiles);
  /*DEBUG*/for (int i = 0; i < numHaveFiles; i++) {
  /*DEBUG*/  struct FileInfo *fi = &files[i];
  /*DEBUG*/  printf("   * file %s of size %lu with %d segments\n", fi->name, fi->size, fi->numsegments);
  /*DEBUG*/}

  //
  // Populate datastructure with downloads to complete
  //
  struct Download {
    int starttime;
    int share;
    bool enabled;
    char filename[MAX_FILENAME];
  };

  for (int i = 0; i < MAX_FILES; i++) {
    downloadState[i] = false;
  }
  if (client->numtasks > 0) {
    for (int i = 0; i < client->numtasks; i++) {
      struct Task *t = (struct Task *)&client->tasks[i];
      struct Download d;
      d.starttime = t->starttime;
      d.share = t->share;
      d.enabled = false;
      memcpy(&d.filename, &t->file, MAX_FILENAME);
      memcpy(&downloads[numDownloads], &d, sizeof(struct Download));

      int (*fp)();
     	fp = enableDownload;
	    Timers_AddTimer(d.starttime, fp, (long*)numDownloads);
      numDownloads++;
    }
  }

  //
  // TODO Event loop doing the actual work now
  //
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
    Timers_AddTimer(commInfo.pktdelay, fp, (int*)1);

		tmv.tv_sec = 3; tmv.tv_usec = 0; // TODO figure out what to set timer to here
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
        //handleRecvData();
        
			}
		}
	}

  // one-to-one file transfer testing
//  if (client->numtasks > 0) {
//    struct Task *t = (struct Task *)&client->tasks[0];
//    //
//    // GROUP_SHOW_INTEREST (c -> t)
//    //
//    int16_t n_msgtype = htons(31); // GROUP_SHOW_INTEREST
//    int16_t n_cid = htons(client->id);
//    int16_t n_numfiles = htons(1);
//    int16_t n_type;
//    int16_t n_pktsize;
//
//    // figure out type
//    bool haveFileAlready = false;
//    for (int i = 0; i < numHaveFiles; i++) {
//      struct FileInfo *fi = &files[i];
//      if (strcmp(fi->name, t->file) == 0) {
//        haveFileAlready = true;
//        break;
//      }
//    }
//    if (!haveFileAlready && t->share == 0) {
//      // request a group, but not willing to share
//      n_type = htons(0);
//    } else if (!haveFileAlready && t->share == 1) {
//      // request a group, and willing to share
//      n_type = htons(1);
//    } else if (haveFileAlready && t->share == 1) {
//      // show interest only, do not need a group, willing to share
//      n_type = htons(2);
//    }
//    printf("file %s has type %d\n", t->file, ntohs(n_type));
//
//    // Setup UDP socket to Tracker
//    struct sockaddr_in si_other;
//    int udpsock;
//    socklen_t slen;
//    slen = sizeof(si_other);
//    if ((udpsock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
//      perror("ERROR (clientDoWork) UDP socket creation");
//      exit(1);
//    }
//    memset((char *) &si_other, 0, sizeof(si_other));
//    si_other.sin_family = AF_INET;
//    si_other.sin_port = htons(trackerport); // set tracker port we want to send to
//
//    {
//      // Create packet and send it
//      // pktsize(16bit)
//      // msgtype (16bit)
//      // client_id(16bit)
//      // numfiles(16bit)
//      // n * filename(32bytes)
//      // type(16bit)
//      int numfiles = 1; // TODO should be dynamic at later phase
//      int16_t pktsize = ((sizeof(int16_t)*4) + ((MAX_FILENAME + sizeof(int16_t)) * numfiles));
//      u_char *pkt = malloc(pktsize);
//      printf("client %d: size of one file packet = %d\n", client->id, pktsize);
//      n_pktsize = htons(pktsize);
//	    memset(pkt, 0, pktsize);
//      memcpy(pkt, &n_pktsize, sizeof(int16_t));
//      memcpy(pkt+2, &n_msgtype, sizeof(int16_t));
//      memcpy(pkt+4, &n_cid, sizeof(int16_t));
//      memcpy(pkt+6, &n_numfiles, sizeof(int16_t));
//      memcpy(pkt+8, &t->file, MAX_FILENAME);
//      memcpy(pkt+8+MAX_FILENAME, &n_type, sizeof(int16_t));
//
//      int bytesSent = -1;
//      int totalBytesSent = 0;
//      while (bytesSent < totalBytesSent) {
//        if ((bytesSent = sendto(udpsock, pkt, pktsize, 0, (struct sockaddr*)&si_other, slen)) == -1) {
//          perror("ERROR (clientDoWork) sendto");
//          exit(1);
//        }
//        totalBytesSent += bytesSent;
//      }
//      printf("client %d sent %d bytes to tracker\n", client->id, totalBytesSent);
//      free(pkt);
//    }
//    
//    // TODO log the packet that was just sent to tracker
//    
//    {
//    }
//
//  }
  



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


