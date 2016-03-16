#ifndef CLIENT_H
#define CLIENT_H

#define MAX_FILENAME 33
#define MAX_FILES 5
#define MAX_CLIENTS 25

enum MSGTYPE {
  GROUP_SHOW_INTEREST, // client tells tracker about itself
  GROUP_ASSIGN, // tracker tells client about other clients
  CLNT_INFO_REQ, // client asks other client for file info
  CLNT_INFO_REP, // client tells other client about file segments it has
  CLNT_SEG_REQ, // client asks other client for a file segment
  CLNT_SEG_REP // client sends other client file segment

};





struct Task
{
  int m_starttime;
  int m_share;
  char m_file[MAX_FILENAME];
};

struct Client
{
  int m_id;
  int m_pktdelay; // delay (msec)
  int m_pktprob;  // drop probabilty
  int m_numfiles;
  int m_numtasks;
  char m_files[MAX_FILES][MAX_FILENAME];
  struct Task m_tasks[MAX_FILES][sizeof(struct Task)];
};

void clientDoWork(int clientid, int32_t managerport);
struct Client* serializeClient(struct Client *client);
struct Client* deserializeClient(struct Client *client);

#endif
