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
#include <netdb.h>
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
//#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <assert.h>
#include <signal.h>
#include <fcntl.h>

#include "port_fwd_reader.c"

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

struct EndPointFd {
  int is_client;
  int alt_fd;
} EndPointFd;

struct Client connection[EPOLL_QUEUE_LEN]; // index is fd
struct EndPointFd end_point[EPOLL_QUEUE_LEN]; // index is fd
pthread_t thread_id[THREAD_COUNT];

// listening socket, largest current fd
int maxfd, num_clients;
int epoll_fd[THREAD_COUNT];

void* epollMethod(void*);
static int echo(int);
static long long timeval_diff(struct timeval*, struct timeval*, struct timeval*);
int initOutputFile();
int writeConnections();
void closeFd(int);

int main (int argc, char **argv)
{
	int	i, fd;
	struct sockaddr_in server;
  struct ThreadInfo *info_ptr;
  struct sigaction act;
  struct timeval current_time;

  // setup the signal handler to close the server socket when CTRL-c is received
  act.sa_handler = closeFd;
  act.sa_flags = 0;
  if ((sigemptyset(&act.sa_mask) == -1 || sigaction(SIGINT, &act, NULL) == -1))
  {
    perror("Failed to set SIGINT handler");
    exit(1);
  }

  // read port forward table
  if ((i = readPortFwdTable()) == -1)
  {
    perror("port forward table");
    exit(1);
  }

  // initialize connections
  for (i = 0; i < EPOLL_QUEUE_LEN; i++)
  {
    connection[i].bytes_sent = -1;
  }

  // setup server address
  memset(&server, 0, sizeof(struct sockaddr_in));
  server.sin_family = AF_INET;
  server.sin_addr.s_addr = htonl(INADDR_ANY); // accept connections from any client

	// Create stream sockets for each incoming port
  for (i = 0; i < num_port_fwd; i++)
  {
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

    // set port for socket and bind address to the socket
    server.sin_port = htons(port_config[i].rcv_port);

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

    // listen for connections
    listen(fd, THREAD_COUNT);

    // associate fd with port forward table
    port_config[i].fd = fd;
  }

  maxfd = fd + 1; 
  num_clients = 0;

  //initOutputFile();

  // create child threads
  for (i = 0; i < THREAD_COUNT; i++)
  {
    // allocate info_ptr for each thread
    if ((info_ptr = malloc(sizeof (struct ThreadInfo))) == NULL)
    {
      perror("malloc");
      exit(1);
    }

    info_ptr->thread_index = i;
    pthread_create(&thread_id[i], NULL, epollMethod, (void*) info_ptr);
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
  freePortFwdTable();
  exit(0);
}

void* epollMethod(void* info_ptr)
{
  struct ThreadInfo *thread_info = (struct ThreadInfo*) info_ptr;
  int thread_index = thread_info->thread_index;
  // free info_ptr after it was used
  free(info_ptr);

  int i, j, clnt_fd, svr_fd, num_fds, echo_flag;
  struct epoll_event events[THREAD_QUEUE_LEN], event;
  struct sockaddr_in client, server;
  struct hostent *hp;
  socklen_t client_len = sizeof(struct sockaddr_in);

  bzero((char *)&server, sizeof(struct sockaddr_in));
  server.sin_family = AF_INET;

  // initialize epoll fd
  epoll_fd[thread_index] = epoll_create(THREAD_QUEUE_LEN);
  if (epoll_fd[thread_index] == -1)
  {
    perror("epoll_create");
    exit(1);
  }

  // add all socket fds to epoll loop
  event.events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLET;

  for (i = 0; i < num_port_fwd; i++)
  {
    event.data.fd = port_config[i].fd;
    if (epoll_ctl(epoll_fd[thread_index], EPOLL_CTL_ADD, port_config[i].fd, &event) == -1)
    {
      perror("epoll_ctl");
      exit(1);
    }
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

      // case 2: connection request - check which port the request is coming from
      echo_flag = 1;
      for (j = 0; j < num_port_fwd; j++)
      {
        if (events[i].data.fd == port_config[j].fd)
        {
          echo_flag = 0;
          clnt_fd = accept(port_config[j].fd, (struct sockaddr*) &client, &client_len);
          if (clnt_fd == -1)
          {
            if (errno != EAGAIN && errno != EWOULDBLOCK)
            {
              perror("accept");
            }
            break;
          }

          connection[clnt_fd].client = client;
          connection[clnt_fd].bytes_sent = 0;
          connection[clnt_fd].num_requests = 0;

          if (maxfd <= clnt_fd)
          {
            maxfd = clnt_fd + 1;
          }

          // make new fd non-blocking
          if (fcntl(clnt_fd, F_SETFL, O_NONBLOCK | fcntl(clnt_fd, F_GETFL, 0)) == -1)
          {
            perror("fcntl");
            exit(1);
          }

          // add new fd to epoll loop
          event.data.fd = clnt_fd;
          if (epoll_ctl(epoll_fd[thread_index], EPOLL_CTL_ADD, clnt_fd, &event) == -1)
          {
            perror("epoll_ctl");
            exit(1);
          }

          printf("  Remote Address:  %s\n", inet_ntoa(connection[clnt_fd].client.sin_addr));
          num_clients++;

          // create server socket
          if ((svr_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
          {
            perror("Cannot create socket");
            exit(1);
          }

          server.sin_port = htons(port_config[j].svr_port);
          if ((hp = gethostbyname(port_config[j].svr_addr)) == NULL)
          {
            fprintf(stderr, "Unknown server address\n");
            exit(1);
          }
          bcopy(hp->h_addr, (char *)&server.sin_addr, hp->h_length);

          // open new connection to port forward server
          if (connect(svr_fd, (struct sockaddr *)&server, sizeof(server)) == -1)
          {
            fprintf(stderr, "Can't connect to server\n");
            perror("connect");
            exit(1);
          }

          connection[svr_fd].client = server;
          connection[svr_fd].bytes_sent = 0;
          connection[svr_fd].num_requests = 0;

          if (maxfd <= svr_fd)
          {
            maxfd = svr_fd + 1;
          }

          // make new fd non-blocking
          if (fcntl(svr_fd, F_SETFL, O_NONBLOCK | fcntl(svr_fd, F_GETFL, 0)) == -1)
          {
            perror("fcntl");
            exit(1);
          }

          // add new fd to epoll loop
          event.data.fd = svr_fd;
          if (epoll_ctl(epoll_fd[thread_index], EPOLL_CTL_ADD, svr_fd, &event) == -1)
          {
            perror("epoll_ctl");
            exit(1);
          }

          printf("  Remote Address:  %s\n", inet_ntoa(connection[svr_fd].client.sin_addr));


          // store client-server fd for sending purposes
          end_point[clnt_fd].is_client = 1;
          end_point[clnt_fd].alt_fd = svr_fd;

          end_point[svr_fd].is_client = 0;
          end_point[svr_fd].alt_fd = clnt_fd;
          break;
        }
      }

      // case 3: read data for fd
      if (echo_flag == 1)
      {
        echo(events[i].data.fd);
      }
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
    printf("Completed connection for fd %i\n", fd);
    close(fd);

    if (maxfd - 1 == end_point[fd].alt_fd)
    {
      maxfd--;
    }
    connection[end_point[fd].alt_fd].bytes_sent = -1;
    printf("Completed connection for fd %i\n", end_point[fd].alt_fd);
    close(end_point[fd].alt_fd);

    num_clients--;
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
 
  connection[end_point[fd].alt_fd].num_requests += 1;
  printf ("Sending: fd %i, request #%i - %s\n", end_point[fd].alt_fd, connection[end_point[fd].alt_fd].num_requests, buf);
  send (end_point[fd].alt_fd, buf, BUFLEN, 0);
  connection[end_point[fd].alt_fd].bytes_sent += BUFLEN;
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
  int i;
  for (i = 0; i < num_port_fwd; i++)
  {
    close(port_config[i].fd);
  }
  freePortFwdTable();
  exit(EXIT_SUCCESS);
}
