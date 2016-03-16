#include <stdio.h>
#include <stdlib.h> // exit()
#include <string.h> // memset, bzero
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>

#include "Manager.h"
#include "Tracker.h"
#include "Client.h"

// socket sources
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
// end

//
// Declarations
//
void sendUDPPortToManager(int32_t managerport, int32_t trackerport, const char *ipaddr);
void setupTrackerUDPComms(int *udpsock, int *trackerport);
void setupManagerTCPComms(int *tcpsock, int *managerport, char **ipaddr);


//
// Main (manager)
//
int main(int argc, const char* argv[])
{
  // Prints each argument on the command line.
  for( int i = 0; i < argc; i++ )
  {
    printf( "arg %d: %s\n", i, argv[i] );
  }

  //
  // Read the configuration file
  //
  struct Manager *mgr = readMgrCfg();
  //for (int i = 0; i < mgr->m_numclients; i++) {
  //  struct Client *client = &(mgr->m_clients[i]);
  //  printf("Client %d:\n", client->m_id);
  //  printf("pktdelay %d, pktprob %d, numfiles %d, numtasks %d\n", client->m_pktdelay,
  //    client->m_pktprob, client->m_numfiles, client->m_numtasks);

  //  for (int i = 0; i < client->m_numfiles; i++) {
  //    printf("file %s\n", client->m_files[i]);
  //  } 

  //  for (int i = 0; i < client->m_numtasks; i++) {
  //    printf("task %d:\n", i);
  //    struct Task *task = (struct Task *)&(client->m_tasks[i]);
  //    printf("%s %d %d\n", task->m_file, task->m_starttime, task->m_share);
  //  } 
  //}


  //
  // Setup TCP listener for tracker and clients to communicate with
  //
  int tcpsock = 0;
  int newtcpsock = 0;
  int32_t managerport = 0;
  char* ipaddr = NULL;
  setupManagerTCPComms(&tcpsock, &managerport, &ipaddr);
  //printf("tcpsock = %d\n", tcpsock);
  //printf("newtcpsock = %d\n", newtcpsock);
  //printf("managerport = %d\n", managerport);
  //printf("ipaddr = %s\n", ipaddr);
  //
  // Spawn tracker and clients
  //
  pid_t pid = fork();
  if (pid == 0) {
    //=========================================================================
    // TRACKER (child process)
    //=========================================================================
    // create tracker
    // starts listening on for UDP connections
    // reports UDP port number to manager
    // listen for clients - give them what they want
    printf("DBG created child process for tracker pid = %d\n", getpid());

    //
    // setup UDP socket for comms
    //
    int udpsock = 0;
    int32_t trackerport = 0;
    setupTrackerUDPComms(&udpsock, &trackerport);
    //printf("DBG tracker UDP port = %d\n", trackerport);
    //printf("DBG tracker socket = %d\n", udpsock);

    //
    // send UDP port to manager via TCP
    //
    sendUDPPortToManager(managerport, trackerport, ipaddr);

    //
    // list for clients and give them what they want
    //
    trackerDoWork(udpsock, trackerport);

    printf("DBG exiting child process for tracker pid = %d\n", getpid());
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
    //printf("DBG manager TCP received a message..\n");
    //printf("\"");
    for (int i = 0; i <= recv_bytes; i++) {
      //printf("%c", buffer[i]);
    }
    //printf("\"\n");
    
    
    int32_t trackerport;
    sscanf(buffer, "%d", &trackerport);
    //printf("received msg = %s\n", buffer);
    //printf("TRACKER UDP PORT %d\n", trackerport);
    
    //
    // spawn clients
    //
    int clientid = 0;
    for (int i = 0; i < mgr->m_numclients; i++) {
      clientid = mgr->m_clients[i].m_id;
      pid_t clientpid = fork();
      if (clientpid == 0) {
        //=========================================================================
        // CLIENT (child process)
        //=========================================================================
        printf("DBG created child process for client %d with pid = %d\n", clientid, getpid());
        clientDoWork(clientid, managerport);
        printf("DBG created exiting child process client %d with pid = %d\n", clientid, getpid());
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
        //printf("DBG manager TCP received a message..\n");
        //printf("\"");
        //for (int i = 0; i <= recv_bytes; i++) {
        //  printf("%c", buffer[i]);
        //}
        //printf("\"\n");

        int clientid;
        char str1[10], str2[10];
        sscanf(buffer, "%s %d %s", str1, &clientid, str2);
        
        if (strcmp(str1, "CID") == 0 && strcmp(str2, "NEED_CFG") == 0) {
          for (int i = 0; i < mgr->m_numclients; i++) {
            struct Client *client = &(mgr->m_clients[i]);
            if (clientid == client->m_id) {

              // sending the entire client struct is somewhat wasteful, but does the job.
              u_char msg[sizeof(struct Client)];
              struct Client *sc = (struct Client *)serializeClient((struct Client *)client);
              memcpy(&msg, sc, sizeof(struct Client));
              int bytes_sent = -1, len = sizeof(msg);
              while (bytes_sent != len) {
                bytes_sent = send(newtcpsock, msg, len, 0);
              }
              //printf("Manager sent %d byte msg\n", bytes_sent);
              //printf("sizeof(int) %lu\n", sizeof(int));

              // send trackerport
              u_char portmsg[sizeof(int32_t)];
              int32_t n_trackerport = htonl(trackerport);
              memset(&portmsg, 0, sizeof(int32_t));
              memcpy(&portmsg, &n_trackerport, sizeof(int32_t));
              bytes_sent = -1, len = sizeof(int32_t);
              while (bytes_sent != len) {
                bytes_sent = send(newtcpsock, portmsg, len, 0);
              }
              //printf("Manager sent %d byte msg\n", bytes_sent);
              //printf("sizeof(int) %lu\n", sizeof(int));
              break;
            }
          }
        }
      }
    }

    //sleep(3);
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
          perror("pid exit failed");
          exit(1);
        }
      }
    }
    printf("DBG exiting manager process %d\n", getpid());
  } else {
    // fork failed
    perror("ERROR on fork");
    exit(1); 
  }

  return 0;
}

//
// Send UDP port to manager via TCP
//
void sendUDPPortToManager(int32_t managerport, int32_t trackerport, const char *ipaddr)
{
	int sockfd;
	struct addrinfo hints, *servinfo;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

  char mgrport[10];
  sprintf(mgrport, "%d", managerport);
	if (getaddrinfo(ipaddr, mgrport, &hints, &servinfo) != 0) {
		perror("ERROR (sendUDPPortToManager) on tracker getaddrinfo");
		exit(1);
	}

	if ((sockfd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol)) < 0) {
		perror("ERROR (sendUDPPortToManager) on tracker tcp socket");
		exit(1);
	}

	if (connect(sockfd, servinfo->ai_addr, servinfo->ai_addrlen) < 0) {
		perror("ERROR (sendUDPPortToManager) on tracker tcp connect");
		exit(1);
	}

	//printf("(sendUDPPortToManager) Connected!\n");

  char msg[80];
  sprintf(msg, "%d", trackerport);
  int bytes_sent = -1, len = strlen(msg);
  while (bytes_sent != len) {
    bytes_sent = send(sockfd, &msg, len, 0);
  }

}

//
// Setup tracker UDP socket
//
void setupTrackerUDPComms(int *udpsock, int *trackerport)
{
  struct sockaddr_in udp_serv_addr;

  // create UDP socket
  *udpsock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (*udpsock < 0) {
    perror("ERROR (setupTrackerUDPComms) on tracker UDP socket");
    exit(1);
  }

  // zero out the structure
  memset((char *) &udp_serv_addr, 0, sizeof(udp_serv_addr));

  udp_serv_addr.sin_family = AF_INET;
  udp_serv_addr.sin_addr.s_addr = INADDR_ANY;
  udp_serv_addr.sin_port = htons(0); // chooses a dynamic port

  /* Now bind the host address using bind() call.*/
  if (bind(*udpsock, (struct sockaddr *) &udp_serv_addr, sizeof(udp_serv_addr)) < 0) {
     perror("ERROR (setupTrackerUDPComms) tracker binding UDP port");
     exit(1);
  }
     
  /* Get the port number for communicating with spawned processes */
  socklen_t udp_serv_addr_size = sizeof(udp_serv_addr);
  if (getsockname(*udpsock, (struct sockaddr *) &udp_serv_addr, &udp_serv_addr_size) == -1) {
     perror("ERROR (setupTrackerUDPComms) on tracker getsockname");
     exit(1);
  }
  *trackerport = ntohs(udp_serv_addr.sin_port);
  //printf("Tracker UDP IP address is: %s\n", inet_ntoa(udp_serv_addr.sin_addr));
  //printf("Tracker UDP port is: %d\n", *trackerport);
}


//
// Setup manager TCP socket
//
void setupManagerTCPComms(int *tcpsock, int *managerport, char **ipaddr)
{
  struct sockaddr_in serv_addr;

  /* First call to socket() function */
  *tcpsock = socket(AF_INET, SOCK_STREAM, 0);
  
  if (*tcpsock < 0) {
     perror("ERROR (setupManagerTCPComms) opening socket");
     exit(1);
  }
  
  /* Initialize socket structure */
  bzero((char *) &serv_addr, sizeof(serv_addr));
  
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = INADDR_ANY;
  serv_addr.sin_port = htons(0);
  
  /* Now bind the host address using bind() call.*/
  if (bind(*tcpsock, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
     perror("ERROR (setupManagerTCPComms) on binding");
     exit(1);
  }
     
  /* Get the port number for communicating with spawned processes */
  socklen_t serv_addr_size = sizeof(serv_addr);
  if (getsockname(*tcpsock, (struct sockaddr *) &serv_addr, &serv_addr_size) == -1) {
     perror("ERROR (setupManagerTCPComms) on getsockname");
     exit(1);
  }
  *managerport = ntohs(serv_addr.sin_port);
  *ipaddr = inet_ntoa(serv_addr.sin_addr);

  //printf("Manager IP address is: %s\n", *ipaddr);
  //printf("Manager TCP port is: %d\n", *managerport);
  //printf("Manager tpcsock is: %d\n", *tcpsock);
}

