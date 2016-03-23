#ifndef TRACKER_H
#define TRACKER_H
#define MAX_FILENAME 33
#define MAX_IP 16
#define MAX_CLIENTS 25
#define MAX_FILES 25

// The tracker is supposed to just keep track of all nodes currently in the
// network that are interested in sharing a particular file

void trackerDoWork(int udpsock, int32_t trackerport);

#endif
