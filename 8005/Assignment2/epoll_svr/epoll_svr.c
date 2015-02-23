/*---------------------------------------------------------------------------------------
--	SOURCE FILE:		select_svr.c -   A simple echo server using select
--
--	PROGRAM:		tsvr.exe
--
--	FUNCTIONS:		Berkeley Socket API
--
--	DATE:			January 23, 2001
--
--	REVISIONS:		(Date and Description)
--
--				January 2005
--				Modified the read loop to use fgets.
--				While loop is based on the buffer length 
--
--	DESIGNERS:		Aman Abdulla
--
--	PROGRAMMERS:		Aman Abdulla
--
--	NOTES:
--	The program will accept TCP connections from client machines.
-- The program will read data from the client socket and simply echo it back.
---------------------------------------------------------------------------------------*/
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <strings.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <fcntl.h>

#include "timer.h"

#define SERVER_TCP_PORT 7000  // Default port
#define BUFLEN	255           // Buffer length
#define TRUE	1
#define THREAD_COUNT 10
#define BASE_THREAD_COUNT 2
#define MAX_THREAD_COUNT 25000/THREAD_COUNT
#define FILENAME "connections.txt"

// parameter for thread function
struct ThreadInfo {
  int parent_thread_index;
  int thread_index;
} ThreadInfo;

struct ReaderThread {
  pthread_t thread_id;
  int num_client;
  int last_index;
  int num_thread;
  int child_index; // equivalent to reader_index for child threads
} ReaderThread;

struct ChildThread {
  pthread_t thread_id;
  int sd;
  struct sockaddr_in client;
  int bytes_sent;
  int num_requests;
} ChildThread;

// rwlock for rset/allset/reader_index
pthread_rwlock_t rwlock;
// struct containing assigned reader thread index, child thread accepts rset sd
int reader_index = -1;
fd_set rset, allset;
int maxfd;

// rwlock for client
pthread_rwlock_t child_rwlock[THREAD_COUNT];
struct ReaderThread reader[THREAD_COUNT];
struct ChildThread client[THREAD_COUNT][MAX_THREAD_COUNT];

int sd;

int initOutputFile();
int writeConnections();
void* reader_method(void*);
void* echo(void*);
//long long timeval_diff(struct timeval*, struct timeval*, struct timeval*);

// print connection details on timeout
void handler(int sig, siginfo_t *si, void *uc)
{
  if (si->si_value.sival_ptr == &timerid)
  {
    writeConnections();
    armTimer();
  }
}

int main (int argc, char **argv)
{
	int	i, port, nready;
	struct sockaddr_in server;
  struct ThreadInfo *info_ptr[THREAD_COUNT];

	switch(argc)
	{
		case 1:
			port = SERVER_TCP_PORT;	// Use the default port
		break;
		case 2:
			port = atoi(argv[1]);	// Get user specified port
		break;
		default:
			fprintf(stderr, "Usage: %s [port]\n", argv[0]);
			exit(1);
	}

  pthread_rwlock_init(&rwlock, NULL);
  for (i = 0; i < THREAD_COUNT; i++)
  {
    pthread_rwlock_init(&child_rwlock[i], NULL);

    if ((info_ptr[i] = malloc(sizeof (struct ThreadInfo))) == NULL)
    {
      perror("malloc");
      exit(1);
    }
  }

  initOutputFile();

	// Create a stream socket
	if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
	{
		perror("Can't create a socket");
		exit(1);
	}

  // reuse address socket option
  int arg = 1;
  if (setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &arg, sizeof(arg)) == -1)
  {
    perror("Can't set socket option");
    exit(1);
  }

	// Bind an address to the socket
	bzero((char *)&server, sizeof(struct sockaddr_in));
	server.sin_family = AF_INET;
	server.sin_port = htons(port);
	server.sin_addr.s_addr = htonl(INADDR_ANY); // Accept connections from any client

	if (bind(sd, (struct sockaddr *)&server, sizeof(server)) == -1)
	{
		perror("Can't bind name to socket");
		exit(1);
	}

	// Listen for connections
	// queue up to THREAD_COUNT connect requests
	listen(sd, THREAD_COUNT);
  
  maxfd = sd;
  FD_ZERO(&allset);
  FD_SET(sd, &allset);

  for (i = 0; i < THREAD_COUNT; i++)
  {
    info_ptr[i]->thread_index = i;
    pthread_create(&reader[i].thread_id, NULL, reader_method, (void*) info_ptr[i]);
    printf("Created thread %lu %i\n", (unsigned long) reader[i].thread_id, i);
  }

  // initialize timer signal and arm timer
  timerinit(10, 0, handler);
  armTimer();

  // setup select loop
  while (TRUE)
  {
    pthread_rwlock_wrlock(&rwlock);
    rset = allset;
    while ((nready = select(maxfd + 1, &rset, NULL, NULL, NULL) == -1) && errno == EINTR)
    {
      continue;
    }

    if (reader_index == -1 && FD_ISSET(sd, &rset))
    {
      // set reader_index to thread with least connections from first thread
      int least_clients = -1;
      for (i = 0; i < THREAD_COUNT; i++)
      {
        if (least_clients == -1)
        {
          reader_index = i;
          least_clients = reader[i].num_client;
        }
        else if (reader[i].num_client < least_clients)
        {
          reader_index = i;
          least_clients = reader[i].num_client;
        }
      }
    }
    pthread_rwlock_unlock(&rwlock);
  }

	close(sd);
  for (i = 0; i < THREAD_COUNT; i++)
  {
    free(info_ptr[i]);
  }
  exit(0);
}

void* reader_method(void* info_ptr)
{
  struct ThreadInfo* thread_info = (struct ThreadInfo*) info_ptr;
  int thread_index = thread_info->thread_index;
  socklen_t client_len = sizeof(struct sockaddr_in);
  int i, new_sd, child_index;
  struct ThreadInfo *child_info_ptr[MAX_THREAD_COUNT];

  for (i = 0; i < MAX_THREAD_COUNT; i++)
  {
    if ((child_info_ptr[i] = malloc(sizeof (struct ThreadInfo))) == NULL)
    {
      perror("malloc");
      exit(1);
    }  
  }

  reader[thread_index].last_index = BASE_THREAD_COUNT;
  reader[thread_index].num_thread = BASE_THREAD_COUNT;
  reader[thread_index].num_client = 0;
  reader[thread_index].child_index = -1;

  // initialize client with BASE_THREAD_COUNT threads
  for (i = 0; i < BASE_THREAD_COUNT; i++)
  {
    child_info_ptr[i]->parent_thread_index = thread_index;
    child_info_ptr[i]->thread_index = i;
    pthread_create(&client[thread_index][i].thread_id, NULL, echo, (void*) child_info_ptr[i]);
    //printf("Created thread %lu %i-%i\n", (unsigned long) client[thread_index][i].thread_id, thread_index, i);

    client[thread_index][i].sd = -1;
    client[thread_index][i].bytes_sent = 0;
    client[thread_index][i].num_requests = 0;
  }

  while (TRUE)
  {
    pthread_rwlock_rdlock(&rwlock);
    // handle new connection
    if (reader_index == thread_index)
    {
      // find usable child thread, only reader can accept connections
      pthread_rwlock_wrlock(&child_rwlock[thread_index]);
      for (i = 0; i < reader[thread_index].last_index; i++)
      {
        if (client[thread_index][i].sd == -1)
        {
          child_index = i;
          reader[thread_index].child_index = i;
          break;
        }
      }

      if ((new_sd = accept(sd, (struct sockaddr *) &client[thread_index][child_index].client, &client_len)) == -1)
      {
        perror("accept");
        exit(1);
      }
      
      client[thread_index][child_index].sd = new_sd;
      client[thread_index][child_index].bytes_sent = 0;
      client[thread_index][child_index].num_requests = 0;
      reader[thread_index].num_client++;

      // buffer full of clients, create a new child thread 
      if (reader[thread_index].num_client == reader[thread_index].num_thread && reader[thread_index].num_thread < MAX_THREAD_COUNT)
      {
        int new_thread_index;
        // if buffer reaches end of current list, extend list
        if (reader[thread_index].num_thread == reader[thread_index].last_index)
        {
          new_thread_index = reader[thread_index].last_index;
          reader[thread_index].last_index++;
        }
        else
        {
          // fill deleted thread location if it exists (bytes_sent = -1)
          for (i = reader[thread_index].last_index - 1; i < 0; i--)
          {
            if (client[thread_index][i].bytes_sent == -1)
            {
              new_thread_index = i;
              break;
            }
          }
        }
        child_info_ptr[new_thread_index]->parent_thread_index = thread_index;
        child_info_ptr[new_thread_index]->thread_index = new_thread_index;
        pthread_create(&client[thread_index][new_thread_index].thread_id, NULL, echo, (void*) child_info_ptr[new_thread_index]);
        reader[thread_index].num_thread++;
        client[thread_index][new_thread_index].sd = -1;
        client[thread_index][new_thread_index].bytes_sent = 0;
        client[thread_index][new_thread_index].num_requests = 0;
      
        printf("Created thread %lu %i\n", (unsigned long) client[thread_index][new_thread_index].thread_id, reader[thread_index].num_thread);
      }

      pthread_rwlock_unlock(&child_rwlock[thread_index]);

      FD_SET(new_sd, &allset);
      if (new_sd > maxfd)
      {
        maxfd = new_sd;
      }
      reader_index = -1;
    }
    pthread_rwlock_unlock(&rwlock);

    // close excess threads (buffer of 5)
    pthread_rwlock_wrlock(&child_rwlock[thread_index]);
    if (reader[thread_index].num_thread > BASE_THREAD_COUNT && reader[thread_index].num_client + 5 >= reader[thread_index].num_thread)
    {
      for (i = reader[thread_index].last_index - 1; i < 0; i--)
      {
        if (client[thread_index][i].sd == -1)
        {
          printf("Cancelled thread %lu %i\n", client[thread_index][i].thread_id, reader[thread_index].num_thread);
          if (pthread_cancel(client[thread_index][i].thread_id) != 0)
          {
            perror("pthread_cancel");
            exit(1);
          }
          reader[thread_index].num_thread--;
          client[thread_index][i].bytes_sent = -1;
          break;
        }
      }
    }
    pthread_rwlock_unlock(&child_rwlock[thread_index]);
  }

  for (i = 0; i < MAX_THREAD_COUNT; i++)
  {
    free(child_info_ptr[i]);
  }
}

void* echo(void* info_ptr)
{
  struct ThreadInfo* thread_info = (struct ThreadInfo *) info_ptr;
  int thread_index = thread_info->parent_thread_index;
  int client_index = thread_info->thread_index;
  int sd = -1;

  int n, bytes_to_read;
  char *bp, buf[BUFLEN];

  while (TRUE)
  {
    // check to setup new connection
    if (sd == -1)
    {
      pthread_rwlock_rdlock(&child_rwlock[thread_index]);
      if (reader[thread_index].child_index == client_index)
      {
        sd = client[thread_index][client_index].sd;

        printf("%lu - Remote Address:  %s\n", (unsigned long) pthread_self(), inet_ntoa(client[thread_index][client_index].client.sin_addr));
      }
      pthread_rwlock_unlock(&child_rwlock[thread_index]);
    }
    else if (FD_ISSET(sd, &rset))
    {
      FD_CLR(sd, &rset);
      bp = buf;
      bytes_to_read = BUFLEN;
      
      // loop until entire message received
      while ((n = recv (sd, bp, bytes_to_read, 0)) < BUFLEN)
      {
        bp += n;
        bytes_to_read -= n;
      }
     
      client[thread_index][client_index].num_requests += 1;
      //printf ("%i - Sending: %s\n", client[thread_index][child_index].thread_id, buf);
      send (sd, buf, BUFLEN, 0);
      client[thread_index][client_index].bytes_sent += BUFLEN;

      if (n == 0)
      {
        pthread_rwlock_rdlock(&child_rwlock[thread_index]);
        //printf("%ld, %i - Closing Connection %i\n", (long) getpid(), thread_id, client_count);
        printf("Thread %i-%i completed a connection\n", thread_index, client_index);
        close(sd);
        client[thread_index][client_index].sd = -1;
        reader[thread_index].num_client--;
        pthread_rwlock_unlock(&child_rwlock[thread_index]);
      }
    }
  }
  return 0;
}

// calculate difference in time between end_time and start_time (return usec)
/*long long timeval_diff(struct timeval *difference, struct timeval *end_time, struct timeval *start_time)
{
  struct timeval temp_diff;

  if (difference == NULL)
  {
    difference = &temp_diff;
  }

  difference.tv_sec = end_time.tv_sec - start_time.tv_sec;
  difference.tv_usec = end_time.tv_usec - start_time.tv_usec;

  while (difference.tv_usec < 0)
  {
    difference.tv_usec += 1000000;
    difference.tv_sec -= 1;
  }

  return 1000000LL * difference.tv_sec + difference.tv_usec;
}*/

int initOutputFile()
{
  FILE *file;
  if ((file = fopen(FILENAME, "w")) == NULL)
  {
    printf("Can't open output file: %s\n", FILENAME);
    return 1;
  }

  fprintf(file, "Time                  | Process | # Requests | Amt of Data Transferred\n");
  fprintf(file, "______________________________________________________________________\n");

  fclose(file);
  return 0;
}

int writeConnections()
{
  FILE *file;
  time_t timer;
  char time_buffer[25];
  struct tm *tm_info;
  struct timeval tv;

  if ((file = fopen(FILENAME, "a")) == NULL)
  {
    printf("Can't open output file: %s\n", FILENAME);
    return 1;
  }

  time(&timer);
  tm_info = localtime(&timer);
  strftime(time_buffer, 25, "%D %T", tm_info);

  gettimeofday(&tv, 0);

  // write connection details for active connections
  int i, j;
  for (i = 0; i < THREAD_COUNT; i++)
  {
    for (j = 0; j < reader[i].last_index; j++)
    {
      if (client[i][j].sd != -1 && client[i][j].bytes_sent != -1)
      {
        // print thread_conn[i] info
        printf("%*s:%*i | %*i | %*i\n", 17, time_buffer, 3, (int) tv.tv_usec % 1000, 10, client[i][j].num_requests, 23, client[i][j].bytes_sent);
        fprintf(file, "%*s:%*i | %*i | %*i\n", 17, time_buffer, 3, (int) tv.tv_usec % 1000, 10, client[i][j].num_requests, 23, client[i][j].bytes_sent);
      }
    }
  }

  fclose(file);
  return 0;
}
