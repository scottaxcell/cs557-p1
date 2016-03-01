#include <stdio.h>
#include <memory>

#include "Manager.h"

int main( int argc, const char* argv[] )
{
  // Prints each argument on the command line.
  for( int i = 0; i < argc; i++ )
  {
    printf( "arg %d: %s\n", i, argv[i] );
  }

  std::string managerCfg("manager.conf");
  auto manager = std::unique_ptr<Manager>(new Manager(managerCfg));
  manager->readConf();
}
