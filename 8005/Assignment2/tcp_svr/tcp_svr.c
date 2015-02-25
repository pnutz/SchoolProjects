/*---------------------------------------------------------------------------------------
--	SOURCE FILE:		tcp_svr.c -   A simple echo server using TCP
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
#define PROCESS_COUNT 19
#define BASE_THREAD_COUNT 2
#define MAX_THREAD_COUNT 100
#define FILENAME "connections.txt"

struct ConnectionInfo {
  struct sockaddr_in client;
  int bytes_sent;
  int num_requests;
};

int initOutputFile();
int writeConnections();
void* echo();
long long timeval_diff(struct timeval*, struct timeval*, struct timeval*);

// mutex variable
pthread_mutex_t conn_lock = PTHREAD_MUTEX_INITIALIZER;
int sd_conn = -1;
struct sockaddr_in client_conn;
int client_count = 0;

pthread_mutex_t thread_lock = PTHREAD_MUTEX_INITIALIZER;
int thread_count = BASE_THREAD_COUNT;
pthread_t thread_id[MAX_THREAD_COUNT];

struct ConnectionInfo thread_conn[MAX_THREAD_COUNT];

pthread_mutex_t file_lock = PTHREAD_MUTEX_INITIALIZER;

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
	int	sd, port;
	struct sockaddr_in server;
  pid_t childpid = 0; 

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

  // make file_lock mutex system-wide (between processes)
  pthread_mutexattr_t mutexattr;
  pthread_mutexattr_init(&mutexattr);
  pthread_mutexattr_setpshared(&mutexattr, PTHREAD_PROCESS_SHARED);
  pthread_mutex_init(&file_lock, &mutexattr);
  pthread_mutexattr_destroy(&mutexattr);

  initOutputFile();

  int i;
  // initialize thread_id
  for (i = 0; i < MAX_THREAD_COUNT; i++)
  {
    thread_id[i] = 0;
  }

	// Create a stream socket
	if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
	{
		perror ("Can't create a socket");
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
	listen(sd, SOMAXCONN);
  
  for (i = 0; i < PROCESS_COUNT; i++)
  {
    childpid = fork();
    if (childpid == 0) // child
    {
     	printf("Created child process %ld\n", (long) getpid());
      break;
    }
    else if (childpid < 0) // error occurred
    {
      perror("fork failed\n");
      return 1;
    }
  }

  // each process including parent accepts connections
  if (childpid >= 0)
  {
    // create threads
    for (i = 0; i < BASE_THREAD_COUNT; i++)
    {
      pthread_create(&thread_id[i], NULL, echo, NULL);
      //printf("Created thread %lu %i for process %ld\n", (unsigned long) thread_id[i], i, (long) getpid());
    }

    socklen_t client_len = sizeof(client_conn);

    // initialize timer signal and arm timer
    timerinit(10, 0, handler);
    armTimer();

    while (TRUE)
    {
      // obtain mutex
      pthread_mutex_lock(&conn_lock);
      if (sd_conn == -1)
      {
        sd_conn = accept(sd, (struct sockaddr *)&client_conn, &client_len);
        if (sd_conn == -1)
        {
          perror("accept");
          return 1;
        }
        else
        {
          client_count++;
          printf("Process %ld has %i connections\n", (long) getpid(), client_count);
          //printf("%i - Accepted new connection %i\n", sd_conn, client_count);
        }
      }
      pthread_mutex_unlock(&conn_lock);

      // dynamically create more threads with 5-connection buffer
      if (client_count + 5 >= thread_count && thread_count < MAX_THREAD_COUNT)
      {
        pthread_mutex_lock(&thread_lock);
        for (i = 0; i < MAX_THREAD_COUNT; i++)
        {
          if (thread_id[i] == 0)
          {
            pthread_create(&thread_id[i], NULL, echo, NULL);
            break;
          }
        }
        printf("Created thread %i for process %ld\n", thread_count, (long) getpid());
        thread_count++;
        pthread_mutex_unlock(&thread_lock);
      }
    }
  }
	close(sd);
  exit(0);
}

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
  pthread_mutex_lock(&file_lock);
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
  int i;
  for (i = 0; i < MAX_THREAD_COUNT; i++)
  {
    if (thread_id[i] != 0 && thread_conn[i].bytes_sent != -1)
    {
      // print thread_conn[i] info
      printf("%*s:%*i | %*ld | %*i | %*i\n", 17, time_buffer, 3, (int) tv.tv_usec % 1000, 7, (long) getpid(), 10, thread_conn[i].num_requests, 23, thread_conn[i].bytes_sent);
      fprintf(file, "%*s:%*i | %*ld | %*i | %*i\n", 17, time_buffer, 3, (int) tv.tv_usec % 1000, 7, (long) getpid(), 10, thread_conn[i].num_requests, 23, thread_conn[i].bytes_sent);
    }
  }

  fclose(file);
  pthread_mutex_unlock(&file_lock);
  return 0;
}

void* echo()
{
  int i, n, bytes_to_read, thread_index;
  char *bp, buf[BUFLEN];
  struct timeval start, end;

  int complete = 0;
  int new_sd = -1;

  for (i = 0; i < MAX_THREAD_COUNT; i++)
  {
    if (thread_id[i] == pthread_self())
    {
      thread_index = i;
      break;
    }
  }

  thread_conn[thread_index].bytes_sent = -1;

  while (TRUE)
  {
    // check mutex for new connection
    pthread_mutex_lock(&conn_lock);
    if (sd_conn != -1)
    {
      new_sd = sd_conn;
      thread_conn[thread_index].client = client_conn;
      thread_conn[thread_index].num_requests = 0;
      thread_conn[thread_index].bytes_sent = 0;
      sd_conn = -1;
    }
    pthread_mutex_unlock(&conn_lock);

    if (new_sd != -1)
    {
      int status = fcntl(new_sd, F_SETFL, fcntl(new_sd, F_GETFL, 0) | O_NONBLOCK);
      if (status == -1)
      {
        perror("fcntl");
      }

      printf("%ld, %lu - Remote Address:  %s\n", (long) getpid(), (unsigned long) pthread_self(), inet_ntoa(thread_conn[thread_index].client.sin_addr));

      // loop echo until timeout, then close connection
      while (TRUE)
      {
        bp = buf;
        bytes_to_read = BUFLEN;
        
        // set start time
        if (gettimeofday(&start, NULL))
        {
          perror("start gettimeofday");
          exit(1);
        }
     
        // loop until entire message received or 5 second timeout occurs
        while ((n = recv (new_sd, bp, bytes_to_read, 0)) < BUFLEN)
        {
          if (errno == EWOULDBLOCK)
          {
            // get end time
            if (gettimeofday(&end, NULL))
            {
              perror("end gettimeofday");
              exit(1);
            }

            if (timeval_diff(NULL, &end, &start) > 5000000LL)
            {
              complete = 1;
              break;
            }
          }
          else
          {
            bp += n;
            bytes_to_read -= n;
          }
        }

        // end connection after 5 second timeout
        if (complete)
        {
          complete = 0;
          break;
        }

        thread_conn[thread_index].num_requests += 1;
        //printf ("%ld, %i - Sending: %s\n", (long) getpid(), thread_id, buf);
        send (new_sd, buf, BUFLEN, 0);
        thread_conn[thread_index].bytes_sent += BUFLEN;
      }

      //printf("%ld, %i - Closing Connection %i\n", (long) getpid(), thread_id, client_count);
      printf("Process %ld completed a connection\n", (long) getpid());
      thread_conn[thread_index].bytes_sent = -1;
      pthread_mutex_lock(&conn_lock);
      client_count--;
      pthread_mutex_unlock(&conn_lock);

      close (new_sd);
      new_sd = -1;

      // buffer of 10 connections before closing excess threads
      if (client_count + 10 < thread_count && thread_count > BASE_THREAD_COUNT)
      {
        pthread_mutex_lock(&thread_lock);
        printf("Killed thread %i for process %ld\n", thread_count, (long) getpid());
        thread_count--; 
        thread_id[thread_index] = 0;
        pthread_mutex_unlock(&thread_lock);
        pthread_exit(NULL);
      }
      else if (client_count == 0)
      {
        printf("Finished responding to all requests.\n");
      }
    }
  }
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
