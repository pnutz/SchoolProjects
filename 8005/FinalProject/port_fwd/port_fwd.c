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
#include <signal.h>

#define SERVER_TCP_PORT 7000  // Default port
#define BUFLEN	255           // Buffer length
#define TRUE	1
#define THREAD_COUNT 8
#define EPOLL_QUEUE_LEN 25000
#define THREAD_QUEUE_LEN EPOLL_QUEUE_LEN/THREAD_COUNT
#define FILENAME "connections.txt"

// parameter for thread function
struct ThreadInfo {
  int thread_index;
} ThreadInfo;

struct Client {
  struct sockaddr_in client;
  int bytes_sent;
  int num_requests;
  struct timeval last_seen;
} Client;

struct Client connection[EPOLL_QUEUE_LEN]; // index is fd
pthread_t thread_id[THREAD_COUNT];

// listening socket, largest current fd
int fd, maxfd, num_clients;
int epoll_fd[THREAD_COUNT];

void* epollMethod(void*);
static int echo(int);
static long long timeval_diff(struct timeval*, struct timeval*, struct timeval*);
int initOutputFile();
int writeConnections();
void closeFd(int);

int main (int argc, char **argv)
{
	int	i, port;
	struct sockaddr_in server;
  struct ThreadInfo *info_ptr[THREAD_COUNT];
  struct sigaction act;
  struct timeval current_time;

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
  }

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

  // make socket fd non-blocking
  if (fcntl(fd, F_SETFL, O_NONBLOCK | fcntl(fd, F_GETFL, 0)) == -1)
  {
    perror("fcntl");
    exit(1);
  }

	// Listen for connections
	listen(fd, THREAD_COUNT);
  maxfd = fd + 1; 
  num_clients = 0;

  //initOutputFile();

  // create child threads
  for (i = 0; i < THREAD_COUNT; i++)
  {
    info_ptr[i]->thread_index = i;
    pthread_create(&thread_id[i], NULL, epollMethod, (void*) info_ptr[i]);
    printf("Created thread %lu %i\n", (unsigned long) thread_id[i], i);
  }

  // can use this for printing
  // clean up timed out connections
  while (TRUE)
  {
/*    if (gettimeofday(&current_time, NULL))
    {
      perror("current_time gettimeofday");
      exit(1);
    }

    for (i = 0; i < maxfd; i++)
    {
      // timeout occurs if no message in 5 seconds
      if (connection[i].bytes_sent != -1 && timeval_diff(NULL, &current_time, &connection[i].last_seen) > 5000000LL)
      {
        close(i);
        if (maxfd - 1 == i)
        {
          maxfd--;
        }
        connection[i].bytes_sent = -1;
        num_clients--;
        printf("Completed connection for fd %i\n", i);
      }
    }*/
  }

	close(fd);
  for (i = 0; i < THREAD_COUNT; i++)
  {
    free(info_ptr[i]);
  }
  exit(0);
}

void* epollMethod(void* info_ptr)
{
  struct ThreadInfo *thread_info = (struct ThreadInfo*) info_ptr;
  int thread_index = thread_info->thread_index;
  int i, new_fd, num_fds;
  struct epoll_event events[THREAD_QUEUE_LEN], event;
  struct sockaddr_in client;
  socklen_t client_len = sizeof(struct sockaddr_in);

  // initialize epoll fd
  epoll_fd[thread_index] = epoll_create(THREAD_QUEUE_LEN);
  if (epoll_fd[thread_index] == -1)
  {
    perror("epoll_create");
    exit(1);
  }

  // add socket fd to epoll loop
  event.events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLET;
  event.data.fd = fd;
  if (epoll_ctl(epoll_fd[thread_index], EPOLL_CTL_ADD, fd, &event) == -1)
  {
    perror("epoll_ctl");
    exit(1);
  }

  while (TRUE)
  {
    num_fds = epoll_wait(epoll_fd[thread_index], events, THREAD_QUEUE_LEN, -1);
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
        perror("epoll error");
        close(events[i].data.fd);
        continue;
      }
      assert(events[i].events & EPOLLIN);

      // case 2: connection request
      if (events[i].data.fd == fd)
      {
        new_fd = accept(fd, (struct sockaddr*) &client, &client_len);
        if (new_fd == -1)
        {
          if (errno != EAGAIN && errno != EWOULDBLOCK)
          {
            perror("accept");
          }
          continue;
        }

        connection[new_fd].client = client;
        connection[new_fd].bytes_sent = 0;
        connection[new_fd].num_requests = 0;

        if (maxfd <= new_fd)
        {
          maxfd = new_fd + 1;
        }

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
        num_clients++;
        continue;
      }

      // case 3: read data for fd
      echo(events[i].data.fd);
    }
  }
  return 0;
}

static int echo(int fd)
{
  int n, bytes_to_read;
  char *bp, buf[BUFLEN];
  if (gettimeofday(&connection[fd].last_seen, NULL))
  {
    perror("last_seen gettimeofday");
    exit(1);
  }

  bp = buf;
  bytes_to_read = BUFLEN;

  // receive initial BUFLEN of message
  n = recv(fd, bp, bytes_to_read, 0);
  // check if connection is closed
  if (n == 0)
  {
    if (maxfd - 1 == fd)
    {
      maxfd--;
    }
    connection[fd].bytes_sent = -1;
    num_clients--;
    printf("Completed connection for fd %i\n", fd);
    close(fd);
    return 1;
  }

  bp += n;
  bytes_to_read -= n;

  if (n < BUFLEN)
  {
    // loop until entire message received
    while ((n = recv (fd, bp, bytes_to_read, 0)) < BUFLEN)
    {
      bp += n;
      bytes_to_read -= n;
    }
  }
 
  connection[fd].num_requests += 1;
  //printf ("Sending: %s\n", buf);
  send (fd, buf, BUFLEN, 0);
  connection[fd].bytes_sent += BUFLEN;
  return 0;
}

// calculate difference in time between end_time and start_time (return usec)
static long long timeval_diff(struct timeval *difference, struct timeval *end_time, struct timeval *start_time)
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
