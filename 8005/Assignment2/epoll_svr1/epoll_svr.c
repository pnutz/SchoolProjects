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
#include <sys/stat.h>
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

// listening socket, largest current fd
int fd, maxfd, buflen;
int num_clients[THREAD_COUNT];
int epoll_fd[THREAD_COUNT + 1];
struct Client connection[EPOLL_QUEUE_LEN]; // index is fd
pthread_t thread_id[THREAD_COUNT + 1];
int fd_pipe[THREAD_COUNT][2];

void* acceptMethod(void*);
void* epollMethod(void*);
static int setupConn(int*);
static int echo(int, int);
static int findFewestClients();
//static long long timeval_diff(struct timeval*, struct timeval*, struct timeval*);
int initOutputFile();
int writeConnections();
void closeFd(int);

int main (int argc, char **argv)
{
	int	i, port;
	struct sockaddr_in server;
  struct ThreadInfo *info_ptr;
  struct sigaction act;

	switch(argc)
	{
		case 1:
			port = SERVER_TCP_PORT;	// use the default port
      buflen = BUFLEN; // use the default buflen
		break;
		case 2:
			port = atoi(argv[1]);	// get user specified port
      buflen = BUFLEN;
		break;
    case 3:
      port = atoi(argv[1]);
      buflen = atoi(argv[2]); // get user specified buffer length
      break;
		default:
			fprintf(stderr, "Usage: %s [port] [buflen]\n", argv[0]);
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
	listen(fd, 128);
  maxfd = fd + 1; 

  // create child threads
  for (i = 0; i < THREAD_COUNT; i++)
  {
    if ((info_ptr = malloc(sizeof (struct ThreadInfo))) == NULL)
    {
      perror("malloc");
      exit(1);
    }
    info_ptr->thread_index = i;
    pthread_create(&thread_id[i], NULL, epollMethod, (void*) info_ptr);
    printf("Created thread %lu %i\n", (unsigned long) thread_id[i], i);
  }

  // create thread for accepting clients
  if ((info_ptr = malloc(sizeof (struct ThreadInfo))) == NULL)
  {
    perror("malloc");
    exit(1);
  }
  info_ptr->thread_index = THREAD_COUNT;
  pthread_create(&thread_id[THREAD_COUNT], NULL, acceptMethod, (void*) info_ptr);
  printf("Created thread %lu %i\n", (unsigned long) thread_id[THREAD_COUNT], THREAD_COUNT);

  //initOutputFile();

  // log outputs to file
  while (TRUE)
  {
  }

	close(fd);
  exit(0);
}

void* acceptMethod(void* info_ptr)
{
  struct ThreadInfo *thread_info = (struct ThreadInfo*) info_ptr;
  int thread_index = thread_info->thread_index;
  // free info_ptr after it was used
  free(info_ptr);

  int num_fds;
  struct epoll_event events[1], event;

  // initialize epoll fd
  epoll_fd[thread_index] = epoll_create(1);
  if (epoll_fd[thread_index] == -1)
  {
    perror("epoll_create");
    exit(1);
  }

  // add all socket fds to epoll loop
  event.events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLET;
  event.data.fd = fd;
  if (epoll_ctl(epoll_fd[thread_index], EPOLL_CTL_ADD, fd, &event) == -1)
  {
    perror("epoll_ctl");
    exit(1);
  }

  while (TRUE)
  {
    num_fds = epoll_wait(epoll_fd[thread_index], events, 1, 0);
    if (num_fds < 0 && errno != EINTR)
    {
      perror("epoll_wait");
      exit(1);
    }
    else if (num_fds > 0)
    {
      // case 1: error condition
      if (events[0].events & (EPOLLHUP | EPOLLERR))
      {
        perror("accept epoll error");
        close(events[0].data.fd);
        continue;
      }
      assert(events[0].events & EPOLLIN);

      // case 2: connection request - check which port the request is coming from
      int new_fd;
      if (setupConn(&new_fd) == 1)
      {
        exit(1);
      }

      int target_thread = findFewestClients();

      // send client & server fd down thread pipe
      printf("write to %i pipe: %i\n", target_thread, new_fd);
      write(fd_pipe[target_thread][1], &new_fd, sizeof(int));
    }
  }
  return 0;
}

void* epollMethod(void* info_ptr)
{
  struct ThreadInfo *thread_info = (struct ThreadInfo*) info_ptr;
  int thread_index = thread_info->thread_index;
  // free info_ptr after it was used
  free(info_ptr);

  int i, new_fd, num_fds;
  struct epoll_event events[THREAD_QUEUE_LEN], event;

  num_clients[thread_index] = 0;

  // initialize fd_pipe for thread
  if (pipe(fd_pipe[thread_index]) < 0)
  {
    perror("pipe call");
    exit(1);
  }

  // set pipe read as non-blocking
  if (fcntl(fd_pipe[thread_index][0], F_SETFL, O_NONBLOCK) < 0)
  {
    perror("fcntl");
    exit(1);
  }

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
        printf("%i fd error - #requests %i, bytes sent %i\n", events[i].data.fd, connection[events[i].data.fd].num_requests, connection[events[i].data.fd].bytes_sent);
        perror("epoll error");
        close(events[i].data.fd);
        continue;
      }
      assert(events[i].events & EPOLLIN);

      // case 2: connection request
      if (events[i].data.fd == fd)
      {
        if (setupConn(&new_fd) == 1)
        {
          exit(1);
        }

        num_clients[thread_index]++;

        // add new fd to epoll loop
        event.data.fd = new_fd;
        if (epoll_ctl(epoll_fd[thread_index], EPOLL_CTL_ADD, new_fd, &event) == -1)
        {
          perror("epoll_ctl");
          exit(1);
        }
        continue;
      }

      // case 3: read data for fd
      echo(events[i].data.fd, thread_index);
    }

    // check pipe for new connections
    while (read(fd_pipe[thread_index][0], &new_fd, sizeof(int)) > 0)
    {
      printf("pipe %i read new_fd %i\n", thread_index, new_fd);

      num_clients[thread_index]++;

      // add new fd to epoll loop
      event.data.fd = new_fd;
      if (epoll_ctl(epoll_fd[thread_index], EPOLL_CTL_ADD, new_fd, &event) == -1)
      {
        perror("epoll_ctl");
        exit(1);
      }
    }
  }
  return 0;
}

// accept client connection
// init variables, modifies new_fd to point to clnt_fd
static int setupConn(int *new_fd)
{
  int clnt_fd;
  struct sockaddr_in client;
  socklen_t client_len = sizeof(struct sockaddr_in);

  clnt_fd = accept(fd, (struct sockaddr*) &client, &client_len);
  if (clnt_fd == -1)
  {
    if (errno != EAGAIN && errno != EWOULDBLOCK)
    {
      perror("accept");
    }
    return 1;
  }

  connection[clnt_fd].client = client;
  connection[clnt_fd].bytes_sent = 0;
  connection[clnt_fd].num_requests = 0;

  // make new fd non-blocking
  if (fcntl(clnt_fd, F_SETFL, O_NONBLOCK | fcntl(clnt_fd, F_GETFL, 0)) == -1)
  {
    perror("fcntl");
    return 1;
  }

  printf("  Remote Address:  %s, %i\n", inet_ntoa(connection[clnt_fd].client.sin_addr), clnt_fd);

  if (maxfd <= clnt_fd)
  {
    maxfd = clnt_fd + 1;
  }

  *new_fd = clnt_fd;
  return 0;
}

static int echo(int recv_fd, int thread_index)
{
  int n, bytes_to_read;
  char *bp, buf[buflen];
  if (gettimeofday(&connection[recv_fd].last_seen, NULL))
  {
    perror("last_seen gettimeofday");
    exit(1);
  }

  bp = buf;
  bytes_to_read = buflen;

  // receive initial buflen of message
  n = recv(recv_fd, bp, bytes_to_read, 0);
  // check if connection is closed
  if (n == 0)
  {
    if (maxfd - 1 == recv_fd)
    {
      maxfd--;
    }
    connection[recv_fd].bytes_sent = -1;
    num_clients[thread_index]--;
    printf("Completed connection for fd %i\n", recv_fd);
    close(recv_fd);
    return 1;
  }

  bp += n;
  bytes_to_read -= n;

  if (n < buflen)
  {
    // loop until entire message received
    while ((n = recv (recv_fd, bp, bytes_to_read, 0)) < buflen)
    {
      bp += n;
      bytes_to_read -= n;
    }
  }
 
  connection[recv_fd].num_requests += 1;
  printf ("Sending: fd %i, request #%i - %s\n", recv_fd, connection[recv_fd].num_requests, buf);
  send (recv_fd, buf, buflen, 0);
  connection[recv_fd].bytes_sent += buflen;
  return 0;
}

// iterates through each worker thread, returning thread index with the lowest number of clients
// number of clients takes into account pipe contents
static int findFewestClients()
{
  int i;
  struct stat st;
  int index = 0;
  if (fstat(fd_pipe[0][0], &st) < 0)
  {
    perror("fstat");
    exit(1);
  }
  int pipe_count = st.st_size/(sizeof(int) * 2);
  int count = num_clients[0] + pipe_count;
  for (i = 1; i < THREAD_COUNT; i++)
  {
    if (fstat(fd_pipe[i][0], &st) < 0)
    {
      perror("fstat");
      exit(1);
    }
    pipe_count = st.st_size/(sizeof(int) * 2);

    if (num_clients[i] + pipe_count < count)
    {
      count = num_clients[i] + pipe_count;
      index = i;
    }
  }
  return index;
}


// calculate difference in time between end_time and start_time (return usec)
/*static long long timeval_diff(struct timeval *difference, struct timeval *end_time, struct timeval *start_time)
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
