#ifndef UTILS_H
#define UTILS_H

#include <arpa/inet.h>

#define MAX_FILENAME 33
#define MAX_IP 16
#define MAX_CLIENTS 25
#define MAX_FILES 25

// The tracker is supposed to just keep track of all nodes currently in the
// network that are interested in sharing a particular file

//
// Address of a client who has contacted the tracker
//
struct ClientAddr
{
  uint16_t id;
  uint16_t port;
  char ip[MAX_IP];
};
  
//
// Group information for a file being tracked by the tracker
//
struct Group
{
  char filename[MAX_FILENAME];
  int sharingClients[MAX_CLIENTS];
  int downClients[MAX_CLIENTS];
  uint32_t filesize;
};

#endif
