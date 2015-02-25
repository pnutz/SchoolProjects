/*---------------------------------------------------------------------------------------
--	SOURCE FILE:		epoll_svr.c -   A simple echo server using select
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
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <sys/epoll.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>

#include "timer.h"

#define SERVER_TCP_PORT 7000  // Default port
#define BUFLEN	255           // Buffer length
#define TRUE	1
#define THREAD_COUNT 10
#define BASE_THREAD_COUNT 2
#define MAX_THREAD_COUNT 25000/THREAD_COUNT
#define EPOLL_QUEUE_LEN 25000
#define FILENAME "connections.txt"

// parameter for thread function
struct ThreadInfo {
  int parent_thread_index;
  int thread_index;
} ThreadInfo;

struct ReaderThread {
  pthread_t thread_id;
  int num_client;
  int num_thread;
} ReaderThread;

struct Client {
  struct sockaddr_in client;
  int bytes_sent;
  int num_requests;
} Client;

// mutex for reader_index and fd to pass to reader thread
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
int reader_index = -1;
int new_fd;

struct ReaderThread reader[THREAD_COUNT];
struct Client connection[EPOLL_QUEUE_LEN]; // index is fd

pthread_t thread_id[THREAD_COUNT][MAX_THREAD_COUNT];

int fd, main_epoll_fd;
int epoll_fd[THREAD_COUNT];
int pfd[THREAD_COUNT][2];
int maxfd;

int initOutputFile();
int writeConnections();
void* readerMethod(void*);
void* echo(void*);
void closeFd(int);
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
	int	i, port, num_fds, client_index;
	struct sockaddr_in server;
  socklen_t client_len = sizeof(struct sockaddr_in);
  struct ThreadInfo *info_ptr[THREAD_COUNT];
  struct sigaction act;
  struct epoll_event events[1], event;

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

  // setup the signal handler to close the server socket when CTRL-c is received
  act.sa_handler = closeFd;
  act.sa_flags = 0;
  if ((sigemptyset(&act.sa_mask) == -1 || sigaction(SIGINT, &act, NULL) == -1))
  {
    perror("Failed to set SIGINT handler");
    exit(1);
  }

  for (i = 0; i < THREAD_COUNT; i++)
  {
    if ((info_ptr[i] = malloc(sizeof (struct ThreadInfo))) == NULL)
    {
      perror("malloc");
      exit(1);
    }
  }

  // initialize connections
  for (i = 0; i < EPOLL_QUEUE_LEN; i++)
  {
    connection[i].bytes_sent = -1;
    connection[i].num_requests = 0;
  }

  initOutputFile();

	// Create a stream socket
	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
	{
		perror("Can't create a socket");
		exit(1);
	}

  // reuse address socket option
  int arg = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &arg, sizeof(arg)) == -1)
  {
    perror("Can't set socket option");
    exit(1);
  }

	// Bind an address to the socket
	memset(&server, 0, sizeof(struct sockaddr_in));
	server.sin_family = AF_INET;
	server.sin_port = htons(port);
	server.sin_addr.s_addr = htonl(INADDR_ANY); // Accept connections from any client

	if (bind(fd, (struct sockaddr *)&server, sizeof(server)) == -1)
	{
		perror("Can't bind name to socket");
		exit(1);
	}

	// Listen for connections
	listen(fd, SOMAXCONN);
  maxfd = fd + 1; 

  main_epoll_fd = epoll_create(1);
  if (main_epoll_fd == -1)
  {
    perror("epoll_create");
    exit(1);
  }

  // add server socket to epoll event loop
  event.events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLET;
  event.data.fd = fd;
  if (epoll_ctl(main_epoll_fd, EPOLL_CTL_ADD, fd, &event) == -1)
  {
    perror("epoll_ctl");
    exit(1);
  }
 
  for (i = 0; i < THREAD_COUNT; i++)
  {
    info_ptr[i]->thread_index = i;
    pthread_create(&reader[i].thread_id, NULL, readerMethod, (void*) info_ptr[i]);
    reader[i].num_client = 0;
    reader[i].num_thread = BASE_THREAD_COUNT;
    printf("Created thread %lu %i\n", (unsigned long) reader[i].thread_id, i);
  }

  // initialize timer signal and arm timer
  timerinit(10, 0, handler);
  armTimer();

  while (TRUE)
  {
    num_fds = epoll_wait(main_epoll_fd, events, EPOLL_QUEUE_LEN, -1);
    if (num_fds < 0 && errno != EINTR)
    {
      perror("epoll_wait");
      exit(1);
    }

    if (num_fds == 1)
    {
      // case 1: error condition
      if (events[0].events & (EPOLLHUP | EPOLLERR))
      {
        perror("main_epoll_fd error");
        close(events[0].data.fd);
        continue;
      }
      assert(events[0].events & EPOLLIN);

      // case 2: connection request
      if (events[0].data.fd == fd)
      {
        // find index in Client array to add
        for (client_index = 0; client_index < EPOLL_QUEUE_LEN; client_index++)
        {
          if (connection[client_index].bytes_sent == -1)
          {
            break;
          }
        }        

        pthread_mutex_lock(&mutex);
        if (reader_index == -1)
        {
          new_fd = accept(fd, (struct sockaddr*) &connection[client_index].client, &client_len);
          if (new_fd == -1)
          {
            if (errno != EAGAIN && errno != EWOULDBLOCK)
            {
              perror("accept");
            }
            continue;
          }

          maxfd = new_fd + 1;
          connection[new_fd].bytes_sent = 0;
          connection[new_fd].num_requests = 0;

          // find reader thread that will add new_fd to set
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
          printf("%i reader index\n", reader_index);
        }
        pthread_mutex_unlock(&mutex);
      } 
    }
  }

	close(fd);
  for (i = 0; i < THREAD_COUNT; i++)
  {
    free(info_ptr[i]);
  }
  exit(0);
}

void* readerMethod(void* info_ptr)
{
  struct ThreadInfo* thread_info = (struct ThreadInfo*) info_ptr;
  int thread_index = thread_info->thread_index;
  int i, num_fds;
  struct ThreadInfo *child_info_ptr[MAX_THREAD_COUNT];
  struct epoll_event events[MAX_THREAD_COUNT], event;

  // initialize pipe
  if (pipe(pfd[thread_index]) < 0)
  {
    perror("pipe");
    exit(1);
  }

  // allocate memory for arguments to child threads
  for (i = 0; i < MAX_THREAD_COUNT; i++)
  {
    if ((child_info_ptr[i] = malloc(sizeof (struct ThreadInfo))) == NULL)
    {
      perror("malloc");
      exit(1);
    }  
  }

  // initialize client with BASE_THREAD_COUNT threads
  for (i = 0; i < BASE_THREAD_COUNT; i++)
  {
    child_info_ptr[i]->parent_thread_index = thread_index;
    child_info_ptr[i]->thread_index = i;
    pthread_create(&thread_id[thread_index][i], NULL, echo, (void*) child_info_ptr[i]);
    //printf("Created thread %lu %i-%i\n", (unsigned long) thread_id[thread_index][i], thread_index, i);
  }

  // initialize epoll fd
  epoll_fd[thread_index] = epoll_create(MAX_THREAD_COUNT);
  if (epoll_fd[thread_index] == -1)
  {
    perror("epoll_create");
    exit(1);
  }

  // make main_epoll_fd non-blocking
  if (fcntl(main_epoll_fd, F_SETFL, O_NONBLOCK | fcntl(main_epoll_fd, F_GETFL, 0)) == -1)
  {
    perror("fcntl");
    exit(1);
  }

  // add main fd to epoll loop
  event.events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLET;
  event.data.fd = main_epoll_fd;
  if (epoll_ctl(epoll_fd[thread_index], EPOLL_CTL_ADD, main_epoll_fd, &event) == -1)
  {
    perror("epoll_ctl");
    exit(1);
  }

  while (TRUE)
  {
    num_fds = epoll_wait(epoll_fd[thread_index], events, MAX_THREAD_COUNT, -1);
    if (num_fds < 0 && errno != EINTR)
    {
      perror("epoll_wait");
      exit(1);
    }

    for (i = 0; i < num_fds; i++)
    {
      // case 1: error condition
      if (events[i].events & (EPOLLHUP | EPOLLERR))
      {
        perror("epoll_fd error");
        close(events[i].data.fd);
        continue;
      }
      assert(events[i].events & EPOLLIN);

      // case 2: main fd read, new connection
      if (events[i].data.fd == main_epoll_fd)
      {
        pthread_mutex_lock(&mutex);
        // handle new connection
        if (reader_index == thread_index)
        {
          // add new_fd to epoll set
          // make new fd non-blocking
          if (fcntl(new_fd, F_SETFL, O_NONBLOCK | fcntl(new_fd, F_GETFL, 0)) == -1)
          {
            perror("fcntl");
            exit(1);
          }

          // add new fd to epoll loop
          event.data.fd = new_fd;
          if (epoll_ctl(epoll_fd[thread_index], EPOLL_CTL_ADD, new_fd, &event) == -1)
          {
            perror("epoll_ctl");
            exit(1);
          }

          printf("  Remote Address:  %s\n", inet_ntoa(connection[new_fd].client.sin_addr)); 
          reader[thread_index].num_client++;
          reader_index = -1;
        }
        pthread_mutex_unlock(&mutex);
        continue;
      }

      // case 3: read data for fd
      if (write(pfd[thread_index][1], &events[i].data.fd, sizeof(events[i].data.fd)) < 0)
      {
        perror("write pipe");
      }
    }
  
    // compare num_fds (incoming data) w num threads

      // buffer full of clients, create a new child thread
      // change this based on epoll! 
/*      if (reader[thread_index].num_client == reader[thread_index].num_thread && reader[thread_index].num_thread < MAX_THREAD_COUNT)
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
        client[thread_index][new_thread_index].fd = -1;
        client[thread_index][new_thread_index].bytes_sent = 0;
        client[thread_index][new_thread_index].num_requests = 0;
      
        printf("Created thread %lu %i\n", (unsigned long) client[thread_index][new_thread_index].thread_id, reader[thread_index].num_thread);
      }

      FD_SET(new_fd, &allset);
      if (new_fd > maxfd)
      {
        maxfd = new_fd;
      }
      reader_index = -1;
    }

    // close excess threads (buffer of 5)
    if (reader[thread_index].num_thread > BASE_THREAD_COUNT && reader[thread_index].num_client + 5 >= reader[thread_index].num_thread)
    {
      for (i = reader[thread_index].last_index - 1; i < 0; i--)
      {
        if (client[thread_index][i].fd == -1)
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
    }*/
  }

  for (i = 0; i < MAX_THREAD_COUNT; i++)
  {
    free(child_info_ptr[i]);
  }
  return 0;
}

void* echo(void* info_ptr)
{
  struct ThreadInfo* thread_info = (struct ThreadInfo *) info_ptr;
  int thread_index = thread_info->parent_thread_index;

  int fd;
  int n, bytes_to_read;
  char *bp, buf[BUFLEN], pipebuf[sizeof(int)];

  while (TRUE)
  {
    read(pfd[thread_index][0], pipebuf, sizeof(int));
    fd = atoi(pipebuf);

    // delete thread
    if (fd == -1)
    {
      reader[thread_index].num_thread--;
      pthread_exit(NULL);
    }
    else if (fd > 0)
    {
      bp = buf;
      bytes_to_read = BUFLEN;
      
      // loop until entire message received
      while ((n = recv (fd, bp, bytes_to_read, 0)) < BUFLEN)
      {
        bp += n;
        bytes_to_read -= n;
      }
     
      connection[fd].num_requests += 1;
      //printf ("Sending: %s\n", buf);
      send (fd, buf, BUFLEN, 0);
      connection[fd].bytes_sent += BUFLEN;

      /*if (n == 0)
      {
        //printf("%ld, %i - Closing Connection %i\n", (long) getpid(), thread_id, client_count);
        printf("Thread %i-%i completed a connection\n", thread_index, client_index);
        close(fd);
        client[thread_index][client_index].fd = -1;
        reader[thread_index].num_client--;
      }*/
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
  int i;
  for (i = 0; i < maxfd; i++)
  {
    if (connection[i].bytes_sent != -1)
    {
      // print thread_conn[i] info
      printf("%*s:%*i | %*i | %*i\n", 17, time_buffer, 3, (int) tv.tv_usec % 1000, 10, connection[i].num_requests, 23, connection[i].bytes_sent);
      fprintf(file, "%*s:%*i | %*i | %*i\n", 17, time_buffer, 3, (int) tv.tv_usec % 1000, 10, connection[i].num_requests, 23, connection[i].bytes_sent);
    }
  }

  fclose(file);
  return 0;
}

void closeFd(int signo)
{
  close(fd);
  exit(EXIT_SUCCESS);
}
