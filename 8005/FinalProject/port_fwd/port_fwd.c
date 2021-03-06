/*---------------------------------------------------------------------------------------
--	SOURCE FILE:		epoll_svr.c -   A simple port forward server using epoll
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
-- The program will read data from the client socket and simply forward it to destination.
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

#define BUFLEN	5000           // Buffer length
#define TRUE	1
#define THREAD_COUNT 8
#define EPOLL_QUEUE_LEN 80000
#define THREAD_QUEUE_LEN EPOLL_QUEUE_LEN/THREAD_COUNT
#define FILENAME "port_fwd_connections.txt"

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

struct PrintData {
  int recv_fd;
  int send_fd;
  int num_requests;
  int bytes_sent;
} PrintData;

int epoll_fd[THREAD_COUNT + 1];
int num_clients[THREAD_COUNT];
struct Client connection[EPOLL_QUEUE_LEN]; // index is fd
struct EndPointFd end_point[EPOLL_QUEUE_LEN]; // index is fd
pthread_t thread_id[THREAD_COUNT + 1];
int fd_pipe[THREAD_COUNT][2];
int out_pipe[2];

void* acceptMethod(void*);
void* epollMethod(void*);
static int setupConn(int, int*);
static int forward(int, int);
static int findFewestClients();
//static long long timeval_diff(struct timeval*, struct timeval*, struct timeval*);
FILE* initOutputFile();
int writeConnection(FILE*, int, int, int, int);
void closeFd(int);

int main (int argc, char **argv)
{
	int	i, fd;
	struct sockaddr_in server;
  struct ThreadInfo *info_ptr;
  struct sigaction act;

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
    listen(fd, 128);

    // associate fd with port forward table
    port_config[i].fd = fd;
  }

  // initialize out pipe
  if (pipe(out_pipe) < 0)
  {
    perror("pipe call");
    exit(1);
  }

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

  // create thread for accepting clients
  if ((info_ptr = malloc(sizeof (struct ThreadInfo))) == NULL)
  {
    perror("malloc");
    exit(1);
  }
  info_ptr->thread_index = THREAD_COUNT;
  pthread_create(&thread_id[THREAD_COUNT], NULL, acceptMethod, (void*) info_ptr);
  printf("Created thread %lu %i\n", (unsigned long) thread_id[THREAD_COUNT], THREAD_COUNT);

  FILE *file;
  if ((file = initOutputFile()) == NULL)
  {
    perror("file");
    exit(1);
  }

  // log outputs to file
  struct PrintData *print_data = malloc(sizeof(*print_data));
  while (TRUE)
  {
    // read pipe
    if (read(out_pipe[0], print_data, sizeof(*print_data)) > 0)
    {
      writeConnection(file, print_data->recv_fd, print_data->send_fd, print_data->num_requests, print_data->bytes_sent);
    }
  }

  fclose(file);
	close(fd);
  freePortFwdTable();
  exit(0);
}

void* acceptMethod(void* info_ptr)
{
  struct ThreadInfo *thread_info = (struct ThreadInfo*) info_ptr;
  int thread_index = thread_info->thread_index;
  // free info_ptr after it was used
  free(info_ptr);

  int i, j, num_fds, conn, new_fd[2];
  struct epoll_event events[num_port_fwd], event;

  // initialize epoll fd
  epoll_fd[thread_index] = epoll_create(num_port_fwd);
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
    num_fds = epoll_wait(epoll_fd[thread_index], events, num_port_fwd, -1);
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
        perror("accept epoll error");
        close(events[i].data.fd);
        continue;
      }
      assert(events[i].events & EPOLLIN);

      // case 2: connection request - check which port the request is coming from
      for (j = 0; j < num_port_fwd; j++)
      {
        if (events[i].data.fd == port_config[j].fd)
        {
          while (TRUE)
          {
            if ((conn = setupConn(j, new_fd)) == -1)
            {
              exit(1);
            }
            else if (conn == 0)
            {
              int target_thread = findFewestClients();

              // send client & server fd down thread pipe
              printf("write to %i pipe: %i, %i\n", target_thread, new_fd[0], new_fd[1]);
              write(fd_pipe[target_thread][1], &new_fd[0], sizeof(int));
              write(fd_pipe[target_thread][1], &new_fd[1], sizeof(int));
            }
            else
            {
              break;
            }
          }
          break;
        }
      }
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

  int i, j, clnt_fd, svr_fd, num_fds, conn, fwd_flag, new_fd[2];
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
    num_fds = epoll_wait(epoll_fd[thread_index], events, THREAD_QUEUE_LEN, 0);
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
      fwd_flag = 1;
      for (j = 0; j < num_port_fwd; j++)
      {
        if (events[i].data.fd == port_config[j].fd)
        {
          fwd_flag = 0;
          while (TRUE)
          {
            if ((conn = setupConn(j, new_fd)) == -1)
            {
              exit(1);
            }
            else if (conn == 0)
            {
              num_clients[thread_index]++;

              clnt_fd = new_fd[0];
              svr_fd = new_fd[1];

              // add new fd to epoll loop
              event.data.fd = clnt_fd;
              if (epoll_ctl(epoll_fd[thread_index], EPOLL_CTL_ADD, clnt_fd, &event) == -1)
              {
                perror("epoll_ctl");
                exit(1);
              }

              // add new fd to epoll loop
              event.data.fd = svr_fd;
              if (epoll_ctl(epoll_fd[thread_index], EPOLL_CTL_ADD, svr_fd, &event) == -1)
              {
                perror("epoll_ctl");
                exit(1);
              }
            }
            else
            {
              break;
            }
          }
          break;
        }
      }

      // case 3: read data for fd
      if (fwd_flag == 1)
      {
        forward(events[i].data.fd, thread_index);
      }
    }

    // check pipe for new connections
    while (read(fd_pipe[thread_index][0], &clnt_fd, sizeof(int)) > 0)
    {
      // read associated svr_fd (or wait for accept thread to write it)
      while (read(fd_pipe[thread_index][0], &svr_fd, sizeof(int)) == 0)
      {
      }
      printf("pipe %i read clnt_fd %i, svr_fd %i\n", thread_index, clnt_fd, svr_fd);

      num_clients[thread_index]++;

      // add new fd to epoll loop
      event.data.fd = clnt_fd;
      if (epoll_ctl(epoll_fd[thread_index], EPOLL_CTL_ADD, clnt_fd, &event) == -1)
      {
        perror("epoll_ctl");
        exit(1);
      }

      // add new fd to epoll loop
      event.data.fd = svr_fd;
      if (epoll_ctl(epoll_fd[thread_index], EPOLL_CTL_ADD, svr_fd, &event) == -1)
      {
        perror("epoll_ctl");
        exit(1);
      }
    }
  }
  return 0;
}

// accept client connection, connect to server connection
// init variables, modifies new_fd to point to array of int: clnt_fd, svr_fd
// returns 0 if successful, 1 if accept would block, and -1 if an error occurred
static int setupConn(int config_index, int *new_fd)
{
  int clnt_fd, svr_fd;
  struct sockaddr_in client, server;
  socklen_t client_len = sizeof(struct sockaddr_in);
  struct addrinfo hints, *res, *rp;

  clnt_fd = accept(port_config[config_index].fd, (struct sockaddr*) &client, &client_len);
  if (clnt_fd == -1)
  {
    if (errno != EAGAIN && errno != EWOULDBLOCK)
    {
      perror("accept");
      return -1;
    }
    else
    {
      return 1;
    }
  }

  connection[clnt_fd].client = client;
  connection[clnt_fd].bytes_sent = 0;
  connection[clnt_fd].num_requests = 0;

  // make new fd non-blocking
  if (fcntl(clnt_fd, F_SETFL, O_NONBLOCK | fcntl(clnt_fd, F_GETFL, 0)) == -1)
  {
    perror("fcntl");
    return -1;
  }

  printf("  Remote Address:  %s\n", inet_ntoa(connection[clnt_fd].client.sin_addr));

  // create server socket
  if ((svr_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
  {
    perror("Cannot create socket");
    return -1;
  }

  bzero((char *)&server, sizeof(struct sockaddr_in));
  server.sin_family = AF_INET;
  server.sin_port = htons(port_config[config_index].svr_port);

  // replacing gethostbyname
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  getaddrinfo(port_config[config_index].svr_addr, NULL, &hints, &res);

  for (rp = res; rp != NULL; rp = rp->ai_next)
  {
    struct sockaddr_in* saddr = (struct sockaddr_in*) rp->ai_addr;
    server.sin_addr = saddr->sin_addr;
 
    // Connecting to the server
    if (connect (svr_fd, (struct sockaddr *)&server, sizeof(server)) == -1)
    {
      fprintf(stderr, "Can't connect to server\n");
      perror("connect");
      exit(1);
    }
    break;
  }
  freeaddrinfo(res);

  connection[svr_fd].client = server;
  connection[svr_fd].bytes_sent = 0;
  connection[svr_fd].num_requests = 0;

  // make new fd non-blocking
  if (fcntl(svr_fd, F_SETFL, O_NONBLOCK | fcntl(svr_fd, F_GETFL, 0)) == -1)
  {
    perror("fcntl");
    return -1;
  }

  printf("  Destination Address:  %s\n", inet_ntoa(connection[svr_fd].client.sin_addr));

  // store client-server fd for sending purposes
  end_point[clnt_fd].is_client = 1;
  end_point[clnt_fd].alt_fd = svr_fd;

  end_point[svr_fd].is_client = 0;
  end_point[svr_fd].alt_fd = clnt_fd;

  new_fd[0] = clnt_fd;
  new_fd[1] = svr_fd;

  return 0;
}

static int forward(int recv_fd, int thread_index)
{
  int n, bytes_to_read;
  char *bp, buf[BUFLEN];
  if (gettimeofday(&connection[recv_fd].last_seen, NULL))
  {
    perror("last_seen gettimeofday");
    return 1;
  }

  bp = buf;
  bytes_to_read = BUFLEN;

  // receive initial BUFLEN of message
  n = recv(recv_fd, bp, bytes_to_read, 0);
  // check if connection is closed
  if (n == 0)
  {
    connection[recv_fd].bytes_sent = -1;
    printf("Completed connection for %s fd %i\n", (end_point[end_point[recv_fd].alt_fd].is_client) ? "client":"server", recv_fd);
    close(recv_fd);

    connection[end_point[recv_fd].alt_fd].bytes_sent = -1;
    printf("Completed connection for %s fd %i\n", (end_point[recv_fd].is_client) ? "client":"server", end_point[recv_fd].alt_fd);
    close(end_point[recv_fd].alt_fd);

    num_clients[thread_index]--;
    return 0;
  }

  bp += n;
  bytes_to_read -= n;

  if (n < BUFLEN)
  {
    // loop until entire message received or buffer is full
    while ((n = recv (recv_fd, bp, bytes_to_read, 0)) < bytes_to_read && n != -1)
    {
      bp += n;
      bytes_to_read -= n;
    }
  }
 
  connection[end_point[recv_fd].alt_fd].num_requests += 1;
  //printf ("Sending: fd %i, request #%i - %s\n", end_point[recv_fd].alt_fd, connection[end_point[recv_fd].alt_fd].num_requests, buf);
  send (end_point[recv_fd].alt_fd, buf, BUFLEN - bytes_to_read, 0);
  connection[end_point[recv_fd].alt_fd].bytes_sent += BUFLEN - bytes_to_read;

  struct PrintData *data = malloc(sizeof(*data));
  data->recv_fd = recv_fd;
  data->send_fd = end_point[recv_fd].alt_fd;
  data->num_requests = connection[end_point[recv_fd].alt_fd].num_requests;
  data->bytes_sent = connection[end_point[recv_fd].alt_fd].bytes_sent;
  write(out_pipe[1], data, sizeof(*data));
  free(data);
  return 0;
}

// iterates through each worker thread, returning thread index with the lowest number of clients
// number of clients takes into account pipe contents
static int findFewestClients()
{
  int i;
  int index = 0;
  int count = num_clients[0];
  for (i = 1; i < THREAD_COUNT; i++)
  {
    if (num_clients[i] < count)
    {
      count = num_clients[i];
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

FILE* initOutputFile()
{
  FILE *file;
  if ((file = fopen(FILENAME, "w")) == NULL)
  {
    printf("Can't open output file: %s\n", FILENAME);
    return NULL;
  }

  fprintf(file, "Time                  | ReceiveFD | SendFD | # Requests | Bytes Sent\n");
  fprintf(file, "____________________________________________________________________\n");
  return file;
}

int writeConnection(FILE *file, int recv_fd, int send_fd, int num_requests, int bytes_sent)
{
  time_t timer;
  char time_buffer[25];
  struct tm *tm_info;
  struct timeval tv;

  time(&timer);
  tm_info = localtime(&timer);
  strftime(time_buffer, 25, "%D %T", tm_info);

  gettimeofday(&tv, 0);

  // write connection details
  printf("%*s:%*i | %*i | %*i | %*i | %*i\n", 17, time_buffer, 3, (int) tv.tv_usec % 1000, 9, recv_fd, 6, send_fd, 10, num_requests, 10, bytes_sent);
  fprintf(file, "%*s:%*i | %*i | %*i | %*i | %*i\n", 17, time_buffer, 3, (int) tv.tv_usec % 1000, 9, recv_fd, 6, send_fd, 10, num_requests, 10, bytes_sent);
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
