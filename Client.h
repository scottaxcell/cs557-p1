#ifndef CLIENT_H
#define CLIENT_H

#include <sys/types.h>
#include <arpa/inet.h>

#define MAX_FILENAME 33
#define MAX_FILES 25
#define MAX_CLIENTS 25
#define SEGMENT_SIZE 32
#define MAX_SEGMENTS 10240


// Packet and meta data for sending a packet
struct SendData {
  struct sockaddr_in cliaddr;
  int16_t pktsize;
  u_char *pkt;
};


struct CommInfo {
  int32_t trackerport;
  int32_t myport;
  int clientid;
  int udpsock;
  int pktdelay;
  int pktprob;
  int reqtimeout;
};
  

struct Download {
  int starttime;
  int share;
  int numFileSegments;
  uint16_t filesize;
  bool enabled;
  bool doneDownloading;
  bool rawFileInit;
  u_char *rawFile;
  char filename[MAX_FILENAME];
  int haveFileSegments[MAX_SEGMENTS]; // for segment i tells us if we have it already or not
  int segmentToClient[MAX_SEGMENTS]; // for a give segment i tells us the client to ask for it
};


struct FileInfo
{
  int numsegments;
  uint16_t size;
  char name[MAX_FILENAME];
  int segmentSizes[MAX_SEGMENTS]; // for segment i tells us the the size of the segment, should be MAX_SEGMENTS except for last segment of a file
  unsigned char *fp;
};



struct Task
{
  int starttime;
  int share;
  char file[MAX_FILENAME];
};


struct Client
{
  int id;
  int pktdelay; // delay (msec) - amount of time to wait before replying to another client with a segment
  int pktprob;  // drop probabilty
  int reqtimeout;  // drop probabilty
  int numfiles; // files the client owns 
  int numtasks;
  char files[MAX_FILES][MAX_FILENAME];
  struct Task tasks[MAX_FILES];
};


void clientDoWork(int clientid, int32_t managerport);
struct Client* serializeClient(struct Client *client);
struct Client* deserializeClient(struct Client *client);
void dumpSegReqMsg(u_char *pktDontTouch);
void dumpSegRepMsg(u_char *pktDontTouch);


// GROUP_SHOW_INTEREST, // client tells tracker about itself
//   pktsize, msgtype, client id, number files, filename, file size, type, ...
int sendInterestToTracker();

// GROUP_ASSIGN, // tracker told client about other client meta data
//   pktsize, msgtype, number files, filename, file size, num neighbors, neigh. id, neigh. ip, neigh port, ...
void handleTrackerGroupUpdate(u_char *buffer, int16_t buffersize, struct timeval tv);

// CLNT_INFO_REQ, // client asks other client for file info
//   pktsize, msgtype, client id, filename
int sendClientInfoRequests();
int handleClientInfoRequest(u_char *pktDontTouch, struct timeval tv, struct sockaddr_in cliaddr);

// CLNT_INFO_REP, // client tells other client about file segments it has
//   pktsize, msgtype, client id, filename, num segments, segment number, ...
int sendClientInfoReply(struct SendData *sd);
int handleClientInfoReply(u_char *pktDontTouch, struct timeval tv, struct sockaddr_in cliaddr);

// CLNT_SEG_REQ, // client asks other client for a file segment
//   pktsize, msgtype, client id, filename, segment number
int sendClientSegmentRequests();
int sendClientSegmentRequest(struct SendData *sd);
int handleClientSegmentRequest(u_char *pktDontTouch, struct timeval tv, struct sockaddr_in cliaddr);

// CLNT_SEG_REP // client sends other client file segment
//   pktsize, msgtype, client id, filename, segment, size of segment, raw file data
int sendClientSegmentReply(struct SendData *sd);
int handleClientSegmentReply(u_char *pktDontTouch, struct timeval tv, struct sockaddr_in cliaddr);

#endif
