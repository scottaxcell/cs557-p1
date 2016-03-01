#include "Manager.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <regex>
#include <thread>
#include <mutex>

void Manager::readConf()
{
  auto readingNodes(false);
  std::ifstream infile(m_conf);

  std::string line;
  while (std::getline(infile, line)) {
    std::istringstream iss(line);
    ///*DEBUG*/std::cout << iss.str() << std::endl;

    // ignore commented lines
    if (line[0] == '#')
      continue;

    // first number found is the number of clients
    if (m_numclients == 0) {
      //10
      std::regex base_regex("([0-9]+)");
      std::smatch base_match;
      if (std::regex_match(line, base_match, base_regex)) {
        if (base_match.size() == 2) {
          std::ssub_match base_sub_match = base_match[1];
          std::string base = base_sub_match.str();
          m_numclients = std::stoi(base);
          /*DEBUG*/std::cout << "DBG readConf() m_numclients = " << m_numclients << std::endl;
          continue;
        }
      }
    }

    // request timeout
    if (m_reqtimeout == 0) {
      //5
      std::regex base_regex("([0-9]+)");
      std::smatch base_match;
      if (std::regex_match(line, base_match, base_regex)) {
        if (base_match.size() == 2) {
          std::ssub_match base_sub_match = base_match[1];
          std::string base = base_sub_match.str();
          m_reqtimeout = std::stoi(base);
          /*DEBUG*/std::cout << "DBG readConf() m_reqtimeout = " << m_reqtimeout << std::endl;
          // get ready to read in nodes next
          readingNodes = true;
          continue;
        }
      }
    }

    if (readingNodes) {
      //0           10                      0
      std::regex base_regex("([0-9]+)\\s+([0-9]+)\\s+([0-9]+)");
      std::smatch base_match;
      if (std::regex_match(line, base_match, base_regex)) {
        if (base_match.size() == 4) {
          std::ssub_match base_sub_match = base_match[1];
          std::string base = base_sub_match.str();
          int nodeId = std::stoi(base);
          base_sub_match = base_match[2];
          base = base_sub_match.str();
          int packetDelay = std::stoi(base);
          base_sub_match = base_match[3];
          base = base_sub_match.str();
          int packetDelayProb = std::stoi(base);
          /*DEBUG*/std::cout << "DBG readConf() nodeId = " << nodeId
          /*DEBUG*/<< " packetDelay = " << packetDelay
          /*DEBUG*/<< " packetDelayProb = " << packetDelayProb << std::endl;
          if (nodeId == -1) {
            readingNodes = false;
            continue;
          }
          // create a client and add it to manager
          m_clients.emplace_back(std::shared_ptr<Client>(new Client(nodeId, packetDelay, packetDelayProb)));
          continue;
        }
      }
    }

    // TODO next phases, read in file and download information for clients
  }
}

namespace {
std::mutex mutex;
void clientWork()
{
  std::lock_guard<std::mutex> lock(mutex);
  std::this_thread::sleep_for(std::chrono::microseconds(100));
  /*DEBUG*/std::cout << "Client " << "fired on thread " << std::this_thread::get_id() << std::endl;
}
} // namespace

void Manager::fireOffProcesses()
{
  // TODO spawn tracker first

  // spawn clients second
  std::vector<std::thread> threads;
  for (auto &client : m_clients) {
    std::cout << "firing thread for client " << client->m_id << std::endl;
    threads.emplace_back(std::thread(clientWork));
  }

  for (auto &thread : threads) {
    thread.join();
  }
}
