#include <string>

// The manager creates the tracker and client by reading the manager.conf

class Manager
{
private:
  std::string m_conf;

  // hidden
  Manager() {}

public:
  Manager(std::string& conf) : m_conf(conf) {}
  ~Manager() {}

  void readConf();
};

