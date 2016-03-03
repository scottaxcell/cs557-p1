#include <stdio.h>
#include <stdlib.h> // exit()
#include <memory>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <string.h> // memset, bzero
#include <iostream>

#include "Manager.h"

// socket sources
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
// end

void sendUDPPortToManager(int managerport, int trackerport, std::string &ipaddr)
{
	int sockfd;
	struct addrinfo hints, *servinfo;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	if (getaddrinfo(ipaddr.c_str(), std::to_string(managerport).c_str(), &hints, &servinfo) != 0) {
		perror("ERROR on tracker getaddrinfo");
		exit(1);
	}

	if ((sockfd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol)) < 0) {
		perror("ERROR on tracker tcp socket");
		exit(1);
	}

	if (connect(sockfd, servinfo->ai_addr, servinfo->ai_addrlen) < 0) {
		perror("ERROR on tracker tcp connect");
		exit(1);
	}

	printf("Connected!\n");

  std::string msg("T_UDP_PORT:"+std::to_string(trackerport));
  int bytes_sent = -1, len = strlen(msg.c_str());
  while (bytes_sent != len) {
    bytes_sent = send(sockfd, msg.c_str(), len, 0);
  }

}

void setupManagerTCPComms(int &tcpsock, int &managerport, std::string &ipaddr)
{
  struct sockaddr_in serv_addr;

  /* First call to socket() function */
  tcpsock = socket(AF_INET, SOCK_STREAM, 0);
  
  if (tcpsock < 0) {
     perror("ERROR opening socket");
     exit(1);
  }
  
  /* Initialize socket structure */
  bzero((char *) &serv_addr, sizeof(serv_addr));
  
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = INADDR_ANY;
  serv_addr.sin_port = htons(0);
  
  /* Now bind the host address using bind() call.*/
  if (bind(tcpsock, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
     perror("ERROR on binding");
     exit(1);
  }
     
  /* Get the port number for communicating with spawned processes */
  socklen_t serv_addr_size = sizeof(serv_addr);
  if (getsockname(tcpsock, (struct sockaddr *) &serv_addr, &serv_addr_size) == -1) {
     perror("ERROR on getsockname");
     exit(1);
  }
  managerport = ntohs(serv_addr.sin_port);
  ipaddr = inet_ntoa(serv_addr.sin_addr);

  printf("Manager IP address is: %s\n", ipaddr.c_str());
  printf("Manager TCP port is: %d\n", managerport);
}

void setupTrackerUDPComms(int &udpsock, int &trackerport)
{
  struct sockaddr_in udp_serv_addr;

  // create UDP socket
  udpsock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (udpsock < 0) {
    perror("ERROR on tracker UDP socket");
    exit(1);
  }

  // zero out the structure
  memset((char *) &udp_serv_addr, 0, sizeof(udp_serv_addr));

  udp_serv_addr.sin_family = AF_INET;
  udp_serv_addr.sin_addr.s_addr = INADDR_ANY;
  udp_serv_addr.sin_port = htons(0); // chooses a dynamic port

  /* Now bind the host address using bind() call.*/
  if (bind(udpsock, (struct sockaddr *) &udp_serv_addr, sizeof(udp_serv_addr)) < 0) {
     perror("ERROR on tracker binding UDP port");
     exit(1);
  }
     
  /* Get the port number for communicating with spawned processes */
  socklen_t udp_serv_addr_size = sizeof(udp_serv_addr);
  if (getsockname(udpsock, (struct sockaddr *) &udp_serv_addr, &udp_serv_addr_size) == -1) {
     perror("ERROR on tracker getsockname");
     exit(1);
  }
  trackerport = ntohs(udp_serv_addr.sin_port);
  printf("Tracker UDP IP address is: %s\n", inet_ntoa(udp_serv_addr.sin_addr));
  printf("Tracker UDP port is: %d\n", trackerport);
}

void trackerDoWork(int &udpsock)
{

}

void clientDoWork(int clientid, int &managerport)
{
	int sockfd;
	struct addrinfo hints, *servinfo;
  std::string ipaddr; // TODO not sure I need this

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	if (getaddrinfo(ipaddr.c_str(), std::to_string(managerport).c_str(), &hints, &servinfo) != 0) {
		perror("ERROR on tracker getaddrinfo");
		exit(1);
	}

	if ((sockfd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol)) < 0) {
		perror("ERROR on tracker tcp socket");
		exit(1);
	}

	if (connect(sockfd, servinfo->ai_addr, servinfo->ai_addrlen) < 0) {
		perror("ERROR on tracker tcp connect");
		exit(1);
	}

	printf("Connected!\n");

  std::string msg("CLIENT_ID:"+std::to_string(clientid));
  int bytes_sent = -1, len = strlen(msg.c_str());
  while (bytes_sent != len) {
    bytes_sent = send(sockfd, msg.c_str(), len, 0);
  }

}

int main( int argc, const char* argv[] )
{
  // Prints each argument on the command line.
  for( int i = 0; i < argc; i++ )
  {
    printf( "arg %d: %s\n", i, argv[i] );
  }

  //
  // Read the configuration file
  //
  std::string managerCfg("manager.conf");
  auto manager = std::unique_ptr<Manager>(new Manager(managerCfg));
  manager->readConf();

  //
  // Setup TCP listener for tracker and clients to communicate with
  //
  int tcpsock = 0;
  int newtcpsock = 0;
  int managerport = 0;
  std::string ipaddr;
  setupManagerTCPComms(tcpsock, managerport, ipaddr);

  //
  // Spawn tracker and clients
  //
  auto pid = fork();
  if (pid == 0) {
    //=========================================================================
    // TRACKER (child process)
    //=========================================================================
    // create tracker
    // starts listening on for UDP connections
    // reports UDP port number to manager
    // listen for clients - give them what they want
    /*DEBUG*/std::cout << "DBG created child process for tracker pid = " << getpid() << std::endl;

    //
    // setup UDP socket for comms
    //
    int udpsock = 0;
    int trackerport = 0;
    setupTrackerUDPComms(udpsock, trackerport);
    /*DEBUG*/std::cout << "DBG tracker UDP port = " << trackerport << std::endl;
    /*DEBUG*/std::cout << "DBG tracker socket = " << udpsock << std::endl;
    
    //
    // send UDP port to manager via TCP
    //
    sendUDPPortToManager(managerport, trackerport, ipaddr);

    //
    // list for clients and give them what they want
    //
    trackerDoWork(udpsock);

    /*DEBUG*/std::cout << "DBG exiting child process for tracker pid = " << getpid() << std::endl;
    exit(0);
  } else if (pid > 0) {
    //=========================================================================
    // MANAGER (parent process)
    //=========================================================================
  
    //
    // listen for tracker letting us know her UDP
    //
    char buffer[256];
    struct sockaddr_in cli_addr;
    
    listen(tcpsock,5);

	  socklen_t clilen;
	  clilen = sizeof(cli_addr);
	  newtcpsock = accept(tcpsock, (struct sockaddr *)&cli_addr, &clilen);

    if (newtcpsock < 0) {
      perror("ERROR on manager accept");
      exit(1);
    }
    bzero(buffer, 256);
    int recv_bytes = recv(newtcpsock, buffer, sizeof(buffer), 0); // is blocking
    // TODO error cheking recv_bytes
    /*DEBUG*/std::cout << "DBG manager TCP received a message.." << std::endl;
    // TODO add check to make sure we're getting T_UDP_PORT:<portnum>
    printf("\"");
    for (int i = 0; i <= recv_bytes; i++) {
      printf("%c", buffer[i]);
    }
    printf("\"\n");
    std::string tmp(buffer);
    std::string tport(tmp.substr(11));
    std::cout << "TRACKER UDP PORT (str) = " << tport << std::endl;
    int trackerport = std::stoi(tport);
    std::cout << "TRACKER UDP PORT (int) = " << trackerport << std::endl;
    
    //
    // spawn clients
    //
    int clientid = 0;
    for (auto &client : manager->m_clients) {
      clientid = client->m_id;
      auto clientpid = fork();
      if (clientpid == 0) {
        //=========================================================================
        // CLIENT (child process)
        //=========================================================================
        /*DEBUG*/std::cout << "DBG created child process for client " << clientid << " pid = " << getpid() << std::endl;
        clientDoWork(clientid, managerport);
        sleep(3);
        /*DEBUG*/std::cout << "DBG exiting child process for client " << clientid << " pid = " << getpid() << std::endl;
        exit(0);
      } else if (clientpid == -1) {
        // client fork failed
        perror("ERROR on client fork");
        exit(1); 
      }
      //
      // listen for newly created client and provide her with configuration data
      //
      char buffer[256];
      struct sockaddr_in cli_addr;
      
      listen(tcpsock,5);

	    socklen_t clilen;
	    clilen = sizeof(cli_addr);
	    newtcpsock = accept(tcpsock, (struct sockaddr *)&cli_addr, &clilen);

      if (newtcpsock < 0) {
        perror("ERROR on manager accept");
        exit(1);
      }

      bzero(buffer, sizeof(buffer));
      int recv_bytes = recv(newtcpsock, buffer, sizeof(buffer), 0);
      if (recv_bytes == 0) {
        printf("Manager TCP socket closed.\n");
      } else if (recv_bytes == -1) {
        perror("ERROR manager recv failed");
        exit(1);
      } else {
        /*DEBUG*/std::cout << "DBG manager TCP received a message.." << std::endl;
        printf("\"");
        for (int i = 0; i <= recv_bytes; i++) {
          printf("%c", buffer[i]);
        }
        printf("\"\n");
      }
    }

    //
    // Wait for all children processes to exit
    //
    while (true) {
      int status;
      pid_t done = wait(&status);
      if (done == -1) {
        if (errno == ECHILD)
          break; // no more child processes
      } else {
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
          std::cerr << "pid " << done << " failed" << std::endl;
          exit(1);
        }
      }
    }
    /*DEBUG*/std::cout << "DBG exiting manager process " << getpid() << std::endl;
  } else {
    // fork failed
    perror("ERROR on fork");
    exit(1); 
  }
}
