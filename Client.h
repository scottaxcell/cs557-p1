#define MAX_FILENAME 33
#define MAX_FILES 25

struct Task
{
  char m_file[MAX_FILENAME];
  int m_starttime;
  int m_share;
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

struct ClientToTrackerMsg
{
  
};
