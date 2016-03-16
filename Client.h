#define MAX_FILENAME 32
#define MAX_FILES 25
#define MAX_CLIENTS 25

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

struct Client* serializeClient(struct Client *client)
{
  struct Client *s = malloc(sizeof(struct Client));
  s->m_id = htonl(client->m_id);
  s->m_pktdelay = htonl(client->m_pktdelay);
  s->m_pktprob = htonl(client->m_pktprob);
  s->m_numfiles = htonl(client->m_numfiles);
  s->m_numtasks = htonl(client->m_numtasks);
  for (int i = 0; i < client->m_numfiles; i++) {
    memcpy(s->m_files[i], &client->m_files[i], MAX_FILENAME);
  }
  for (int i = 0; i < client->m_numtasks; i++) {
    struct Task *task = (struct Task*)&(client->m_tasks[i]);
    struct Task *st = (struct Task*)&(s->m_tasks[i]);
    st->m_starttime = htonl(task->m_starttime);
    st->m_share = htonl(task->m_share);
    memcpy(st->m_file, &task->m_file, MAX_FILENAME);
  }

  return s;
}

struct Client* deserializeClient(struct Client *client)
{
  struct Client *s = malloc(sizeof(struct Client));
  s->m_id = ntohl(client->m_id);
  s->m_pktdelay = ntohl(client->m_pktdelay);
  s->m_pktprob = ntohl(client->m_pktprob);
  s->m_numfiles = ntohl(client->m_numfiles);
  s->m_numtasks = ntohl(client->m_numtasks);
  for (int i = 0; i < s->m_numfiles; i++) {
    memcpy(s->m_files[i], &client->m_files[i], MAX_FILENAME);
  }
  for (int i = 0; i < s->m_numtasks; i++) {
    struct Task *task = (struct Task*)&(client->m_tasks[i]);
    struct Task *st = (struct Task*)&(s->m_tasks[i]);
    st->m_starttime = htonl(task->m_starttime);
    st->m_share = htonl(task->m_share);
    memcpy(st->m_file, &task->m_file, MAX_FILENAME);
  }

  return s;
}



