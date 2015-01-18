/*-----------------------------------------------------------------------------
--  SOURCE FILE:      tcp_svr.c - A TCP file transfer server program
--
--  PROGRAM:          tcp_svr.exe
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
--  The program will accept TCP connections from client machines.
--  The program will receive a file sent from a client connection.
--  The program will respond to sent packets following TCP packet protocol.
-----------------------------------------------------------------------------*/
#include <stdio.h>
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
#include "log.h"
#include "packet.h"
#include "timer.h"

#define CONFIG_FILE "connections.cfg"
#define DEFAULT_RWND 128
#define SIG SIGUSR1

int main (int argc, char **argv)
{
  // connection variables
	int	bytes_to_read, rwnd;
	int	sd, new_sd, server_port = 0;
  socklen_t client_len;
	struct	sockaddr_in server, client;

  // allocated for logging
  char output[1000];

  // handle receiver window argument
  switch(argc)
  {
    case 2:
      rwnd = atoi(argv[1]);
    break;
    default:
      rwnd = DEFAULT_RWND;
  }
  
  if (rwnd <= 0)
  {
    logerr("Receiver Window must be a positive integer.");
    printf("Receiver Window must be a positive integer.\n");
    exit(1);
  }
  else
  {
    sprintf(output, "Receiver Window is %i mss.", rwnd);
    logstr(output);
    printf("Receiver Window is %i mss.\n", rwnd);
  }

  ConfigData config = parse(CONFIG_FILE);
  if (config.size < 6)
  {
    logerr("tcp_svr requires 6 key-value configurations.");
    printf("tcp_svr requires 6 key-value configurations.\n");
    exit(1);
  }

  int i;
  for (i = 0; i < config.size; i++)
  {
    // set receiver port
    if (strcmp(config.kv[i].key, "receiver_port") == 0)
    {
      printf("receiver_port found.\n");
      server_port = atoi(config.kv[i].value);
      // not a valid integer
      if (server_port == 0)
      {
        logerr("receiver_port value configuration is not an integer.");
        printf("receiver_port value configuration is not an integer.\n");
        exit(1);
      }
    }
  }
  
  // error check configurations
  if (server_port == 0)
  {
    logerr("receiver_port key-value configuration not found.");
    printf("receiver_port key-value configuration not found.\n");
    exit(1);
  }

  // Create a stream socket
	if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
	{
    sprintf(output, "Can't create a socket: %s", strerror(errno));
    logerr(output);
		perror ("Can't create a socket");
		exit(1);
	}

	// Bind an address to the socket
	bzero((char *)&server, sizeof(struct sockaddr_in));
	server.sin_family = AF_INET;
	server.sin_port = htons(server_port);
	server.sin_addr.s_addr = htonl(INADDR_ANY); // Accept connections from any client

	if (bind(sd, (struct sockaddr *)&server, sizeof(server)) == -1)
	{
    sprintf(output, "Can't bind name to socket: %s", strerror(errno));
    logerr(output);
		perror("Can't bind name to socket");
		exit(1);
	}

	// Listen for connections

	// queue up to 5 connect requests
	listen(sd, 5);

  client_len = sizeof(client);
  if ((new_sd = accept (sd, (struct sockaddr *)&client, &client_len)) == -1)
  {
    sprintf(output, "Can't accept client");
    logerr(output);
    fprintf(stderr, "Can't accept client\n");
    exit(1);
  }

  char* ip_address = inet_ntoa(client.sin_addr);
  unsigned short client_port = client.sin_port;
  
  sprintf(output, "Remote Address:  %s", ip_address);
  logstr(output);
  printf("%s\n", output);
  sprintf(output, "Remote Port:  %d", client_port);
  logstr(output);
  printf("%s\n", output);

  // variables for tcp
  // receiving
  int expected_seq_num; // next seq # to be acked
  int last_acked_seq;
  char *phase = "3WAY HS";

  WindowSegment *first = 0; // beginning of window
  WindowSegment *last; // end of window, easy to add onto
  WindowSegment *conductor; // for traversing linked list
  WindowSegment *prev_conductor; // for getting prev node 

  char filename[PAYLOADLEN];
  memset(filename, 0, PAYLOADLEN);

  // sending
  int send_base;
  int next_seq_num;

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
    sprintf(output, "Can't create packet timer: %s", strerror(errno));
    logerr(output);
    perror("Can't create packet timer");
    exit(1);
  }
  
  // timer expiration
  its.it_value.tv_sec = 2; // seconds
  its.it_value.tv_nsec = 0; // nanoseconds (1 billion in a second)
  // timer period, does not repeat
  its.it_interval.tv_sec = 0;
  its.it_interval.tv_nsec = 0;

  disarm.it_value.tv_sec = 0;
  disarm.it_value.tv_nsec = 0;
  disarm.it_interval.tv_sec = 0;
  disarm.it_interval.tv_nsec = 0;

  // variables for polling
  struct pollfd ufds[1];
  ufds[0].fd = new_sd;
  ufds[0].events = POLLIN; // check only for normal data

  // log table header
  sprintf(output, "SEND/RCV | Type    | SeqNum     | AckNum     | Length | Phase   | Window | #Buffered | Timeout Interval");
  logstr(output);
  printf("%s\n", output);
  sprintf(output, "_______________________________________________________________________________________________________");
  logstr(output);
  printf("%s\n", output);

  // 3-way handshake
  // SYN
  // random initial sequnce number - server_isn
  srand(time(NULL));
  send_base = rand();
  next_seq_num = send_base + 1;
  Packet received;
  bytes_to_read = sizeof(Packet);

  while (received.PacketType != SYN)
  {
    recv(new_sd, &received, bytes_to_read, 0);
  }

  sprintf(output, "%*s | %*s | %*i | %*i | %*i | %*s | %*i | %*i | %is %insec", 8, "RCV", 7, getPacketType(received.PacketType), 10, received.SeqNum, 10, received.AckNum, 6, received.DataLength, 7, phase, 6, received.WindowSize, 9, 1, (int) its.it_value.tv_sec, (int) its.it_value.tv_nsec);
  logstr(output);
  printf("%s\n", output);

  // SYN-ACK
  last_acked_seq = received.SeqNum;
  expected_seq_num = received.SeqNum + received.DataLength;
  Packet sent = { .PacketType = SYNACK, .SeqNum = send_base, .data[0] = 0, .DataLength = 1, .WindowSize = rwnd, .AckNum = expected_seq_num };

  sprintf(output, "%*s | %*s | %*i | %*i | %*i | %*s | %*i | %*i | %is %insec", 8, "SEND", 7, getPacketType(sent.PacketType), 10, sent.SeqNum, 10, sent.AckNum, 6, sent.DataLength, 7, phase, 6, sent.WindowSize, 9, 0, (int) its.it_value.tv_sec, (int) its.it_value.tv_nsec);
  logstr(output);
  printf("%s\n", output);
 
  // send and start timer
  send(new_sd, (void *) &sent, bytes_to_read, 0);
  armTimer();

  // ACK
  // while loop is to wait for a valid ack/data packet (implies connection open)
  while ((received.PacketType != ACK || received.AckNum != next_seq_num) && (received.PacketType != DATA || received.SeqNum != expected_seq_num + 1))
  {
    if (timeout == 1)
    {
      sprintf(output, "Timeout: Retransmit");
      logstr(output);
      printf("%s\n", output);

      sprintf(output, "%*s | %*s | %*i | %*i | %*i | %*s | %*i | %*i | %is %insec", 8, "SEND", 7, getPacketType(sent.PacketType), 10, sent.SeqNum, 10, sent.AckNum, 6, sent.DataLength, 7, phase, 6, sent.WindowSize, 9, 0, (int) its.it_value.tv_sec, (int) its.it_value.tv_nsec);
      logstr(output);
      printf("%s\n", output);

      send(new_sd, (void *) &sent, bytes_to_read, 0);
      timeout = 0;
      armTimer();
    }
    else if (poll(ufds, 1, 0) > 0)
    {
      recv(new_sd, &received, bytes_to_read, 0);
    }
  }
  disarmTimer();

  if (received.PacketType == ACK)
  {
    last_acked_seq = received.SeqNum;
    expected_seq_num = received.SeqNum + received.DataLength;
  }
  else
  {
    strcpy(filename, received.data);
    printf("Received filename DATA: %s\n", filename);
    phase = "-------";
  }

  sprintf(output, "%*s | %*s | %*i | %*i | %*i | %*s | %*i | %*i | %is %insec", 8, "RCV", 7, getPacketType(received.PacketType), 10, received.SeqNum, 10, received.AckNum, 6, received.DataLength, 7, phase, 6, received.WindowSize, 9, 1, (int) its.it_value.tv_sec, (int) its.it_value.tv_nsec);
  logstr(output);
  printf("%s\n", output);

  while (filename[0] == 0)
  {
    if (poll(ufds, 1, 0) > 0)
    {
      recv(new_sd, &received, bytes_to_read, 0);
      if (received.PacketType == DATA && received.SeqNum == expected_seq_num)
      {
        strcpy(filename, received.data);
        printf("Received filename DATA: %s\n", filename);
        phase = "-------";

        sprintf(output, "%*s | %*s | %*i | %*i | %*i | %*s | %*i | %*i | %is %insec", 8, "RCV", 7, getPacketType(received.PacketType), 10, received.SeqNum, 10, received.AckNum, 6, received.DataLength, 7, phase, 6, received.WindowSize, 9, 1, (int) its.it_value.tv_sec, (int) its.it_value.tv_nsec);
        logstr(output);
        printf("%s\n", output);
      }
    }
  } 
  last_acked_seq = received.SeqNum;
  expected_seq_num = received.SeqNum + received.DataLength;

  FILE *fp;
  // receive file from client
  fp = fopen(filename, "w");
  if (fp == NULL)
  {
    sprintf(output, "Can't save sent file: %s", strerror(errno));
    logerr(output);
    perror("Can't save sent file");
    exit(1);
  }
  printf("Opened %s successfully\n", filename);

  // prep for receiving data
  its.it_value.tv_sec = 0; // seconds
  its.it_value.tv_nsec = 500000; // nanoseconds (set to 500 usec)

  armTimer();

  sent.PacketType = ACK;
  sent.data[0] = 0;
  sent.DataLength = 1;
  sent.SeqNum = 1;
  sent.WindowSize = rwnd;

  int receivedFin = 0;
  // receiving while loop
  while (receivedFin == 0)
  {
    // send on timeout
    if (timeout == 1)
    {
      send(new_sd, (void *) &sent, bytes_to_read, 0);
      timeout = 0;

      sprintf(output, "%*s | %*s | %*i | %*i | %*i | %*s | %*i | %*i | %is %insec", 8, "SEND", 7, getPacketType(sent.PacketType), 10, sent.SeqNum, 10, sent.AckNum, 6, sent.DataLength, 7, phase, 6, sent.WindowSize, 9, rwnd - sent.WindowSize, (int) its.it_value.tv_sec, (int) its.it_value.tv_nsec);
      logstr(output);
      printf("%s\n", output);
    }
    else if (poll(ufds, 1, 0) > 0)
    {
      recv(new_sd, &received, bytes_to_read, 0);

      sprintf(output, "%*s | %*s | %*i | %*i | %*i | %*s | %*i | %*i | %is %insec", 8, "RCV", 7, getPacketType(received.PacketType), 10, received.SeqNum, 10, received.AckNum, 6, received.DataLength, 7, phase, 6, received.WindowSize, 9, rwnd - sent.WindowSize, (int) its.it_value.tv_sec, (int) its.it_value.tv_nsec);
      logstr(output);
      printf("%s\n", output);

      // update ack for packet
      if (received.SeqNum == expected_seq_num)
      {
        last_acked_seq = expected_seq_num;
        expected_seq_num += received.DataLength;
        sent.AckNum = expected_seq_num;

        int write_data;
        if (received.PacketType == FIN)
        {
          receivedFin = 1;
        }
        else
        {
          // write data to file
          write_data = fwrite(received.data, sizeof(char), received.DataLength, fp);
          if (write_data < received.DataLength)
          {
            sprintf(output, "Failed receiving file: %s", strerror(errno));
            logerr(output);
            perror("Failed receiving file");
            exit(1);
          }
        }
        
        // iterate through receive window for in-order packets
        conductor = first;
        if (conductor != 0)
        {
          while (conductor->next != 0)
          {
            if (conductor->packet.SeqNum == expected_seq_num)
            {
              if (conductor->packet.PacketType == FIN)
              {
                receivedFin = 1;
              }
              else
              {
                // write data to file
                write_data = fwrite(conductor->packet.data, sizeof(char), conductor->packet.DataLength, fp);
                if (write_data < conductor->packet.DataLength)
                {
                  sprintf(output, "Failed receiving file: %s", strerror(errno));
                  logerr(output);
                  perror("Failed receiving file");
                  exit(1);
                }
              }

              last_acked_seq = expected_seq_num;
              expected_seq_num += conductor->packet.DataLength;
              sent.AckNum = expected_seq_num;

              // remove WindowSegment from buffer
              if (conductor == first)
              {
                conductor = conductor->next;
                free(first);
                first = conductor;
              }
              else
              {
                prev_conductor->next = conductor->next;
                free(conductor);
                conductor = prev_conductor->next;
              }
              sent.WindowSize++;
            }
            else
            {
              prev_conductor = conductor;
              conductor = conductor->next;
            }
          }
          
          // check last WindowSegment
          if (conductor->packet.SeqNum == expected_seq_num)
          {
            if (conductor->packet.PacketType == FIN)
            {
              receivedFin = 1;
            }
            else
            {
              // write data to file
              write_data = fwrite(conductor->packet.data, sizeof(char), conductor->packet.DataLength, fp);
              if (write_data < conductor->packet.DataLength)
              {
                sprintf(output, "Failed receiving file: %s", strerror(errno));
                logerr(output);
                perror("Failed receiving file");
                exit(1);
              }
            }

            last_acked_seq = expected_seq_num;
            expected_seq_num += conductor->packet.DataLength;
            sent.AckNum = expected_seq_num;

            // first = last
            if (conductor == first)
            {
              free(first);
              first = 0;
            }
            else
            {
              free(last);
              last = prev_conductor;
              last->next = 0;
            }
            sent.WindowSize++;
          }
        }

        // start 500 usec timer to receive more packets if timer is disarmed
        timer_gettime(timerid, &remaining);
        if (remaining.it_value.tv_sec == 0 && remaining.it_value.tv_nsec == 0)
        {
          armTimer();
          printf("Started 500 usec timer before sending ACK\n");
        }
        else // if timer is armed, disarm and send packet
        {
          disarmTimer();
          send(new_sd, (void *) &sent, bytes_to_read, 0);

          sprintf(output, "%*s | %*s | %*i | %*i | %*i | %*s | %*i | %*i | %is %insec", 8, "SEND", 7, getPacketType(sent.PacketType), 10, sent.SeqNum, 10, sent.AckNum, 6, sent.DataLength, 7, phase, 6, sent.WindowSize, 9, rwnd - sent.WindowSize, (int) its.it_value.tv_sec, (int) its.it_value.tv_nsec);
          logstr(output);
          printf("%s\n", output);
        }
      }
      // out of order packet - buffer and send latest ack
      else if (received.SeqNum > expected_seq_num)
      {
        disarmTimer();
        sent.AckNum = expected_seq_num;
        send(new_sd, (void *) &sent, bytes_to_read, 0);

        sprintf(output, "%*s | %*s | %*i | %*i | %*i | %*s | %*i | %*i | %is %insec", 8, "SEND", 7, getPacketType(sent.PacketType), 10, sent.SeqNum, 10, sent.AckNum, 6, sent.DataLength, 7, phase, 6, sent.WindowSize, 9, rwnd - sent.WindowSize, (int) its.it_value.tv_sec, (int) its.it_value.tv_nsec);
        logstr(output);
        printf("%s\n", output);

        // buffer packet if there is enough space in rwnd
        if (sent.WindowSize > 0)
        {
          // window is empty
          if (first == 0)
          {
            first = (WindowSegment *) malloc(sizeof(WindowSegment));
            first->next = 0;
            first->packet = received;

            last = first;            
          }
          else // add to end of buffer
          {
            WindowSegment *new;
            new = (WindowSegment *) malloc(sizeof(WindowSegment));
            new->packet = received;
            new->next = 0;
            last->next = new;
            last = new;
          }
          sent.WindowSize--;
        }
      }
      // this shouldn't happen - send latest ack
      else
      {
        sent.AckNum = expected_seq_num;
        send(new_sd, (void *) &sent, bytes_to_read, 0);

        sprintf(output, "%*s | %*s | %*i | %*i | %*i | %*s | %*i | %*i | %is %insec", 8, "SEND", 7, getPacketType(sent.PacketType), 10, sent.SeqNum, 10, sent.AckNum, 6, sent.DataLength, 7, phase, 6, sent.WindowSize, 9, rwnd - sent.WindowSize, (int) its.it_value.tv_sec, (int) its.it_value.tv_nsec);
        logstr(output);
        printf("%s\n", output);
      }
    }
  }

  fseek(fp, 0, SEEK_END);
  if (ftell(fp) > 0)
  {
    sprintf(output, "Finished receiving %s", filename);
    logstr(output);
    printf("Finished receiving %s\n", filename);
  }
  else
  {
    sprintf(output, "Failed receiving file: %s", strerror(errno));
    logerr(output);
    perror("Failed receiving file");
    exit(1);
  }

  fclose(fp);
  fp = NULL;

  // send ack
  send(new_sd, (void *) &sent, bytes_to_read, 0);

  sprintf(output, "%*s | %*s | %*i | %*i | %*i | %*s | %*i | %*i | %is %insec", 8, "SEND", 7, getPacketType(sent.PacketType), 10, sent.SeqNum, 10, sent.AckNum, 6, sent.DataLength, 7, phase, 6, sent.WindowSize, 9, rwnd - sent.WindowSize, (int) its.it_value.tv_sec, (int) its.it_value.tv_nsec);
  logstr(output);
  printf("%s\n", output);

  // FIN
  send_base = next_seq_num;
  sent.PacketType = FIN;
  sent.SeqNum = send_base;
  sent.AckNum = 1;
  next_seq_num += sent.DataLength;

  its.it_value.tv_sec = 2; // seconds
  its.it_value.tv_nsec = 0; // nanoseconds (1 billion in a second)

  sprintf(output, "%*s | %*s | %*i | %*i | %*i | %*s | %*i | %*i | %is %insec", 8, "SEND", 7, getPacketType(sent.PacketType), 10, sent.SeqNum, 10, sent.AckNum, 6, sent.DataLength, 7, phase, 6, sent.WindowSize, 9, 0, (int) its.it_value.tv_sec, (int) its.it_value.tv_nsec);
  logstr(output);
  printf("%s\n", output);

  // send and start timer
  send(new_sd, (void *) &sent, bytes_to_read, 0);
  armTimer();

  while (received.PacketType != ACK || received.AckNum != next_seq_num)
  {
    if (timeout == 1)
    {
      sprintf(output, "Timeout: Retransmit");
      logstr(output);
      printf("%s\n", output);

      sprintf(output, "%*s | %*s | %*i | %*i | %*i | %*s | %*i | %*i | %is %insec", 8, "SEND", 7, getPacketType(sent.PacketType), 10, sent.SeqNum, 10, sent.AckNum, 6, sent.DataLength, 7, phase, 6, sent.WindowSize, 9, 0, (int) its.it_value.tv_sec, (int) its.it_value.tv_nsec);
      logstr(output);
      printf("%s\n", output);

      send(new_sd, (void *) &sent, bytes_to_read, 0);
      timeout = 0;
      armTimer();
    }
    else if (poll(ufds, 1, 0) > 0)
    {
      recv(new_sd, &received, bytes_to_read, 0);
    }
  }

  disarmTimer();
  sprintf(output, "%*s | %*s | %*i | %*i | %*i | %*s | %*i | %*i | %is %insec", 8, "RCV", 7, getPacketType(received.PacketType), 10, received.SeqNum, 10, received.AckNum, 6, received.DataLength, 7, phase, 6, received.WindowSize, 9, rwnd - sent.WindowSize, (int) its.it_value.tv_sec, (int) its.it_value.tv_nsec);
  logstr(output);
  printf("%s\n", output);

  logstr("Connection closed");
  printf("Connection closed\n");

  close(new_sd);
  close(sd);
	return(0);
}
