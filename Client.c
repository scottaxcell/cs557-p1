#include <stdio.h>
#include <stdlib.h> // exit()
#include <string.h> // memset, bzero
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <stdbool.h> // bool
#include <time.h>    // time()

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

// File Contents:
// All functions that a client process needs to complete it's job of sharing
// and downloading files.

//
// Static globals for usage by a client process
//
// Each forked process gets a copy of these static variables so there is no collision
static struct CommInfo s_commInfo;

static struct Download s_downloads[MAX_FILES];
static long s_numDownloads = 0;

static struct FileInfo s_ownedFiles[MAX_FILES];
static int s_numOwnedFiles = 0;

static struct ClientAddr sc_clientAddrs[MAX_CLIENTS];
static int sc_numClientAddrs = 0;

static struct Group sc_groups[MAX_FILES];
static int sc_numGroups = 0;

static int sc_numGroupUpdates = 0; // keep track of group updates so we know when to terminate
static int sc_numDownloadProgress = 0; // keep track of group updates so we know when to terminate


void dumpClientDownloads()
{
  printf("Client %d has %ld downloads:\n", s_commInfo.clientid, s_numDownloads);
  for (int i = 0; i < s_numDownloads; i++) {
    struct Download *d = &s_downloads[i];
    printf("  Download %d: %s, filesize %d, numsegments %d, share %d\n", i, d->filename, d->filesize, d->numFileSegments, d->share);
    printf("  Have file segments: ");
    for (int j = 0; j < d->numFileSegments; j++) {
      printf("%d:%d ",j, d->haveFileSegments[j]);
    }
    printf("\n");
    printf("  Segment to client: ");
    for (int j = 0; j < d->numFileSegments; j++) {
      printf("%d:%d ",j, d->segmentToClient[j]);
    }
    printf("\n");
  }
}

void dumpClientOwnedFiles()
{
  printf("Client %d: owns %d files\n", s_commInfo.clientid, s_numOwnedFiles);
  for (int i = 0; i < s_numOwnedFiles; i++) {
    struct FileInfo *fi = &s_ownedFiles[i];
    printf("  File %d: %s of size %d with %d segments\n", i, fi->name, fi->size, fi->numsegments);
    printf("  Segment sizes: ");
    for (int j = 0; j < fi->numsegments; j++) {
      printf("%d:%d ", j, fi->segmentSizes[j]);
    }
    printf("\n");
  }
}

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
    printf("  ClientAddr %d: %s:%d\n", ca->id, ca->ip, ca->port);
  }
}


void dumpClientGroups()
{
  printf("Client %d knows about %d groups\n", s_commInfo.clientid, sc_numGroups);
  for (int i = 0; i < sc_numGroups; i++) {
    struct Group *g = &sc_groups[i];
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


void dumpDeserializedMsg(unsigned char *pktDontTouch)
{
  // MSG FORMAT = pktsize, msgtype, client id, number files, filename, file size, type, ...
  unsigned char *pkt = pktDontTouch;

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

    int32_t n_filesize;
    memcpy(&n_filesize, pkt, sizeof(n_filesize));
    int32_t filesize = ntohs(n_filesize);
    pkt += sizeof(n_filesize);

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
  s_downloads[i].enabled = true;
  return -1; // negative reurn will remove timer after completion
}

int16_t getFileSize(char *filename)
{
  for (int i = 0; i < sc_numGroups; i++) {
    struct Group *group = &sc_groups[i];
    if (strncmp(group->filename, filename, strlen(group->filename)) == 0) {
      return group->filesize;
    }
  }
  return 0;
}

int16_t getClientPort(int16_t clientid)
{
  for (int i = 0; i < sc_numClientAddrs; i++) {
    struct ClientAddr *ca = &sc_clientAddrs[i];
    if (clientid == ca->id) {
      return ca->port;
    }
  }
  return -1;
}

int16_t getClientId(uint16_t port)
{
  for (int i = 0; i < sc_numClientAddrs; i++) {
    struct ClientAddr *ca = &sc_clientAddrs[i];
    if (port == ca->port) {
      return ca->id;
    }
  }
  return -1;
}


int handleClientInfoRequest(unsigned char *pktDontTouch, struct timeval tv, struct sockaddr_in cliaddr)
{
  // CLNT_INFO_REQ, // client asks other client for file info
  // pktsize, msgtype, client id, filename
  sc_numGroupUpdates = 0; // reset counter so we don't terminate too soon

  unsigned char *pkt = pktDontTouch;
  
  // ignore pktsize and msgtype
  pkt += (2 * sizeof(int16_t));

  int16_t n_cid;
  memcpy(&n_cid, pkt, sizeof(n_cid));
  pkt += sizeof(n_cid);
  int16_t cid = ntohs(n_cid);

  char filename[MAX_FILENAME];
  memcpy(&filename, pkt, MAX_FILENAME);
  
  //printf("Client %d received CLNT_INFO_REQ from client %d for filename %s\n", s_commInfo.clientid, cid, filename);

  //
  // Log received CLNT_INFO_REQ message
  //
  uint16_t fromPort = ntohs(cliaddr.sin_port);
  int16_t sendingClientId = getClientId(fromPort);
  if (sendingClientId == -1) {
    // update client addrs database
    struct ClientAddr clientaddr;
    clientaddr.id = cid;
    clientaddr.port = fromPort;
    char *ip = inet_ntoa(cliaddr.sin_addr);
    memcpy(&clientaddr.ip, ip, MAX_IP);
    sc_clientAddrs[sc_numClientAddrs++] = clientaddr;
  }

  FILE *fp;
  char logfile[20];
  sprintf(logfile, "%02d.out", s_commInfo.clientid);
  fp = fopen(logfile, "a");
  if (fp == NULL) {
    perror("ERROR opening client log file for append");
    exit(1);
  }
  fprintf(fp, "%lu.%d\tFrom\t%d\tCLNT_INFO_REQ\t%s\n", tv.tv_sec, tv.tv_usec, cid, filename);
  fclose(fp);

  // Reply with the segment information requested
  // CLNT_INFO_REP, // client tells other client about file segments it has
  //   pktsize, msgtype, client id, filename, num segments, segment number, ...
    
  int numSegments = 0;
  for (int i = 0; i < s_numOwnedFiles; i++) {
    struct FileInfo *fi = &s_ownedFiles[i];
    if (strncmp(fi->name, filename, strlen(fi->name)) == 0) {
      numSegments = fi->numsegments;
      break;
    }
  }

  if (numSegments == 0) {
    ///*DEBUG*/printf("Client %d DOES NOT HAVE segments for %s\n", s_commInfo.clientid, filename);
    return -1;
  }

  ///*DEBUG*/printf("Client %d HAS segments for %s\n", s_commInfo.clientid, filename);

  int16_t sendPktSize = (sizeof(int16_t) * 5) + MAX_FILENAME + (sizeof(int16_t) * numSegments);
  int16_t n_sendPktSize = htons(sendPktSize);
  int16_t n_sendMsgtype = htons(1);
  int16_t n_numSegments = htons(numSegments);
  int16_t n_sendClientId = htons(s_commInfo.clientid);
  unsigned char *sendPktDontTouch = malloc(sendPktSize);
  unsigned char *sendPkt = sendPktDontTouch;

  memcpy(sendPkt, &n_sendPktSize, sizeof(n_sendPktSize));
  sendPkt += sizeof(n_sendPktSize);

  memcpy(sendPkt, &n_sendMsgtype, sizeof(n_sendMsgtype));
  sendPkt += sizeof(n_sendMsgtype);

  memcpy(sendPkt, &n_sendClientId, sizeof(n_sendClientId));
  sendPkt += sizeof(n_sendClientId);

  memcpy(sendPkt, &filename, MAX_FILENAME);
  sendPkt += MAX_FILENAME;

  memcpy(sendPkt, &n_numSegments, sizeof(n_numSegments));
  sendPkt += sizeof(n_numSegments);

  for (int i = 0; i < numSegments; i++) {
    int16_t n_segmentNum = htons(i);
    memcpy(sendPkt, &n_segmentNum, sizeof(n_segmentNum));
    sendPkt += sizeof(n_segmentNum);
  }

  struct SendData *sd = malloc(sizeof(struct SendData));
  sd->pkt = sendPktDontTouch;
  sd->pktsize = sendPktSize;
  sd->cliaddr = cliaddr;

  int (*funcp)();
  funcp = sendClientInfoReply;
  Timers_AddTimer(s_commInfo.pktdelay, funcp, (struct SendData *)sd);

  return -1;
}


int handleClientSegmentRequest(unsigned char *pktDontTouch, struct timeval tv, struct sockaddr_in cliaddr)
{
  // CLNT_SEG_REQ, // client asks other client for a file segment
  // pktsize, msgtype, client id, filename, segment number
  sc_numGroupUpdates = 0; // reset counter so we don't terminate too soon

  unsigned char *pkt = pktDontTouch;
  
  // ignore pktsize and msgtype
  pkt += (2 * sizeof(int16_t));

  int16_t n_cid;
  memcpy(&n_cid, pkt, sizeof(n_cid));
  pkt += sizeof(n_cid);
  int16_t cid = ntohs(n_cid);

  char filename[MAX_FILENAME];
  memcpy(&filename, pkt, MAX_FILENAME);
  pkt += MAX_FILENAME;

  int16_t n_segment;
  memcpy(&n_segment, pkt, sizeof(n_segment));
  pkt += sizeof(n_segment);
  int16_t segment = ntohs(n_segment);

  //printf("Client %d received CLNT_SEG_REQ from client %d for filename %s %d\n", s_commInfo.clientid, cid, filename, segment);

  //
  // Log received CLNT_SEG_REQ message
  //
  uint16_t fromPort = ntohs(cliaddr.sin_port);
  int16_t sendingClientId = getClientId(fromPort);
  if (sendingClientId == -1) {
    // update client addrs database
    struct ClientAddr clientaddr;
    clientaddr.id = cid;
    clientaddr.port = fromPort;
    char *ip = inet_ntoa(cliaddr.sin_addr);
    memcpy(&clientaddr.ip, ip, MAX_IP);
    sc_clientAddrs[sc_numClientAddrs++] = clientaddr;
  }

  FILE *fp;
  char logfile[20];
  sprintf(logfile, "%02d.out", s_commInfo.clientid);
  fp = fopen(logfile, "a");
  if (fp == NULL) {
    perror("ERROR opening client log file for append");
    exit(1);
  }
  fprintf(fp, "%lu.%d\tFrom\t%d\tCLNT_SEG_REQ\t%s\n", tv.tv_sec, tv.tv_usec, cid, filename);
  fclose(fp);

  // Reply with the segment information requested
  // CLNT_SEG_REP // client sends other client file segment
  // pktsize, msgtype, client id, filename, segment, size of segment, raw file data

  bool haveReqSegment = false;
  for (int i = 0; i < s_numDownloads; i++) {
    struct Download *d = &s_downloads[i];
    if (strncmp(filename, d->filename, strlen(d->filename)) == 0) {
      if (d->haveFileSegments[segment] == 1) {
        haveReqSegment = true;
      }
      break;
    }
  }

  if (haveReqSegment == false)
    return -1;
    
  unsigned char rawData[SEGMENT_SIZE];
  int16_t segmentSize = 0;
  for (int i = 0; i < s_numOwnedFiles; i++) {
    struct FileInfo *fi = &s_ownedFiles[i];
    if (strncmp(fi->name, filename, strlen(fi->name)) == 0) {
      segmentSize = fi->segmentSizes[segment];
      memcpy(&rawData, fi->fp+(segment*SEGMENT_SIZE), segmentSize);
      break;
    }
  }

  int16_t sendPktSize = (sizeof(int16_t) * 3) + MAX_FILENAME + segmentSize;
  int16_t n_sendPktSize = htons(sendPktSize);
  int16_t n_sendMsgtype = htons(3);
  int16_t n_sendClientId = htons(s_commInfo.clientid);
  int16_t n_sendSegmentSize = htons(segmentSize);
  int16_t n_sendSegment = htons(segment);
  unsigned char *sendPktDontTouch = malloc(sendPktSize);
  unsigned char *sendPkt = sendPktDontTouch;

  memcpy(sendPkt, &n_sendPktSize, sizeof(n_sendPktSize));
  sendPkt += sizeof(n_sendPktSize);

  memcpy(sendPkt, &n_sendMsgtype, sizeof(n_sendMsgtype));
  sendPkt += sizeof(n_sendMsgtype);

  memcpy(sendPkt, &n_sendClientId, sizeof(n_sendClientId));
  sendPkt += sizeof(n_sendClientId);

  memcpy(sendPkt, &filename, MAX_FILENAME);
  sendPkt += MAX_FILENAME;

  memcpy(sendPkt, &n_sendSegment, sizeof(n_sendSegment));
  sendPkt += sizeof(n_sendSegment);

  memcpy(sendPkt, &n_sendSegmentSize, sizeof(n_sendSegmentSize));
  sendPkt += sizeof(n_sendSegmentSize);

  memcpy(sendPkt, &rawData, segmentSize);
  sendPkt += sizeof(rawData);

  struct SendData *sd = malloc(sizeof(struct SendData));
  sd->pkt = sendPktDontTouch;
  sd->pktsize = sendPktSize;
  sd->cliaddr = cliaddr;

  ///*DEBUG*/dumpSegRepMsg(sd->pkt);
  int (*funcp)();
  funcp = sendClientSegmentReply;
  Timers_AddTimer(s_commInfo.pktdelay, funcp, (struct SendData *)sd);

  return -1;
}


int sendClientSegmentRequest(struct SendData *sd)
{
  ///*DEBUG*/dumpSegReqMsg(sd->pkt);
  struct sockaddr_in si_other;
  socklen_t slen = sizeof(si_other);
  memset((char *) &si_other, 0, sizeof(si_other));
  si_other.sin_family = AF_INET;
  si_other.sin_port = sd->cliaddr.sin_port;
  uint16_t fromPort = ntohs(sd->cliaddr.sin_port);
  int16_t cid = getClientId(fromPort);
  if (cid == -1) {
    // don't know client address yet

  }

  int bytesSent = 0, totalBytesSent = 0;
  while (totalBytesSent < sd->pktsize) {
    if ((bytesSent = sendto(s_commInfo.udpsock, sd->pkt, sd->pktsize, 0, (struct sockaddr*)&si_other, slen)) == -1) {
      perror("ERROR (sed CLNT_SEG_REP) sendto");
      exit(1);
    }
    totalBytesSent += bytesSent;
  }
  struct timeval ltv;
  gettimeofday(&ltv, NULL);

  unsigned char *pkt = sd->pkt;
  //int16_t n_pktsize;
  //memcpy(&n_pktsize, pkt, sizeof(int16_t));
  //int16_t pktsize = ntohs(n_pktsize);
  pkt += 2; 

  //int16_t n_msgtype;
  //memcpy(&n_msgtype, pkt, sizeof(int16_t));
  //int16_t msgtype = ntohs(n_msgtype);
  pkt += 2; 

  int16_t n_id;
  memcpy(&n_id, pkt, sizeof(int16_t));
  int16_t id = ntohs(n_id);
  pkt += 2; 

  char filename[MAX_FILENAME];
  memcpy(filename, pkt, MAX_FILENAME);
  pkt += MAX_FILENAME;

  int16_t n_segment;
  memcpy(&n_segment, pkt, sizeof(int16_t));
  int16_t segment = ntohs(n_segment);

  FILE *fp;
  char logfile[20];
  sprintf(logfile, "%02d.out", s_commInfo.clientid);
  fp = fopen(logfile, "a");
  if (fp == NULL) {
    perror("ERROR opening client log file for append");
    exit(1);
  }
  fprintf(fp, "%lu.%d\tTo\t%d\tCLNT_SEG_REQ\t%s %d\n", ltv.tv_sec, ltv.tv_usec, id, filename, segment);
  //printf("Client %d sending CLNT_SEG_REQ for filename %s %d\n", id, filename, segment );
  fclose(fp);

  free(sd->pkt);
  free(sd);

  return -1;
}


void writeDownloadedFile(struct Download *d)
{
  FILE *fp;
  char filename[MAX_FILENAME];
  sprintf(filename, "%d-", s_commInfo.clientid);
  strncat(filename, d->filename, MAX_FILENAME); // skip original client id and dash
  printf("Client %d is writing downloaded file %s!\n", s_commInfo.clientid, filename);

  fp = fopen(filename, "wb");
  if (fp == NULL) {
    printf("ERROR opening %s for writing", filename);
    exit(1);
  }
  fwrite(d->rawFile, sizeof(unsigned char), d->filesize, fp);
  fclose(fp);

  for (int i = 0; i < d->filesize; i++) {
    
  }
}


int sendClientSegmentRequests()
{
  //printf("Client %d fired off sendClientSegmentRequests\n", s_commInfo.clientid);
  // if not done downloading - randomly select 8 segments don't have to download
  // and send a request to the associated clients
  // start 
  for (int i = 0; i < s_numDownloads; i++) {
    struct Download *d = &s_downloads[i];
    if (d->doneDownloading == false) {
      bool haveFileAlready = false;
      for (int i = 0; i < s_numOwnedFiles; i++) {
        struct FileInfo *fi = &s_ownedFiles[i];
        if (strncmp(fi->name, d->filename, strlen(d->filename)) == 0) {
          haveFileAlready = true;
          break;
        }
      }
      if (haveFileAlready == true || d->numFileSegments == 0)
        continue;

      //printf("Client %d prepping to get segments for download %s\n", s_commInfo.clientid, d->filename);
      // figure out 8 random segments I still need to complete the  download
      int numRequiredSegments = 0;
      for (int j = 0; j < d->numFileSegments; j++) {
        if (d->haveFileSegments[j] == 0) {
          numRequiredSegments++;
        }
      }
      //printf("Client %d needs %d segments to complete download of download %s\n", s_commInfo.clientid, numRequiredSegments, d->filename);
      if (numRequiredSegments == 0) {
        // done downloading
        d->doneDownloading = true;

        
        // if we aren't the owner of the file already, write it out!
        bool haveFileAlready = false;
        for (int i = 0; i < s_numOwnedFiles; i++) {
          struct FileInfo *fi = &s_ownedFiles[i];
          if (strncmp(fi->name, d->filename, strlen(d->filename)) == 0) {
            haveFileAlready = true;
            break;
          }
        }
        if (haveFileAlready == false) {
          // write out the file to disk
          writeDownloadedFile(d);
  
          // copy file over to owned files container
          struct FileInfo fileinfo;
          fileinfo.size = d->filesize;
          fileinfo.numsegments = d->numFileSegments;
          strncpy(fileinfo.name, d->filename, MAX_FILENAME);
          fileinfo.fp = d->rawFile;
          for (int j = 0; j < MAX_SEGMENTS; j++) {
            fileinfo.segmentSizes[j] = MAX_SEGMENTS;
          }
  
          // figure out last segment size and set it
          int16_t lastSize = fileinfo.size - (SEGMENT_SIZE - fileinfo.numsegments);
          memset(&fileinfo.segmentSizes[fileinfo.numsegments-1], lastSize, sizeof(lastSize));
          fileinfo.segmentSizes[fileinfo.numsegments-1] = lastSize;
          
          // add file to our owned files database
          memcpy(&s_ownedFiles[s_numOwnedFiles++], &fileinfo, sizeof(struct FileInfo));
        }
        continue;
      }

      int requiredSegments[numRequiredSegments];
      int reqSegmentCnt = 0;
      for (int j = 0; j < d->numFileSegments; j++) {
        if (d->haveFileSegments[j] == 0) {
          requiredSegments[reqSegmentCnt++] = j;
        }
      }
        
      int numSegmentRequests = 8;
      if (numRequiredSegments < 8)
        numSegmentRequests = numRequiredSegments;
      //printf("Client %d looping for %d segment requests for download %s\n", s_commInfo.clientid, numSegmentRequests, d->filename);

      while (numSegmentRequests > 0) {
        int randomSegment = rand() % numRequiredSegments; // chance of selecting the same segment more than once, but should be ok
        int segmentNum = requiredSegments[randomSegment];
        int16_t clientId = d->segmentToClient[segmentNum];
        if (clientId == 25) {
          numSegmentRequests--;
          continue;
        }
        //printf("Client %d sending CLNT_SEG_REQ to client %d for segment %d\n", s_commInfo.clientid, clientId, segmentNum);

        struct sockaddr_in cliaddr;
        memset((char *) &cliaddr, 0, sizeof(cliaddr));
        cliaddr.sin_family = AF_INET;
        cliaddr.sin_port = htons(getClientPort(clientId));
        
        // CLNT_SEG_REQ, // client asks other client for a file segment
        //   pktsize, msgtype, client id, filename, segment number
        int16_t sendPktSize = (sizeof(int16_t) * 4) + MAX_FILENAME;
        int16_t n_sendPktSize = htons(sendPktSize);
        int16_t n_sendMsgtype = htons(2);
        int16_t n_sendClientId = htons(s_commInfo.clientid);
        int16_t n_sendSegment = htons(segmentNum);
        unsigned char *sendPktDontTouch = malloc(sendPktSize);
        unsigned char *sendPkt = sendPktDontTouch;

        memcpy(sendPkt, &n_sendPktSize, sizeof(n_sendPktSize));
        sendPkt += sizeof(n_sendPktSize);

        memcpy(sendPkt, &n_sendMsgtype, sizeof(n_sendMsgtype));
        sendPkt += sizeof(n_sendMsgtype);

        memcpy(sendPkt, &n_sendClientId, sizeof(n_sendClientId));
        sendPkt += sizeof(n_sendClientId);

        memcpy(sendPkt, &d->filename, MAX_FILENAME);
        sendPkt += MAX_FILENAME;

        memcpy(sendPkt, &n_sendSegment, sizeof(n_sendSegment));
        sendPkt += sizeof(n_sendSegment);

        struct SendData *sd = malloc(sizeof(struct SendData));
        sd->pkt = sendPktDontTouch;
        sd->pktsize = sendPktSize;
        sd->cliaddr = cliaddr;

        ///*DEBUG*/dumpSegReqMsg(sd->pkt);
        int (*funcp)();
        funcp = sendClientSegmentRequest;
        Timers_AddTimer(s_commInfo.pktdelay, funcp, (struct SendData *)sd);
        numSegmentRequests--;
      }
      //printf("Client %d DONE looping for %d segment requests for download %s\n", s_commInfo.clientid, numSegmentRequests, d->filename);
    }
  }

  return 0;
}

void dumpSegReqMsg(unsigned char *pktDontTouch)
{
  unsigned char *pkt = pktDontTouch;
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

  char filename[MAX_FILENAME];
  memcpy(filename, pkt, MAX_FILENAME);
  pkt += MAX_FILENAME;

  int16_t n_segment;
  memcpy(&n_segment, pkt, sizeof(int16_t));
  int16_t segment = ntohs(n_segment);

  printf("Client %d CLNT_SEG_REQ = pktsize %d, msgtype %d, filename %s, segment %d\n", id, pktsize, msgtype, filename, segment);

}

void dumpSegRepMsg(unsigned char *pktDontTouch)
{
  unsigned char *pkt = pktDontTouch;
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

  char filename[MAX_FILENAME];
  memcpy(filename, pkt, MAX_FILENAME);
  pkt += MAX_FILENAME;

  int16_t n_segment;
  memcpy(&n_segment, pkt, sizeof(int16_t));
  int16_t segment = ntohs(n_segment);

  printf("Client %d CLNT_SEG_REP to client %d = pktsize %d, msgtype %d, filename %s, segment %d\n", s_commInfo.clientid, id, pktsize, msgtype, filename, segment);

}


int sendClientSegmentReply(struct SendData *sd)
{
  struct sockaddr_in si_other;
  socklen_t slen = sizeof(si_other);
  memset((char *) &si_other, 0, sizeof(si_other));
  si_other.sin_family = AF_INET;
  si_other.sin_port = sd->cliaddr.sin_port;
  uint16_t fromPort = ntohs(sd->cliaddr.sin_port);
  int16_t cid = getClientId(fromPort);

  int bytesSent = 0, totalBytesSent = 0;
  while (totalBytesSent < sd->pktsize) {
    if ((bytesSent = sendto(s_commInfo.udpsock, sd->pkt, sd->pktsize, 0, (struct sockaddr*)&si_other, slen)) == -1) {
      perror("ERROR (sed CLNT_SEG_REP) sendto");
      exit(1);
    }
    totalBytesSent += bytesSent;
  }
  struct timeval ltv;
  gettimeofday(&ltv, NULL);

  char filename[MAX_FILENAME];
  memcpy(&filename, sd->pkt+6, MAX_FILENAME);

  FILE *fp;
  char logfile[20];
  sprintf(logfile, "%02d.out", s_commInfo.clientid);
  fp = fopen(logfile, "a");
  if (fp == NULL) {
    perror("ERROR opening client log file for append");
    exit(1);
  }
  fprintf(fp, "%lu.%d\tTo\t%d\tCLNT_SEG_REP\t%s\n", ltv.tv_sec, ltv.tv_usec, cid, filename);
  //printf("Client %d sending CLNT_SEG_REP to client %d for filename %s \n", s_commInfo.clientid, cid, filename );
  fclose(fp);

  free(sd->pkt);
  free(sd);

  return -1;
}


int handleClientInfoReply(unsigned char *pktDontTouch, struct timeval tv, struct sockaddr_in cliaddr)
{
  // CLNT_INFO_REP, // client tells other client about file segments it has
  // pktsize, msgtype, client id, filename, num segments, segment number, ...

  unsigned char *pkt = pktDontTouch;
  
  // ignore pktsize and msgtype
  pkt += (2 * sizeof(int16_t));

  int16_t n_cid;
  memcpy(&n_cid, pkt, sizeof(n_cid));
  pkt += sizeof(n_cid);
  int16_t cid = ntohs(n_cid);

  char filename[MAX_FILENAME];
  memcpy(&filename, pkt, MAX_FILENAME);
  pkt += MAX_FILENAME;
  
  //printf("Client %d received CLNT_INFO_REP from client %d for filename %s\n", s_commInfo.clientid, cid, filename);

  int16_t n_numSegments;
  memcpy(&n_numSegments, pkt, sizeof(n_numSegments));
  pkt += sizeof(n_numSegments);
  int16_t numSegments = ntohs(n_numSegments);
  unsigned char *segmentPointer = pkt;

  //
  // Update download with new meta data
  //
  for (int i = 0; i < s_numDownloads; i++) {
    struct Download *d = &s_downloads[i];
    if (strncmp(filename, d->filename, strlen(d->filename)) == 0) {
      for (int i = 0; i < numSegments; i++) {
        int16_t n_segment;
        memcpy(&n_segment, pkt, sizeof(n_segment));
        pkt += sizeof(n_segment);
        int16_t segment = ntohs(n_segment);
        d->segmentToClient[segment] = cid;
        //printf("Updating segmentToClient segment %d with client %d\n", segment, cid);
      }
      break;
    }
  }

  //
  // Log received CLNT_INFO_REP message
  //
  uint16_t fromPort = ntohs(cliaddr.sin_port);
  int16_t sendingClientId = getClientId(fromPort);
  if (sendingClientId == -1) {
    // update client addrs database
    struct ClientAddr clientaddr;
    clientaddr.id = cid;
    clientaddr.port = fromPort;
    char *ip = inet_ntoa(cliaddr.sin_addr);
    memcpy(&clientaddr.ip, ip, MAX_IP);
    sc_clientAddrs[sc_numClientAddrs++] = clientaddr;
  }

  FILE *fp;
  char logfile[20];
  sprintf(logfile, "%02d.out", s_commInfo.clientid);
  fp = fopen(logfile, "a");
  if (fp == NULL) {
    perror("ERROR opening client log file for append");
    exit(1);
  }
  fprintf(fp, "%lu.%d\tFrom\t%d\tCLNT_INFO_REP\t%s ", tv.tv_sec, tv.tv_usec, cid, filename);
  for (int i = 0; i < numSegments; i++) {
    int16_t n_segment;
    memcpy(&n_segment, segmentPointer, sizeof(n_segment));
    segmentPointer += sizeof(n_segment);
    int16_t segment = ntohs(n_segment);
    fprintf(fp, "%d ", segment);
  }
  fprintf(fp, "\n");
  fclose(fp);

  return -1;
}

int handleClientSegmentReply(unsigned char *pktDontTouch, struct timeval tv, struct sockaddr_in cliaddr)
{
  // CLNT_SEG_REP // client sends other client file segment
  // pktsize, msgtype, client id, filename, segment, size of segment raw file data
  sc_numDownloadProgress = 0; // reset download tracker since we got a file segment

  unsigned char *pkt = pktDontTouch;
  
  // ignore pktsize and msgtype
  pkt += (2 * sizeof(int16_t));

  int16_t n_cid;
  memcpy(&n_cid, pkt, sizeof(n_cid));
  pkt += sizeof(n_cid);
  int16_t cid = ntohs(n_cid);

  char filename[MAX_FILENAME];
  memcpy(&filename, pkt, MAX_FILENAME);
  pkt += MAX_FILENAME;

  int16_t n_segment;
  memcpy(&n_segment, pkt, sizeof(n_segment));
  pkt += sizeof(n_segment);
  int16_t segment = ntohs(n_segment);

  int16_t n_segmentSize;
  memcpy(&n_segmentSize, pkt, sizeof(n_segmentSize));
  pkt += sizeof(n_segmentSize);
  int16_t segmentSize = ntohs(n_segmentSize);

  unsigned char rawData[segmentSize];
  memcpy(&rawData, pkt, segmentSize);

  //
  // Update download status
  //
  for (int i = 0; i < s_numDownloads; i++) {
    struct Download *d = &s_downloads[i];
    if (strncmp(d->filename, filename, strlen(d->filename)) == 0) {
      if(d->rawFileInit == false) {
        d->rawFile = malloc(d->filesize);
        d->rawFileInit = true;
        memset(d->rawFile, 0, d->filesize);
      }
      //memcpy((d->rawFile+(segment*SEGMENT_SIZE)), &rawData, segmentSize);
      memcpy((d->rawFile+(segment*SEGMENT_SIZE)), pkt, segmentSize);
      d->haveFileSegments[segment] = 1;
    }
  }

  //
  // Log received CLNT_SEG_REP message
  //
  uint16_t fromPort = ntohs(cliaddr.sin_port);
  int16_t sendingClientId = getClientId(fromPort);
  if (sendingClientId == -1) {
    // update client addrs database
    struct ClientAddr clientaddr;
    clientaddr.id = cid;
    clientaddr.port = fromPort;
    char *ip = inet_ntoa(cliaddr.sin_addr);
    memcpy(&clientaddr.ip, ip, MAX_IP);
    sc_clientAddrs[sc_numClientAddrs++] = clientaddr;
  }

  FILE *fp;
  char logfile[20];
  sprintf(logfile, "%02d.out", s_commInfo.clientid);
  fp = fopen(logfile, "a");
  if (fp == NULL) {
    perror("ERROR opening client log file for append");
    exit(1);
  }
  fprintf(fp, "%lu.%d\tFrom\t%d\tCLNT_SEG_REP\t%s\n", tv.tv_sec, tv.tv_usec, cid, filename);
  //printf("Client %d received CLNT_SEG_REP from client %d for filename %s\n", s_commInfo.clientid, cid, filename);
  fclose(fp);

  return -1;
}


int sendClientInfoRequests()
{
  // CLNT_INFO_REQ, // client asks other client for file info
  // pktsize, msgtype, filename

  if (s_numDownloads == 0)
    return -1;

  char logstr[(s_numDownloads*MAX_FILENAME) + 56];
  memset(&logstr, '\0', sizeof(logstr));

  for (int i = 0; i < s_numDownloads; i++) {
    struct Download *d = &s_downloads[i];
    if (d->filesize == 0)
      continue;

    // Figure out who is sharing this file
    int neighbors[MAX_CLIENTS];
    memset(&neighbors, 0, sizeof(neighbors));
    for (int i = 0; i < sc_numGroups; i++) {
      struct Group *group = &sc_groups[i];
      if (strncmp(group->filename, d->filename, strlen(d->filename)) == 0) {
        for (int j = 0; j < MAX_CLIENTS; j++) {
          if (group->sharingClients[j] == 1) {
            neighbors[j] = 1;
          }
        }
      }
    }
    
    // send the request to the other clients
    int16_t pktsize = (sizeof(int16_t) * 3) + MAX_FILENAME;
    int16_t n_pktsize = htons(pktsize);
    unsigned char *pkt = malloc((sizeof(int16_t) * 2) + MAX_FILENAME);
    memcpy(pkt, &n_pktsize, sizeof(int16_t));
    int16_t n_msgtype = htons(0); 
    memcpy(pkt+2, &n_msgtype, sizeof(int16_t));
    int16_t n_cid = htons(s_commInfo.clientid); 
    memcpy(pkt+4, &n_cid, sizeof(int16_t));
    memcpy(pkt+6, &d->filename, MAX_FILENAME);
  
    struct sockaddr_in si_other;
    socklen_t slen = sizeof(si_other);

    for (int j = 0; j < MAX_CLIENTS; j++) {
      if (neighbors[j] == 0 || j == s_commInfo.clientid) // do not send to myself!
        continue;

      ///*DEBUG*/printf("Client %d sending CLNT_INFO_REQ to client %d: pktsize %d, msgtype %d, filename %s\n", s_commInfo.clientid, j, pktsize, ntohs(n_msgtype), d->filename);

      memset((char *) &si_other, 0, sizeof(si_other));
      si_other.sin_family = AF_INET;
      si_other.sin_port = htons(getClientPort(j));
  
      int bytesSent = 0, totalBytesSent = 0;
      while (totalBytesSent < pktsize) {
        if ((bytesSent = sendto(s_commInfo.udpsock, pkt, pktsize, 0, (struct sockaddr*)&si_other, slen)) == -1) {
          perror("ERROR (sendClientRequest) sendto");
          exit(1);
        }
        totalBytesSent += bytesSent;
      }
      
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
      fprintf(fp, "%lu.%d\tTo\t%d\tCLNT_INFO_REQ\t%s\n", tv.tv_sec, tv.tv_usec, j, d->filename);
      fclose(fp);
    }
    free(pkt);
  }

  return 0;
}


int sendClientInfoReply(struct SendData *sd)
{
  struct sockaddr_in si_other;
  socklen_t slen = sizeof(si_other);
  memset((char *) &si_other, 0, sizeof(si_other));
  si_other.sin_family = AF_INET;
  si_other.sin_port = sd->cliaddr.sin_port;
  uint16_t fromPort = ntohs(sd->cliaddr.sin_port);
  int16_t cid = getClientId(fromPort);

  int bytesSent = 0, totalBytesSent = 0;
  while (totalBytesSent < sd->pktsize) {
    if ((bytesSent = sendto(s_commInfo.udpsock, sd->pkt, sd->pktsize, 0, (struct sockaddr*)&si_other, slen)) == -1) {
      perror("ERROR (sed CLNT_INFO_REP) sendto");
      exit(1);
    }
    totalBytesSent += bytesSent;
  }
  struct timeval ltv;
  gettimeofday(&ltv, NULL);

  char filename[MAX_FILENAME];
  memcpy(&filename, sd->pkt+6, MAX_FILENAME);

  //printf("Client %d sending CLNT_INFO_REP to client %d for filename %s\n", s_commInfo.clientid, cid, filename );

  int16_t n_numSegments;
  memcpy(&n_numSegments, sd->pkt+6+MAX_FILENAME, sizeof(n_numSegments));
  int16_t numSegments = ntohs(n_numSegments);

  FILE *fp;
  char logfile[20];
  sprintf(logfile, "%02d.out", s_commInfo.clientid);
  fp = fopen(logfile, "a");
  if (fp == NULL) {
    perror("ERROR opening client log file for append");
    exit(1);
  }
  fprintf(fp, "%lu.%d\tTo\t%d\tCLNT_INFO_REP\t%s ", ltv.tv_sec, ltv.tv_usec, cid, filename);
  for (int i = 0; i < numSegments; i++) {
    fprintf(fp, "%d ", i);
  }
  fprintf(fp, "\n");
  fclose(fp);

  free(sd->pkt);
  free(sd);

  return -1;
}


//
// Send tracker group interest
//
int sendInterestToTracker()
{
  //
  // Check our termination conditions
  // 
  if (sc_numGroupUpdates >= 2) {
    bool terminate = true;
    for (int i = 0; i < s_numDownloads; i++) {
      struct Download *d = &s_downloads[i];
      if (d->doneDownloading == false) {
        terminate = false;
        break;
      }
    }
    if (terminate == true) {
      /*DEBUG*/printf("Client %d exiting after %d uneventful group updates\n", s_commInfo.clientid, sc_numGroupUpdates);
      exit(0);
    }
  }
  if (sc_numDownloadProgress >= 4) {
    /*DEBUG*/printf("Client %d exiting after no download or sharing progress %d\n", s_commInfo.clientid, sc_numDownloadProgress);
    exit(0);
  }

  //
  // GROUP_SHOW_INTEREST (client -> tracker)
  //
  // MSG FORMAT = pktsize, msgtype, client id, number files, filename, file size, type, ...
  sc_numGroupUpdates++;
  sc_numDownloadProgress++;

  if (s_numDownloads == 0)
    return -1;

  // figure out who is enabled to send an update this round
  int numDownloadsThisRound = 0;
  for (int i = 0; i < s_numDownloads; i++) {
    struct Download *d = &s_downloads[i];
    if (d->enabled == true)
      numDownloadsThisRound++;
  }

  char logstr[(numDownloadsThisRound*MAX_FILENAME) + 56];
  memset(&logstr, '\0', sizeof(logstr));

  // Serialize message
  int16_t pktsize = ((sizeof(int16_t)*4) + ((MAX_FILENAME + sizeof(int16_t) + sizeof(uint32_t)) * numDownloadsThisRound));
  int16_t n_pktsize = htons(pktsize);

  int16_t n_msgtype = htons(0); // GROUP_SHOW_INTEREST

  int16_t n_cid = htons(s_commInfo.clientid);

  // downloads holds files that I will just be sharing too
  int16_t n_numfiles = htons(numDownloadsThisRound);

  unsigned char *pkt = malloc(pktsize);
  memset(pkt, 0, pktsize);
  unsigned char *pkt0 = pkt; // keep pointer to beginning so we can free later

  // fill up unsigned char for sending!
  memcpy(pkt, &n_pktsize, sizeof(int16_t));
  memcpy(pkt+2, &n_msgtype, sizeof(int16_t));
  memcpy(pkt+4, &n_cid, sizeof(int16_t));
  memcpy(pkt+6, &n_numfiles, sizeof(int16_t));
  pkt += 8;

  for (int i = 0; i < s_numDownloads; i++) {
    struct Download *d = &s_downloads[i];
    if (d->enabled == false)
      continue;

    bool haveFileAlready = false;

    // check if we know the file size
    for (int i = 0; i < s_numOwnedFiles; i++) {
      struct FileInfo *fi = &s_ownedFiles[i];
      if (strncmp(fi->name, d->filename, strlen(d->filename)) == 0) {
        haveFileAlready = true;
        d->filesize = fi->size;
        d->doneDownloading = true;
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

    uint32_t n_filesize = htons(d->filesize);
    memcpy(pkt, &n_filesize, sizeof(n_filesize));
    pkt += sizeof(uint32_t);

    memcpy(pkt, &n_type, sizeof(int16_t));
    pkt += 2;

    // for logging save off filename
    strcat(logstr, d->filename);
    strcat(logstr, " ");
  }

  ///*DEBUG*/dumpDeserializedMsg(pkt0);

  struct sockaddr_in si_other;
  socklen_t slen;
  slen = sizeof(si_other);
  memset((char *) &si_other, 0, sizeof(si_other));
  si_other.sin_family = AF_INET;
  si_other.sin_port = htons(s_commInfo.trackerport); // set tracker port we want to send to

  int bytesSent = 0, totalBytesSent = 0;
  while (totalBytesSent < pktsize) {
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

  return 0;
}



//
// Client worker
//
void clientDoWork(int clientid, int32_t managerport)
{
  //
  // Clear out static databases in case another process had already populated these
  //
  memset(s_downloads, 0, sizeof(s_downloads));
  s_numDownloads = 0;
  memset(s_ownedFiles, 0, sizeof(s_ownedFiles));
  s_numOwnedFiles = 0;
  memset(&s_commInfo, 0, sizeof(s_commInfo));
  memset(sc_clientAddrs, 0, sizeof(sc_clientAddrs));
  sc_numClientAddrs = 0;
  memset(sc_groups, 0, sizeof(sc_groups));
  sc_numGroups = 0;
  sc_numDownloadProgress = 0;
  sc_numGroupUpdates = 0;

  // Seed randomizer
  srand(time(NULL));

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
  unsigned char buffer[sizeof(struct Client)];
  unsigned char *p_buffer = (unsigned char *)&buffer;
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

  p_buffer = (unsigned char *)&buffer;
  client = (struct Client *)deserializeClient((struct Client *)p_buffer);
  s_commInfo.pktdelay = client->pktdelay;
  s_commInfo.pktprob = client->pktprob;
  s_commInfo.reqtimeout = client->reqtimeout;
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
  unsigned char portmsg[sizeof(int32_t)];
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


  ///*DEBUG*/dumpClientCommInfo();

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

    unsigned char *filecontents = (unsigned char*)malloc(filesize);
    memset(filecontents, 0, filesize);
    fread(filecontents, filesize, 1, fp);
    fclose(fp);

    ///*DEBUG*/ write out file immediately, are we even reading it properly?
    //char newfile[MAX_FILENAME];
    //sprintf(newfile, "%d-", s_commInfo.clientid);
    //strncat(newfile, filename, MAX_FILENAME);
    //printf("Client %d is writing downloaded file %s!\n", s_commInfo.clientid, newfile);

    //fp = fopen(newfile, "wb");
    //if (fp == NULL) {
    //  printf("ERROR opening %s for writing", newfile);
    //  exit(1);
    //}
    //fwrite(filecontents, sizeof(unsigned char), filesize, fp);
    //fclose(fp);
    //exit(0);

    struct FileInfo fileinfo;
    fileinfo.size = filesize;
    fileinfo.numsegments = (filesize / SEGMENT_SIZE) + 1;
    strncpy(fileinfo.name, filename, MAX_FILENAME);
    fileinfo.fp = filecontents;
    for (int j = 0; j < MAX_SEGMENTS; j++) {
      fileinfo.segmentSizes[j] = SEGMENT_SIZE;
    }

    // figure out last segment size and set it
    int16_t lastSize = abs(fileinfo.size - (SEGMENT_SIZE * fileinfo.numsegments));
    fileinfo.segmentSizes[fileinfo.numsegments-1] = lastSize;
    
    // add file to our owned files database
    memcpy(&s_ownedFiles[s_numOwnedFiles++], &fileinfo, sizeof(struct FileInfo));

    // create a group too!
    struct Group group;
    memcpy(&group.filename, &fileinfo.name, MAX_FILENAME);
    for (int j = 0; j < fileinfo.numsegments; j++) {
      group.downClients[j] = 0;
      group.sharingClients[j] = 0;
    }
    group.filesize = fileinfo.size;
    memcpy(&sc_groups[sc_numGroups++], &group, sizeof(struct Group));
  }
  ///*DEBUG*/dumpClientOwnedFiles();

  //
  // Populate datastructure with s_downloads to complete
  //
  for (int i = 0; i < client->numtasks; i++) {
    struct Task *t = (struct Task *)&client->tasks[i];
    struct Download d;
    memset(&d, 0, sizeof(struct Download));
    d.starttime = t->starttime;
    d.share = t->share;
    d.enabled = true; // TODO make this false
    d.filesize = 0;
    d.numFileSegments = 0;
    d.rawFileInit = false;
    snprintf(d.filename, MAX_FILENAME, "%s", t->file);
    for (int j = 0; j < MAX_SEGMENTS; j++) {
      d.haveFileSegments[j] = 0;
    }
    for (int j = 0; j < MAX_SEGMENTS; j++) {
      d.segmentToClient[j] = MAX_CLIENTS; // will never have a client id = MAX_CLIENTS
    }

    for (int j = 0; j < s_numOwnedFiles; j++) {
      struct FileInfo *fi = &s_ownedFiles[j];
      if (strncmp(fi->name, d.filename, strlen(d.filename)) == 0) {
        d.filesize = fi->size;
        d.numFileSegments = fi->numsegments;
        d.doneDownloading = true;
        for (int k = 0; k < d.numFileSegments; k++) {
          d.haveFileSegments[k] = 1;
        }
        break;
      }
    }

    memcpy(&s_downloads[s_numDownloads++], &d, sizeof(struct Download));

    for (int j = 0; j < sc_numGroups; j++) {
      struct Group *g = &sc_groups[j];
      if (strncmp(g->filename, d.filename, strlen(d.filename)) == 0) {
        g->sharingClients[s_commInfo.clientid] = d.share;
        break;
      }
    }

    //int (*fp)();
   	//fp = enableDownload;
    //Timers_AddTimer(d.starttime, fp, (long*)(s_numDownloads-1));
  }
  ///*DEBUG*/dumpClientDownloads();

  if (s_numDownloads == 0) {
    printf("Client %d exiting - no downloads or owned files to share\n", s_commInfo.clientid);
    exit(0);
  }

	struct timeval tmv;
	int status;
  fd_set active_fd_set, read_fd_set;
  FD_ZERO(&active_fd_set);
  FD_SET(udpsock, &active_fd_set);

  //
  // Group update to tracker
  //
  int (*funcp)();
  funcp = sendInterestToTracker; // TODO start a time from within here with the client specific delay
  Timers_AddTimer((s_commInfo.reqtimeout*1000), funcp, (int*)1);
  printf("Added sendInterestToTimer %d seconds from now\n", (s_commInfo.reqtimeout*1000));

  //sleep(s_commInfo.reqtimeout);

  funcp = sendClientInfoRequests;
  Timers_AddTimer(s_commInfo.pktdelay, funcp, (int*)1);
     
  funcp = sendClientSegmentRequests;
  Timers_AddTimer(s_commInfo.pktdelay, funcp, (int*)1);


  // I've reused the code from test-app-c.c exampled provided by the timers
  // library in the following while loop.
	while (1) {
		Timers_NextTimerTime(&tmv);
		if (tmv.tv_sec == 0 && tmv.tv_usec == 0) {
      /* The timer at the head on the queue has expired  */
      Timers_ExecuteNextTimer();
      continue;
		}

		tmv.tv_sec = 0; tmv.tv_usec = 10; // TODO figure out what to set timer to here
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
          unsigned char initBuff[512]; // make this large to make life easy
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
          unsigned char *pkt = malloc(pktsize);
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
            handleTrackerGroupUpdate(pkt, pktsize, tv);
            /*DEBUG*/dumpClientClientAddrs();
            /*DEBUG*/dumpClientGroups();
            /*DEBUG*/dumpClientOwnedFiles();
            /*DEBUG*/dumpClientDownloads();
            printf("\n\n");
            
          } else if (msgtype == 0) {
            // client -> client info req = 0
            handleClientInfoRequest(pkt, tv, udp_serv_addr);
          } else if (msgtype == 1) {
            // client -> client info rep = 1
            handleClientInfoReply(pkt, tv, udp_serv_addr);
          } else if (msgtype == 2) {
            // client -> client seg req = 2
            handleClientSegmentRequest(pkt, tv, udp_serv_addr);
          } else if (msgtype == 3) {
            // client -> client seg rep = 3
            handleClientSegmentReply(pkt, tv, udp_serv_addr);
          }
          free(pkt);
			  }
			}
		}
	}
}


void handleTrackerGroupUpdate(unsigned char *pktDontTouch, int16_t buffersize, struct timeval tv)
{
  // GROUP_ASSIGN, // tracker tells client about other clients
  // pktsize, msgtype, number files, filename, file size, num neighbors, neigh. id, neigh. ip, neigh port, ...

  unsigned char *pkt = pktDontTouch;
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

    uint32_t n_filesize;
    memcpy(&n_filesize, pkt, sizeof(n_filesize));
    pkt += sizeof(n_filesize);
    uint32_t filesize = ntohs(n_filesize);
    newGroup.filesize = filesize;

    // update download info with the filesize  
    for (int i = 0; i < s_numDownloads; i++) {
      struct Download *d = &s_downloads[i];
      if (strncmp(d->filename, newGroup.filename, strlen(d->filename)) == 0) {
        if (d->filesize == 0 && newGroup.filesize != 0) {
          d->filesize = newGroup.filesize;
          d->numFileSegments = (d->filesize / SEGMENT_SIZE) + 1;
          for (int j = 0; j < MAX_SEGMENTS; j++) {
            d->haveFileSegments[j] = 0;
          }
          d->rawFileInit = true;
          d->rawFile = malloc(d->filesize);
          memset(d->rawFile, 0, d->filesize);
        }
        break;
      }
    }

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
      if (haveClientAddr == false) {
        sc_clientAddrs[sc_numClientAddrs++] = clientaddr;
      }
    }

    //
    // update group knowledge (which clients are sharing
    //
    bool knowGroupAlready = false;
    for (int i = 0; i < sc_numGroups; i++) {
      struct Group *group = &sc_groups[i];
      if (strncmp(group->filename, newGroup.filename, strlen(group->filename)) == 0) {
        for (int j = 0; j < MAX_CLIENTS; j++) {
          if (newGroup.sharingClients[j] == 1) {
            group->sharingClients[j] = 1;
          }
        }
        if (group->filesize == 0 && newGroup.filesize != 0) {
          group->filesize = newGroup.filesize;
        }
        knowGroupAlready = true;
        break;
      }
    }
    if (knowGroupAlready == false) {
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
  fprintf(fp, "\n");
  fclose(fp);
}


//
// Serialize client to be sent over the network
//
struct Client* serializeClient(struct Client *client)
{
  struct Client *s = malloc(sizeof(struct Client));
  memset(s, 0, sizeof(struct Client));
  s->id = htonl(client->id);
  s->pktdelay = htonl(client->pktdelay);
  s->pktprob = htonl(client->pktprob);
  s->reqtimeout = htonl(client->reqtimeout);
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
  memset(s, 0, sizeof(struct Client));
  s->id = ntohl(client->id);
  s->pktdelay = ntohl(client->pktdelay);
  s->pktprob = ntohl(client->pktprob);
  s->reqtimeout = ntohl(client->reqtimeout);
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


