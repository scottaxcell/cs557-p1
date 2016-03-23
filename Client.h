#ifndef CLIENT_H
#define CLIENT_H

#include <sys/types.h>

#define MAX_FILENAME 33
#define MAX_FILES 25
#define MAX_CLIENTS 25
#define SEGMENT_SIZE 32

struct CommInfo {
  int32_t trackerport;
  int32_t myport;
  int clientid;
  int udpsock;
  int pktdelay;
  int pktprob;
};
  
struct Download {
  int starttime;
  int share;
  bool enabled;
  char filename[MAX_FILENAME];
  int filesize;
  int numFileSegments;
  u_char *rawFile;
};


struct FileInfo
{
  int numsegments;
  long size;
  char name[MAX_FILENAME];
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
  int numfiles; // files the client owns 
  int numtasks;
  char files[MAX_FILES][MAX_FILENAME];
  struct Task tasks[MAX_FILES][sizeof(struct Task)];
};

void clientDoWork(int clientid, int32_t managerport);
struct Client* serializeClient(struct Client *client);
struct Client* deserializeClient(struct Client *client);

#endif
