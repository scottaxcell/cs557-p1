#include <stdio.h>
#include <stdlib.h> // exit()
#include <memory>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <iostream>

#include "Manager.h"

// socket sources
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
// end

void sendUDPPortToManager(int tcpport, int udpport, std::string &ipaddr)
{
	int sockfd;
	struct addrinfo hints, *servinfo;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	if (getaddrinfo(ipaddr.c_str(), std::to_string(tcpport).c_str(), &hints, &servinfo) != 0) {
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

	//inet_ntop(servinfo->ai_family, get_in_addr((struct sockaddr *)servinfo->ai_addr), server_ip, sizeof(server_ip));
	printf("Connected!\n");

  const char* msg = "Hello Manager, I'm the tracker!";
  int bytes_sent = -1, len = strlen(msg);
  while (bytes_sent != len) {
    bytes_sent = send(sockfd, msg, len, 0);
  }

}

void setupManagerTCPComms(int &tcpsock, int &tcpport, std::string &ipaddr)
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
  tcpport = ntohs(serv_addr.sin_port);
  ipaddr = inet_ntoa(serv_addr.sin_addr);

  printf("Manager IP address is: %s\n", ipaddr.c_str());
  printf("Manager TCP port is: %d\n", tcpport);
}

void setupTrackerUDPComms(int &udpsock, int &udpport)
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
  udpport = ntohs(udp_serv_addr.sin_port);
  printf("Tracker UDP IP address is: %s\n", inet_ntoa(udp_serv_addr.sin_addr));
  printf("Tracker UDP port is: %d\n", udpport);
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
  int tcpport = 0;
  std::string ipaddr;
  setupManagerTCPComms(tcpsock, tcpport, ipaddr);

  //
  // Spawn tracker and clients
  //
  auto pid = fork();
  if (pid == 0) {
    // child process
    // create tracker
    // starts listening on for UDP connections
    // reports UDP port number to manager
    // listen for clients - give them what they want
    /*DEBUG*/std::cout << "DBG created child process for tracker pid = " << getpid() << std::endl;

    //
    // setup UDP socket for comms
    //
    int udpsock = 0;
    int udpport = 0;
    setupTrackerUDPComms(udpsock, udpport);
    /*DEBUG*/std::cout << "DBG tracker UDP port = " << udpport << std::endl;
    /*DEBUG*/std::cout << "DBG tracker socket = " << udpsock << std::endl;
    
    //
    // send UDP port to manager via TCP
    //
    sendUDPPortToManager(tcpport, udpport, ipaddr);

    /*DEBUG*/std::cout << "DBG exiting child process for tracker pid = " << getpid() << std::endl;
    exit(0);
  } else if (pid > 0) {
    // parent process
    // spawn clients
    //pid_t pids[manager->m_clients.size()];
    int clientid = 0;
    for (auto &client : manager->m_clients) {
      clientid = client->m_id;
      auto clientpid = fork();
      if (clientpid == 0) {
        // child process
        // client process
        // do client work
        /*DEBUG*/std::cout << "DBG created child process for client " << clientid << " pid = " << getpid() << std::endl;
        /*DEBUG*/std::cout << "DBG exiting child process for client " << clientid << " pid = " << getpid() << std::endl;
        exit(0);
      } else if (clientpid == -1) {
        // client fork failed
        perror("ERROR on client fork");
        exit(1); 
      }
    }
  
    //
    // listen for trackers and clients - give them what they want
    //
    char buffer[256];
    struct sockaddr_in cli_addr;
    
    listen(tcpsock,5);

    // accept actual connection from client
	  socklen_t clilen;
	  clilen = sizeof(cli_addr);
	  newtcpsock = accept(tcpsock, (struct sockaddr *)&cli_addr, &clilen);

    if (newtcpsock < 0) {
      perror("ERROR on manager accept");
      exit(1);
    }
    bzero(buffer, 256);
    int recv_bytes = recv(newtcpsock, buffer, sizeof(buffer), 0);
    /*DEBUG*/std::cout << "DBG manager TCP received a message.." << std::endl;
    printf("\"");
    for (int i = 0; i <= recv_bytes; i++) {
      printf("%c", buffer[i]);
    }
    printf("\"\n");
    
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
