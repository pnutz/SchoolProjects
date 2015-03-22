/*---------------------------------------------------------------------------------------
--	SOURCE FILE:		tcp_clnt.c - A simple TCP client program.
--
--	PROGRAM:		tclnt.exe
--
--	FUNCTIONS:		Berkeley Socket API
--
--	DATE:			January 23, 2001
--
--	REVISIONS:		(Date and Description)
--				January 2005
--				Modified the read loop to use fgets.
--				While loop is based on the buffer length 
--
--
--	DESIGNERS:		Aman Abdulla
--
--	PROGRAMMERS:		Aman Abdulla
--
--	NOTES:
--	The program will establish a TCP connection to a user specifed server.
-- 	The server can be specified using a fully qualified domain name or and
--	IP address. After the connection has been established the user will be
-- 	prompted for date. The date string is then sent to the server and the
-- 	response (echo) back from the server is displayed.
---------------------------------------------------------------------------------------*/
#include <stdio.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/syscall.h>
#include <pthread.h>

#define SERVER_TCP_PORT		7000	// Default port
#define BUFLEN		      	255  	// Buffer length
#define DATA              "DATA"
#define FILENAME          "clnt_connections.txt"

struct ThreadInfo {
  int thread_index;
} ThreadInfo;

void* openConnection(void*);
long long timeval_diff(struct timeval*, struct timeval*, struct timeval*);

int send_count, wait_time, port;
char *host;
FILE *file;

int main (int argc, char **argv)
{
	int thread_count;
	char *endptr, *b;
  int base = 10;
  struct ThreadInfo *info_ptr;

  errno = 0;
	switch(argc)
	{
		case 5:
			host = argv[1];	// Host name
      thread_count = strtol(argv[2], &endptr, base);
      if (errno != 0 && thread_count == 0)
      {
        perror("strtol");
        exit(1);
      }
      send_count = strtol(argv[3], &endptr, base);
      if (errno != 0 && send_count == 0)
      {
        perror("strtol");
        exit(1);
      }
      wait_time = strtol(argv[4], &endptr, base);     
      if (errno != 0 && wait_time == 0)
      {
        perror("strtol");
        exit(1);
      }
			port = SERVER_TCP_PORT;
		break;
		case 6:
			host = argv[1];
      thread_count = strtol(argv[2], &endptr, base);
      if (errno != 0 && thread_count == 0)
      {
        perror("strtol");
        exit(1);
      }
      send_count = strtol(argv[3], &endptr, base);
      if (errno != 0 && send_count == 0)
      {
        perror("strtol");
        exit(1);
      }
      wait_time = strtol(argv[4], &endptr, base);
      if (errno != 0 && wait_time == 0)
      {
        perror("strtol");
        exit(1);
      }
			port = strtol(argv[5], &endptr, base);	// User specified port
      if (errno != 0 && port == 0)
      {
        perror("strtol");
        exit(1);
      }
		break;
		default:
			fprintf(stderr, "Usage: %s <host> <number of thread connections to create> <number of times to send string> <number of seconds to wait before sending next string> [port]\n", argv[0]);
			exit(1);
	}

  if ((file = fopen(FILENAME, "w")) == NULL)
  {
    printf("Can't open output file: %s\n", FILENAME);
    exit(1);
  }

  pthread_t thread_id[thread_count];

  int i;
  // create a thread for each client connection (parent thread counts as 1)
  for (i = 0; i < thread_count; i++)
  {
    if ((info_ptr = malloc(sizeof (struct ThreadInfo))) == NULL)
    {
      perror("malloc");
      exit(1);
    }
    info_ptr->thread_index = i;
    pthread_create(&thread_id[i], NULL, openConnection, (void*) info_ptr);
    printf("Created thread %i\n", i);
  }
  
  for (i = 0; i < thread_count; i++)
  {
    pthread_join(thread_id[i], (void**)&b);
  }
  fclose(file);
	return (0);
}

void* openConnection(void* info_ptr)
{
  struct ThreadInfo *thread_info = (struct ThreadInfo*) info_ptr;
  int thread_index = thread_info->thread_index;
  free(info_ptr);

	int sd, n, bytes_to_read;
	struct hostent *hp;
	struct sockaddr_in server;
	char *bp, rbuf[BUFLEN], diff[50];
  struct timeval start, end;

  int data_sent = 0;

	// Create the socket
	if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
	{
		perror("Cannot create socket");
		exit(1);
	}
	bzero((char *)&server, sizeof(struct sockaddr_in));
	server.sin_family = AF_INET;
	server.sin_port = htons(port);
	if ((hp = gethostbyname(host)) == NULL)
	{
		fprintf(stderr, "Unknown server address\n");
		exit(1);
	}
	bcopy(hp->h_addr, (char *)&server.sin_addr, hp->h_length);

	// Connecting to the server
	if (connect (sd, (struct sockaddr *)&server, sizeof(server)) == -1)
	{
		fprintf(stderr, "Can't connect to server\n");
		perror("connect");
		exit(1);
	}
	printf("Connected:    Server Name: %s\n", hp->h_name);

  int i;
  for (i = 0; i < send_count; i++)
  {
    //printf("Transmit %i: %s\n", i, DATA);

    // set start time
    if (gettimeofday(&start, NULL))
    {
      perror("start gettimeofday");
      exit(1);
    }

    // Transmit data through the socket
    send (sd, DATA, BUFLEN, 0);

    data_sent += BUFLEN;
    //printf("Receive %i:\n", i);
    bp = rbuf;
    bytes_to_read = BUFLEN;

    // client makes repeated calls to recv until no more data is expected to arrive.
    n = 0;
    while ((n = recv (sd, bp, bytes_to_read, 0)) < BUFLEN)
    {
      bp += n;
      bytes_to_read -= n;
    }
    /*printf("%s\n", rbuf);
    fflush(stdout);*/

    // get end time
    if (gettimeofday(&end, NULL))
    {
      perror("end gettimeofday");
      exit(1);
    }

    // get elapsed time
    sprintf(diff, "%lld", timeval_diff(NULL, &end, &start));
    printf("thread %*i | %*i requests sent | %*i bytes sent | %*s echo time\n", 5, thread_index, 3, i+1, 6, data_sent, 7, diff);
    fprintf(file, "thread %*i | %*i requests sent | %*i bytes sent | %*s echo time\n", 5, thread_index, 3, i+1, 6, data_sent, 7, diff);
    // delay wait_time s
    sleep(wait_time);
  }
  printf("Closing connection\n");
	close (sd);
  return 0;
}

// calculate difference in time between end_time and start_time (return usec)
long long timeval_diff(struct timeval *difference, struct timeval *end_time, struct timeval *start_time)
{
  struct timeval temp_diff;

  if (difference == NULL)
  {
    difference = &temp_diff;
  }

  difference->tv_sec = end_time->tv_sec - start_time->tv_sec;
  difference->tv_usec = end_time->tv_usec - start_time->tv_usec;

  while (difference->tv_usec < 0)
  {
    difference->tv_usec += 1000000;
    difference->tv_sec -= 1;
  }

  return 1000000LL * difference->tv_sec + difference->tv_usec;
}
