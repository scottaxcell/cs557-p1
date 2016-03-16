#ifndef TRACKER_H
#define TRACKER_H

// The tracker is supposed to just keep track of all nodes currently in the
// network that are interested in sharing a particular file

struct Tracker
{
 
};

void trackerDoWork(int udpsock, int32_t trackerport);

#endif
