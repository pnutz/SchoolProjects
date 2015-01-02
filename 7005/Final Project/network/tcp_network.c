/*-----------------------------------------------------------------------------
--  SOURCE FILE:      tcp_network.c - A simple file packet relay using TCP
--
--  PROGRAM:          tcp_network.exe
--
--  FUNCTIONS:        Berkeley Socket API
--
--  DATE:             December 2, 2014
--
--  REVISIONS:        (Date and Description)
--
--  DESIGNERS:        Christopher Eng
--
--  PROGRAMMERS:      Christopher Eng
--
--  NOTES:
--  The program will accept TCP connections from a client machine.
--  The program will read and connect to a stored server ip and port configuration.
--  The program will relay packets sent from one connection to the other
--  after waiting for argument Delay usec.
--  The program will not relay a percentage of packets based on argument Bit Error Rate.
-----------------------------------------------------------------------------*/
#include <stdio.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <sys/poll.h>

#include "config.h"
#include "packet.h"
#include "timer.h"

#define CONFIG_FILE "connections.cfg"
#define SIG SIGUSR1
#define RCV 0
#define SEND 1

int main (int argc, char **argv)
{
  // connection variables
	int	bytes_to_read = sizeof(Packet), delay_nsec, delay_sec = 0;
	int	sd, sender_sd, receiver_sd, receiver_port = 0, network_port = 0;
  float ber;
  char str[16];
  char *receiver_ip = NULL, **pptr;
  struct hostent *hp;
  socklen_t client_len;
	struct	sockaddr_in server, sender_client, receiver_client;

  // handle bit error rate and delay arguments
  switch(argc)
  {
    case 3:
      ber = atof(argv[1]);
      delay_nsec = atoi(argv[2]); // in usec
    break;
    default:
      fprintf(stderr, "Usage: %s bit_error_rate delay_usec\n", argv[0]);
      exit(1);
  }
 
  // carry over sec from usec
  while (delay_nsec >= NSEC_TO_SEC/1000)
  {
    delay_sec++;
    delay_nsec -= NSEC_TO_SEC/1000;
  }
  // convert usec to nsec
  delay_nsec *= 1000;
 
  // timer cannot be 0 or it will not start
  if (delay_sec == 0 && delay_nsec == 0)
  {
    delay_nsec = 1;
  }

  ConfigData config = parse(CONFIG_FILE);
  if (config.size < 6)
  {
    printf("tcp_network requires 6 key-value configurations.\n");
    exit(1);
  }

  int i;
  for (i = 0; i < config.size; i++)
  {
    // set network port
    if (strcmp(config.kv[i].key, "network_port") == 0)
    {
      network_port = atoi(config.kv[i].value);
      // not a valid integer
      if (network_port == 0)
      {
        printf("network_port value configuration is not an integer.\n");
        exit(1);
      }
    }
    else if (strcmp(config.kv[i].key, "receiver_port") == 0)
    {
      receiver_port = atoi(config.kv[i].value);
      // not a valid integer
      if (receiver_port == 0)
      {
        printf("receiver_port value configuration is not an integer.\n");
        exit(1);
      }
    }
    else if (strcmp(config.kv[i].key, "receiver_ip") == 0)
    {
      receiver_ip = config.kv[i].value;
    }
  }
  
  // error check configurations
  if (network_port == 0)
  {
    printf("network_port key-value configuration not found.\n");
    exit(1);
  }
  else if (receiver_port == 0)
  {
    printf("receiver_port key-value configuration not found.\n");
    exit(1);
  }
  else if (receiver_ip == NULL)
  {
    printf("receiver_ip key-value configuration not found.\n");   
    exit(1);
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
	server.sin_port = htons(network_port);
	server.sin_addr.s_addr = htonl(INADDR_ANY); // Accept connections from any client

	if (bind(sd, (struct sockaddr *)&server, sizeof(server)) == -1)
	{
		perror("Can't bind name to socket");
		exit(1);
	}

	// Listen for connections

	// queue up to 5 connect requests
	listen(sd, 5);

  client_len = sizeof(sender_client);
  if ((sender_sd = accept (sd, (struct sockaddr *)&sender_client, &client_len)) == -1)
  {
    fprintf(stderr, "Can't accept sender\n");
    exit(1);
  }

  char* ip_address = inet_ntoa(sender_client.sin_addr);
  unsigned short client_port = sender_client.sin_port;
  
  printf("Remote Address:  %s\n", ip_address);
  printf("Remote Port:  %d\n", client_port);

  // create stream socket
  if ((receiver_sd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
  {
		perror ("Can't create a socket");
		exit(1);
  }

  bzero((char *)&receiver_client, sizeof(struct sockaddr_in));
  receiver_client.sin_family = AF_INET;
  receiver_client.sin_port = htons(receiver_port);
  if ((hp = gethostbyname(receiver_ip)) == NULL)
  {
    fprintf(stderr, "Unknown receiver address\n");
    exit(1);
  }
  bcopy(hp->h_addr, (char *)&receiver_client.sin_addr, hp->h_length);

  // connect to receiver
  if (connect (receiver_sd, (struct sockaddr *)&receiver_client, sizeof(receiver_client)) == -1)
  {
    fprintf(stderr, "Can't connect to receiver\n");
    perror("connect");
    exit(1);
  }
  pptr = hp->h_addr_list;
  printf("Connected    IP Address: %s\n", inet_ntop(hp->h_addrtype, *pptr, str, sizeof(str)));

  // storage tools
  Packet received;
  WindowSegment *new;
  WindowSegment *first = 0; // beginning of window
  WindowSegment *last; // end of window, easy to add onto
  WindowSegment *conductor; // for traversing linked list

  int packets_on_network = 0;

  srand(time(NULL) + 20000);

  // variables for timer
  struct itimerspec remaining;
  struct sigevent sev;
  struct sigaction sa;

  printf("Establishing handler for signal %d\n", SIG);
  sa.sa_flags = SA_SIGINFO;
  sa.sa_sigaction = handler;
  sigemptyset(&sa.sa_mask); // initializes sa_mask signal set, preventing them from being blocked
  sigaction(SIG, &sa, NULL);

  sev.sigev_notify = SIGEV_SIGNAL; // notify when timer ends signal set at sigev_signo
  sev.sigev_signo = SIG; // notification signal
  sev.sigev_value.sival_ptr = &timerid; // pass timer with notification
  if (timer_create(CLOCKID, &sev, &timerid) == -1)
  {
    perror("Can't create packet timer");
    exit(1);
  }
  
  // timer expiration
  its.it_value.tv_sec = delay_sec; // seconds
  its.it_value.tv_nsec = delay_nsec; // nanoseconds (1 billion in a second)
  // timer period, does not repeat
  its.it_interval.tv_sec = 0;
  its.it_interval.tv_nsec = 0;

  // variables for polling
  struct pollfd ufds[2];
  ufds[0].fd = sender_sd;
  ufds[0].events = POLLIN; // check only for normal data
  ufds[1].fd = receiver_sd;
  ufds[1].events = POLLIN; // check only for normal data

  while (1)
  {
    // send packet on timeout and remove from linked list
    if (timeout == 1)
    {
      if (first->sd == SEND)
      {
        send(sender_sd, (void *) &first->packet, bytes_to_read, 0);
        printf("Sent packet to sender, SeqNum: %i\n", first->packet.SeqNum);
      }
      else
      {
        send(receiver_sd, (void *) &first->packet, bytes_to_read, 0);
        printf("Sent packet to receiver, SeqNum: %i\n", first->packet.SeqNum);
      }

      conductor = first;
      conductor = conductor->next;
      if (last == first)
      {
        last = conductor;
      }
      free(first);
      first = conductor;

      packets_on_network--;
      timeout = 0;

      if (first != 0)
      {
        its.it_value.tv_sec = first->delay_sec;
        its.it_value.tv_nsec = first->delay_nsec;
        armTimer();
      }
      else
      {
        its.it_value.tv_sec = delay_sec;
        its.it_value.tv_nsec = delay_nsec;
      }
    }
    else if (poll(ufds, 2, 0) > 0 && (ufds[0].revents || ufds[1].revents))
    {
      // ignore packet if BER is hit
      float ber_test = (float) rand() / (float) RAND_MAX;
      if (ber_test >= ber)
      {
        new = (WindowSegment *) malloc(sizeof(WindowSegment));

        if (ufds[0].revents & POLLIN)
        {
          recv(sender_sd, &received, bytes_to_read, 0);
          new->sd = RCV; // send to receiver
          printf("Received packet from sender, SeqNum: %i\n", received.SeqNum);
        }
        else if (ufds[1].revents & POLLIN)
        {
          recv(receiver_sd, &received, bytes_to_read, 0);
          new->sd = SEND; // send to sender
          printf("Received packet from receiver, AckNum: %i\n", received.AckNum);
        }

        new->packet = received;
        new->next = 0;

        if (first == 0)
        {
          new->delay_sec = 0;
          new->delay_nsec = 0;
          first = new;
          last = first;

          armTimer();
        }
        else
        {
          timer_gettime(timerid, &remaining);

          new->delay_sec = 0;
          new->delay_nsec = 0;
          int *result_sec = &new->delay_sec;
          int *result_nsec = &new->delay_nsec;
          subtractTime(result_sec, result_nsec, delay_sec, delay_nsec, (int) remaining.it_value.tv_sec, (int) remaining.it_value.tv_nsec);        

          // loop through all packets, subtracting their delay
          conductor = first;
          while (conductor->next != 0)
          {
            conductor = conductor->next;
            subtractTime(result_sec, result_nsec, new->delay_sec, new->delay_nsec, conductor->delay_sec, conductor->delay_nsec);
          }

          last->next = new;
          last = new;
        }
        packets_on_network++;
      }
      else
      {
        if (ufds[0].revents & POLLIN)
        {
          recv(sender_sd, &received, bytes_to_read, 0);
          printf("Bit Error: Dropped packet from sender, SeqNum: %i\n", received.SeqNum);
        }
        else if (ufds[1].revents & POLLIN)
        {
          recv(receiver_sd, &received, bytes_to_read, 0);
          printf("Bit Error: Dropped packet from receiver, AckNum: %i\n", received.AckNum);
        }
      }
    }
  }

  printf("Connection closed\n");

  close(sender_sd);
  close(receiver_sd);
  close(sd);
	return(0);
}
