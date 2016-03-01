#include "Manager.h"
#include <iostream>
#include <fstream>
#include <sstream>

void Manager::readConf()
{
  std::ifstream infile(m_conf);

  std::string line;
  while (std::getline(infile, line)) {
    std::istringstream iss(line);
    /*DEBUG*/std::cerr << iss.str();

    // first number found is the number of clients


  }
};
