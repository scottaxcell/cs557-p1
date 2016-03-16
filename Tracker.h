#ifndef TRACKER_H
#define TRACKER_H
#define MAX_FILENAME 33
#define MAX_IP 16
#define MAX_CLIENTS 25
#define MAX_FILES 5

// The tracker is supposed to just keep track of all nodes currently in the
// network that are interested in sharing a particular file

//
// Address of a client who has contacted the tracker
//
struct ClientAddr
{
  int id;
  int port;
  char ip[MAX_IP];
};
  
//
// File being tracked by the tracker
//
struct FileTracker
{
  char filename[MAX_FILENAME];
  int sharingClients[MAX_CLIENTS];
  int numSharingClients;
  int downClients[MAX_CLIENTS];
  int numDownClients;
};


struct Tracker
{
 
};

void trackerDoWork(int udpsock, int32_t trackerport);

#endif
