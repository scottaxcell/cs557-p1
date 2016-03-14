#include <stdio.h>   // printf
#include <stdlib.h>  // sscanf
#include <ctype.h>   // isdigit
#include <stdbool.h> // bool

#include "Client.h"

// The manager creates the tracker and client by reading the manager.conf

struct Manager
{
  const char *m_conf;
  int m_numclients;
  int m_reqtimeout; // request timeout
  struct Client *m_clients;
};

struct Manager* ManagerNew()
{
  struct Manager *mgr = malloc(sizeof(struct Manager));
  mgr->m_conf = "manager.conf";
  mgr->m_numclients = 0;
  mgr->m_reqtimeout = 0;
  mgr->m_clients = NULL;
  return mgr;
}

struct Manager* readMgrCfg()
{
  struct Manager* mgr = ManagerNew();
  bool readNumClients = false;
  bool readReqTimeout = false;
  bool readPktDelays = false;

  FILE *fp;
  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;

  fp = fopen(mgr->m_conf, "r");
  if (fp == NULL) {
    perror("ERROR opening manager.conf");
    exit(1);
  }

  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    //printf("Read line of length %zu :\n", linelen);
    //printf("Read line of lencap %zu :\n", linecap);
    //printf("%s", line);

    // skip commented lines
    if (line[0] == '#')
      continue;
    // skip blank lines
    if (linelen == 1)
      continue;

    // first number read will be the number of clients
    if (!readNumClients) {
      int numclients;
      sscanf(line, "%d", &numclients);
      //printf("READ number of clients %d\n", numclients);
      
      struct Client *clients = malloc(numclients * sizeof(struct Client));
      mgr->m_numclients = numclients;
      mgr->m_clients = clients;
      readNumClients = true;
      continue;
    }

    // second number read will be the request timeout
    if (!readReqTimeout) {
      int req;
      sscanf(line, "%d", &req);
      //printf("READ request timeout %d\n", req);
      mgr->m_reqtimeout = req;
      readReqTimeout = true;
      continue;
    }

    // next lines will be client packet delays and drop probability
    if (!readPktDelays) {
      int cid;
      int delay;
      int prob;
      sscanf(line, "%d %d %d", &cid, &delay, &prob);
      //printf("%d %d %d\n", cid, delay, prob);

      if (cid == -1) {
        readPktDelays = true;
        continue;
      }

      struct Client client;
      client.m_id = cid;
      client.m_pktdelay = delay;
      client.m_pktprob = prob;
      mgr->m_clients[cid] = client;
    }
    // TODO read in rest of configurations
    if (readPktDelays)
      return mgr;
  }
  
  return mgr;
}

