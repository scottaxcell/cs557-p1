#include <stdio.h>
#include <stdlib.h> // exit()
#include <string.h> // memset, bzero
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>

#include "Tracker.h"

// socket sources
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
// end

//
// Tracker worker
//
void trackerDoWork(int udpsock, int32_t trackerport)
{
  // write out log information
  FILE *fp;
  const char *filename = "tracker.out";
  fp = fopen(filename, "w");
  if (fp == NULL) {
    perror("ERROR opening client log file");
    exit(1);
  }
  fprintf(fp, "type Tracker\n");
  fprintf(fp, "pid %d\n", getpid());
  fprintf(fp, "tPort %d\n", trackerport);

  //socklen_t fromlen;
  //struct sockaddr_in addr;
  //fromlen = sizeof(addr);
  //while (true) {
  //  u_char buffer[256];
  //  int recv_bytes = recvfrom(udpsock, buffer, sizeof(buffer), 0, (struct sockaddr*)&addr, &fromlen);
  //  if (recv_bytes == -1) {
  //    perror("ERROR manager recv failed");
  //    exit(1);
  //  } else {
  //    printf("DBG tracker UDP received a message..\n");
  //    printf("\"");
  //    for (int i = 0; i <= recv_bytes; i++) {
  //      printf("%c", buffer[i]);
  //    }
  //    printf("\"\n");
  //  }
  //}
  sleep(3);
}

