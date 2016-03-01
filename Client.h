
class Client
{
private:
  int m_id;
  int m_packetdelay; // msec
  int m_packetdropprob; // packet drop probabilty
  std::string m_report; // log file

  // hidden
  Client() {}

public:
  Client(int &id, int &packetdelay, int &packetdrop) : m_id(id),
    m_packetdelay(packetdelay), m_packetdropprob(packetdrop)
  {
    m_report = std::to_string(id) + ".out";
  }

  ~Client() {}

};
