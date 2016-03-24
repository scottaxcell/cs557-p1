#ifndef MANAGER_H
#define MANAGER_H

#include <stdio.h>   // printf
#include <stdlib.h>  // sscanf
#include <ctype.h>   // isdigit
#include <stdbool.h> // bool

#include "Client.h"

// The manager creates the tracker and client by reading the manager.conf

struct Manager
{
  const char *conf;
  int numclients;
  int reqtimeout; // request timeout
  struct Client *clients;
};

struct Manager* ManagerNew()
{
  struct Manager *mgr = malloc(sizeof(struct Manager));
  mgr->conf = "manager.conf";
  mgr->numclients = 0;
  mgr->reqtimeout = 0;
  mgr->clients = NULL;
  return mgr;
}

struct Manager* readMgrCfg()
{
  struct Manager* mgr = ManagerNew();
  bool readNumClients = false;
  bool readReqTimeout = false;
  bool readPktDelays = false;
  bool readWhoHasFiles = false;
  bool readDownloadTasks = false;

  FILE *fp;
  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;

  fp = fopen(mgr->conf, "r");
  if (fp == NULL) {
    perror("ERROR opening manager.conf");
    exit(1);
  }

  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    ///*DEBUG*/printf("Read line of length %zu :\n", linelen);
    ///*DEBUG*/printf("Read line of lencap %zu :\n", linecap);
    ///*DEBUG*/printf("%s", line);

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
      ///*DEBUG*/printf("READ number of clients %d\n", numclients);
      
      struct Client *clients = malloc(numclients * sizeof(struct Client));
      mgr->numclients = numclients;
      mgr->clients = clients;
      readNumClients = true;
      continue;
    }

    // second number read will be the request timeout
    if (!readReqTimeout) {
      int req;
      sscanf(line, "%d", &req);
      ///*DEBUG*/printf("READ request timeout %d\n", req);
      mgr->reqtimeout = req;
      readReqTimeout = true;
      continue;
    }

    // next lines will be client packet delays and drop probability
    if (!readPktDelays) {
      int cid;
      int delay;
      int prob;
      sscanf(line, "%d %d %d", &cid, &delay, &prob);
      ///*DEBUG*/printf("%d %d %d\n", cid, delay, prob);

      if (cid == -1) {
        readPktDelays = true;
        continue;
      }

      struct Client client;
      client.id = cid;
      client.pktdelay = delay;
      client.pktprob = prob;
      client.numfiles = 0;
      client.numtasks = 0;
      mgr->clients[cid] = client;
      continue;
    }

    // next lines will be client starting files
    if (!readWhoHasFiles) {
      int cid;
      char file[MAX_FILENAME];
      sscanf(line, "%d %s", &cid, file);
      ///*DEBUG*/printf("read \"%d %s\"\n", cid, file);

      if (cid == -1) {
        readWhoHasFiles = true;
        continue;
      }

      struct Client *client = &(mgr->clients[cid]);
      snprintf(client->files[client->numfiles], MAX_FILENAME, "%s", file);
      client->numfiles++;
      continue;
    }

    // next lines will be client download tasks
    if (!readDownloadTasks) {
      int cid;
      char file[MAX_FILENAME];
      int starttime;
      int share;
      sscanf(line, "%d %s %d %d", &cid, file, &starttime, &share);
      ///*DEBUG*/printf("read \"%d %s %d %d\"\n", cid, file, starttime, share);

      if (cid == -1) {
        readDownloadTasks = true;
        continue;
      }

      struct Client *client = &(mgr->clients[cid]);
      struct Task task;
      memset(&task, 0, sizeof(struct Task));
      snprintf(task.file, MAX_FILENAME, "%s", file);
      task.starttime = starttime;
      task.share = share;
      memcpy(&client->tasks[client->numtasks], &task, sizeof(task));
      client->numtasks++;
      continue;
    }

    // TODO read in rest of configurations
    if (readDownloadTasks)
      return mgr;
  }
  
  return mgr;
}

#endif
