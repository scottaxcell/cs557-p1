#include <string>
#include <vector>
#include <memory>

#include "Client.h"

// The manager creates the tracker and client by reading the manager.conf

struct Manager
{
//private:
  std::string m_conf;
  int m_numclients;
  int m_reqtimeout;
  std::string m_spawnme;
  std::vector<std::unique_ptr<Client>> m_clients;

  // hidden
  Manager() {}

//public:
  Manager(std::string& conf) : m_conf(conf), m_numclients(0), m_reqtimeout(0),
    m_spawnme("T") {}
  ~Manager() {}

  void readConf();
  void fireOffProcesses();
};

