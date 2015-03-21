/*-----------------------------------------------------------------------------
--  SOURCE FILE:      tcp_clnt.c - A TCP file transfer client program
--
--  PROGRAM:          tcp_clnt.exe
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
--  The program will establish a TCP connection to a stored server ip and port configuration.
--  The user is required to enter a parameter for filename.
--  The program will send the file described by filename to the server, following proper TCP packet protocol.
--  The program will handle dropped packets, following TCP protocol.
-----------------------------------------------------------------------------*/
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <time.h>
#include <signal.h>
#include <sys/poll.h>

#include "config.h"
#include "log.h"
#include "packet.h"
#include "timer.h"

#define CONFIG_FILE "connections.cfg"
#define SIG SIGUSR1

int main (int argc, char **argv)
{
  // connection variables
	int sd, port = 0;
	struct hostent	*hp;
	struct sockaddr_in server;
	char  *host = NULL, **pptr;

  // allocated for logging
  char output[1000];

  // file variables
	char *filename;
  FILE *fp;

  // handle filename argument
	switch(argc)
	{
		case 2:
			filename = argv[1];
		break;
		default:
			fprintf(stderr, "Usage: %s filename\n", argv[0]);
			exit(1);
	}

  // load config file
  ConfigData config = parse(CONFIG_FILE);
  if (config.size < 6)
  {
    logerr("tcp_clnt requires 6 key-value configurations.");
    printf("tcp_clnt requires 6 key-value configurations.\n");
    exit(1);
  }
  
  int i;
  for (i = 0; i < config.size; i++)
  {
    // set receiver host
    if (strcmp(config.kv[i].key, "network_ip") == 0)
    {
      printf("network_ip key found.\n");
      host = config.kv[i].value;
    }
    // set receiver port
    else if (strcmp(config.kv[i].key, "network_port") == 0)
    {
      printf("network_port found.\n");
      port = atoi(config.kv[i].value);
      // not a valid integer
      if (port == 0)
      {
        logerr("network_port value configuration is not an integer.");
        printf("network_port value configuration is not an integer.\n");
        exit(1);
      }
    }
  }
  
  // error check configurations
  if (host == NULL)
  {
    logerr("network_ip key-value configuration not found.");
    printf("network_ip key-value configuration not found.\n");
    exit(1);
  }
  else if (port == 0)
  {
    logerr("network_port key-value configuration not found.");
    printf("network_port key-value configuration not found.\n");
    exit(1);
  }

  fp = fopen(filename, "r");
  if (fp == NULL)
  {
    sprintf(output, "Can't access file: %s", strerror(errno));
    logerr(output);
    perror("Can't access file");
    exit(1);
  }

  fseek(fp, 0, SEEK_END);
  if (ftell(fp) > 0)
  {
    sprintf(output, "Opened file %s successfully", filename);
    logstr(output);
    printf("Opened file %s successfully\n", filename);
    fseek(fp, 0, SEEK_SET);
  }
  else
  {
    sprintf(output, "File does not exist: %s", strerror(errno));
    logerr(output);
    perror("File does not exist");
    exit(1);
  }

	// Create the socket
	if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
	{
    sprintf(output, "Can't create socket: %s", strerror(errno));
    logerr(output);
		perror("Can't create socket");
		exit(1);
	}

	bzero((char *)&server, sizeof(struct sockaddr_in));
	server.sin_family = AF_INET;
	server.sin_port = htons(port);
	if ((hp = gethostbyname(host)) == NULL)
	{
    sprintf(output, "Unknown server address");
    logerr(output);
		fprintf(stderr, "Unknown server address\n");
		exit(1);
	}
	bcopy(hp->h_addr, (char *)&server.sin_addr, hp->h_length);

	// Connecting to the server
	if (connect (sd, (struct sockaddr *)&server, sizeof(server)) == -1)
	{
    sprintf(output, "Can't connect to server: %s", strerror(errno));
    logerr(output);
		fprintf(stderr, "Can't connect to server\n");
		perror("connect");
		exit(1);
	}

  char str[16]; 
	pptr = hp->h_addr_list;
  sprintf(output, "Connected:    IP Address: %s", inet_ntop(hp->h_addrtype, *pptr, str, sizeof(str)));
  logstr(output);
	printf("Connected:    IP Address: %s\n", str);

  // variables for tcp
  int send_base; // sequence number
  int next_seq_num;
  int seq_ack_count = 0; // count 3 duplicate acks
  int num_pkt_sent = 1; // # packets in window sent
  int last_ack_rcvd = 0;
  int cwnd = 1; // congestion window, 1 MSS
  int ssthresh = 0;
  int window_size = 1;
  int current_window_size = 1; // to track how many mss to add to window
  char *phase = "3WAY HS";

  WindowSegment *first; // beginning of window
  WindowSegment *last; // end of window, easy to add onto
  WindowSegment *conductor; // for traversing linked list
  WindowSegment *prev_conductor; // for getting prev node
  first = (WindowSegment *) malloc(sizeof(WindowSegment));
  first->next = 0;
  last = first;

  // variables for timer
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
  estimatedRTT = 250000000;
  devRTT = 250000000;

  its.it_value.tv_sec = 1; // seconds
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
  ufds[0].fd = sd;
  ufds[0].events = POLLIN; // check only for normal data

  // variables for sending
	int n, bytes_to_read;
  bytes_to_read = sizeof(Packet);

  // log table header
  sprintf(output, "SEND/RCV | Type    | SeqNum     | AckNum     | Length | Phase   | Window | # UnACKed | Timeout Interval");
  logstr(output);
  printf("%s\n", output);
  sprintf(output, "_______________________________________________________________________________________________________");
  logstr(output);
  printf("%s\n", output);

  // 3-way handshake
  // SYN
  // random initial sequence number - client_isn
  // 10000 added to seed since tcp_svr uses time as a seed (too similar)
  srand(time(NULL) + 10000);
  send_base = rand();
  first->packet = (Packet) { .PacketType = SYN, .SeqNum = send_base, .data[0] = 0, .DataLength = 1, .WindowSize = 1, .AckNum = 0 };
  next_seq_num = send_base + first->packet.DataLength;

  sprintf(output, "%*s | %*s | %*i | %*i | %*i | %*s | %*i | %*i | %is %insec", 8, "SEND", 7, getPacketType(first->packet.PacketType), 10, first->packet.SeqNum, 10, first->packet.AckNum, 6, first->packet.DataLength, 7, phase, 6, first->packet.WindowSize, 9, num_pkt_sent, (int) its.it_value.tv_sec, (int) its.it_value.tv_nsec);
  logstr(output);
  printf("%s\n", output);

  // send and start timer
  send (sd, (void *) &first->packet, bytes_to_read, 0);
  armTimer();

  // SYN-ACK
  Packet received;
  // while loop is to wait for a valid synack packet
  while (received.PacketType != SYNACK || received.AckNum != next_seq_num)
  {
    if (timeout == 1)
    {
      send(sd, (void *) &first->packet, bytes_to_read, 0);
      timeout = 0;
      doubleTimeoutInterval();

      sprintf(output, "Timeout: Retransmit");
      logstr(output);
      printf("%s\n", output);

      sprintf(output, "%*s | %*s | %*i | %*i | %*i | %*s | %*i | %*i | %is %insec", 8, "SEND", 7, getPacketType(first->packet.PacketType), 10, first->packet.SeqNum, 10, first->packet.AckNum, 6, first->packet.DataLength, 7, phase, 6, first->packet.WindowSize, 9, num_pkt_sent, (int) its.it_value.tv_sec, (int) its.it_value.tv_nsec);
      logstr(output);
      printf("%s\n", output);

      armTimer();
    }
    else if (poll(ufds, 1, 0) > 0)
    {
      recv(sd, &received, bytes_to_read, 0);
    }
  }
  
  struct itimerspec remaining;
  timer_gettime(timerid, &remaining);
  disarmTimer();
  calculateTimeoutInterval((int) remaining.it_value.tv_sec, (int) remaining.it_value.tv_nsec);

  sprintf(output, "%*s | %*s | %*i | %*i | %*i | %*s | %*i | %*i | %is %insec", 8, "RCV", 7, getPacketType(received.PacketType), 10, received.SeqNum, 10, received.AckNum, 6, received.DataLength, 7, phase, 6, received.WindowSize, 9, 0, (int) its.it_value.tv_sec, (int) its.it_value.tv_nsec);
  logstr(output);
  printf("%s\n", output);

  // ACK
  send_base = next_seq_num;
  first->packet = (Packet) { .PacketType = ACK, .SeqNum = send_base, .data[0] = 0, .DataLength = 1, .WindowSize = window_size, .AckNum = received.SeqNum + received.DataLength };
  next_seq_num += first->packet.DataLength;

  // send
  send(sd, (void *) &first->packet, bytes_to_read, 0);

  sprintf(output, "%*s | %*s | %*i | %*i | %*i | %*s | %*i | %*i | %is %insec", 8, "SEND", 7, getPacketType(first->packet.PacketType), 10, first->packet.SeqNum, 10, first->packet.AckNum, 6, first->packet.DataLength, 7, phase, 6, first->packet.WindowSize, 9, num_pkt_sent, (int) its.it_value.tv_sec, (int) its.it_value.tv_nsec);
  logstr(output);
  printf("%s\n", output);

  // start sending data
  send_base = next_seq_num;
  first->packet = (Packet) { .PacketType = DATA, .SeqNum = send_base, .DataLength = strlen(filename), .WindowSize = window_size, .AckNum = 1 };
  strcpy(first->packet.data, filename);
  next_seq_num += first->packet.DataLength;
  phase = "SLOW ST";

  sprintf(output, "%*s | %*s | %*i | %*i | %*i | %*s | %*i | %*i | %is %insec", 8, "SEND", 7, getPacketType(first->packet.PacketType), 10, first->packet.SeqNum, 10, first->packet.AckNum, 6, first->packet.DataLength, 7, phase, 6, first->packet.WindowSize, 9, num_pkt_sent, (int) its.it_value.tv_sec, (int) its.it_value.tv_nsec);
  logstr(output);
  printf("%s\n", output);

  // send and start timer
  send (sd, (void *) &first->packet, bytes_to_read, 0);
  armTimer();

  // variables for reading from file
  int num_char_read;
  char file_buf[PAYLOADLEN];
  bzero(file_buf, PAYLOADLEN);

  int finAdded = 0;
  // handle response until server wants to close connection (FIN)
  while (received.PacketType != FIN)
  {
    // timeout - retransmit un-acked packets
    if (timeout == 1)
    {
      phase = "SLOW ST";
      timeout = 0;
      ssthresh = window_size / 2;
      window_size = 1;
      num_pkt_sent = 1;
      doubleTimeoutInterval();
      first->packet.WindowSize = window_size;

      send(sd, (void *) &first->packet, bytes_to_read, 0);
      armTimer();

      sprintf(output, "Timeout: Retransmit");
      logstr(output);
      printf("%s\n", output);

      sprintf(output, "%*s | %*s | %*i | %*i | %*i | %*s | %*i | %*i | %is %insec", 8, "SEND", 7, getPacketType(first->packet.PacketType), 10, first->packet.SeqNum, 10, first->packet.AckNum, 6, first->packet.DataLength, 7, phase, 6, first->packet.WindowSize, 9, num_pkt_sent, (int) its.it_value.tv_sec, (int) its.it_value.tv_nsec);
      logstr(output);
      printf("%s\n", output);

    }
    else if (poll(ufds, 1, 0) > 0)
    {
      recv(sd, &received, bytes_to_read, 0);

      sprintf(output, "%*s | %*s | %*i | %*i | %*i | %*s | %*i | %*i | %is %insec", 8, "RCV", 7, getPacketType(received.PacketType), 10, received.SeqNum, 10, received.AckNum, 6, received.DataLength, 7, "-------", 6, received.WindowSize, 9, 0, (int) its.it_value.tv_sec, (int) its.it_value.tv_nsec);
      logstr(output);
      printf("%s\n", output);

      // duplicate ack
      if (received.AckNum == last_ack_rcvd)
      {
        seq_ack_count++;

        // 3 duplicate acks - fast retransmit
        if (seq_ack_count == 3)
        {
          phase = "SLOW ST";
          seq_ack_count = 0;
          window_size /= 2;
          ssthresh = window_size;
          window_size += 3;
          first->packet.WindowSize = window_size;

          send(sd, (void *) &first->packet, bytes_to_read, 0);

          sprintf(output, "3 Duplicate ACKs: Fast Retransmit");
          logstr(output);
          printf("%s\n", output);

          sprintf(output, "%*s | %*s | %*i | %*i | %*i | %*s | %*i | %*i | %is %insec", 8, "SEND", 7, getPacketType(first->packet.PacketType), 10, first->packet.SeqNum, 10, first->packet.AckNum, 6, first->packet.DataLength, 7, phase, 6, first->packet.WindowSize, 9, num_pkt_sent, (int) its.it_value.tv_sec, (int) its.it_value.tv_nsec);
          logstr(output);
          printf("%s\n", output);
        }
      }
      // packets acked
      else if (received.AckNum > last_ack_rcvd)
      {
        timer_gettime(timerid, &remaining);
        disarmTimer();
        calculateTimeoutInterval((int) remaining.it_value.tv_sec, (int) remaining.it_value.tv_nsec);

        // iterate through window removing acked packets
        last_ack_rcvd = received.AckNum;
        conductor = first;
        while (conductor != 0 && conductor->packet.SeqNum < last_ack_rcvd)
        {
          // remove WindowSegment from buffer
          conductor = conductor->next;
          if (last == first)
          {
            last = conductor;
          }
          free(first);
          first = conductor;
          current_window_size--;
          num_pkt_sent--;
        }

        if (received.PacketType == FIN)
        {
          break;
        }

        // slow start
        if (ssthresh == 0 || ssthresh > window_size)
        {
          phase = "SLOW ST";
          window_size *= 2;
        }
        // congestion avoidance
        else
        {
          phase = "CONG AV";
          window_size += 1;
        }

        WindowSegment *new;
        // add packets to window
        while (fp != NULL && current_window_size < window_size)
        {
          if (received.WindowSize != 0)
          {
            num_char_read = fread(file_buf, sizeof(char), PAYLOADLEN, fp);
          }
          else // send 1 byte packets if receiver window size is 0
          {
            sprintf(output, "Receiver Window Is Limiting Data Sent");
            logstr(output);
            printf("%s\n", output);
            num_char_read = fread(file_buf, sizeof(char), 1, fp);
          }

          if (num_char_read > 0)
          {
            send_base = next_seq_num;
            next_seq_num += num_char_read;

            new = (WindowSegment *) malloc(sizeof(WindowSegment));
            new->packet = (Packet) { .PacketType = DATA, .SeqNum = send_base, .WindowSize = window_size, .AckNum = 1 };
            strcpy(new->packet.data, file_buf);
            new->packet.DataLength = num_char_read;
            new->next = 0;

            // empty buffer
            if (first == 0)
            {
              first = new;
              last = first;
            }
            else // add to end of buffer
            {
              last->next = new;
              last = new;
            }

            current_window_size++;
            bzero(file_buf, PAYLOADLEN);
          }
          else
          {
            last->packet.PacketType = EOT;

            fclose(fp);
            fp = NULL;
          }
        }

        // add fin to window if fp is NULL and window has room and not already added
        if (fp == NULL && current_window_size < window_size && finAdded == 0)
        {
          send_base = next_seq_num;
          new = (WindowSegment *) malloc(sizeof(WindowSegment));
          new->packet = (Packet) { .PacketType = FIN, .SeqNum = send_base, .data[0] = 0, .DataLength = 1, .WindowSize = window_size, .AckNum = 1 };
          new->next = 0;

          // empty buffer
          if (first == 0)
          {
            first = new;
            last = first;
          }
          else // add to end of buffer
          {
            last->next = new;
            last = new;
          }

          next_seq_num += new->packet.DataLength;
          current_window_size++;

          finAdded = 1;
        }

        // send packets from window
        int i;
        int num_pkts_to_send;
        // if timeout, current_window_size can be > window_size
        if (current_window_size > window_size)
        {
          num_pkts_to_send = window_size - num_pkt_sent;
        }
        else // if end of transmission, window can be only partially full
        {
          num_pkts_to_send = current_window_size - num_pkt_sent;
        }

        conductor = first;
        for (i = 0; i < num_pkt_sent; i++)
        {
          if (conductor != 0)
          {
            conductor = conductor->next;
          }
        }
        for (i = 0; i < num_pkts_to_send; i++)
        {
          if (conductor != 0)
          {
            conductor->packet.WindowSize = window_size;
            num_pkt_sent++;
            send(sd, (void *) &conductor->packet, bytes_to_read, 0);

            sprintf(output, "%*s | %*s | %*i | %*i | %*i | %*s | %*i | %*i | %is %insec", 8, "SEND", 7, getPacketType(conductor->packet.PacketType), 10, conductor->packet.SeqNum, 10, conductor->packet.AckNum, 6, conductor->packet.DataLength, 7, phase, 6, conductor->packet.WindowSize, 9, num_pkt_sent, (int) its.it_value.tv_sec, (int) its.it_value.tv_nsec);
            logstr(output);
            printf("%s\n", output);

            conductor = conductor->next;
          }
        }
        armTimer();
      }
    } 
  }

  // ACK
  Packet sent = { .PacketType = ACK, .SeqNum = 1, .data[0] = 0, .DataLength = 1, .WindowSize = 1, .AckNum = received.SeqNum + 1 };
  phase = "CLOSE";
  num_pkt_sent = 1;

  sprintf(output, "%*s | %*s | %*i | %*i | %*i | %*s | %*i | %*i | %is %insec", 8, "SEND", 7, getPacketType(sent.PacketType), 10, sent.SeqNum, 10, sent.AckNum, 6, sent.DataLength, 7, phase, 6, sent.WindowSize, 9, num_pkt_sent, (int) its.it_value.tv_sec, (int) its.it_value.tv_nsec);
  logstr(output);
  printf("%s\n", output);

  // send and start timer
  send(sd, (void *) &sent, bytes_to_read, 0);
  received.PacketType = EOT;
  armTimer();

  // wait until timeout without having to retransmit packets to close
  while (timeout != 1)
  {
    if (received.PacketType == FIN)
    {
      disarmTimer();
      sprintf(output, "%*s | %*s | %*i | %*i | %*i | %*s | %*i | %*i | %is %insec", 8, "RCV", 7, getPacketType(received.PacketType), 10, received.SeqNum, 10, received.AckNum, 6, received.DataLength, 7, "-------", 6, received.WindowSize, 9, 0, (int) its.it_value.tv_sec, (int) its.it_value.tv_nsec);
      logstr(output);
      printf("%s\n", output);

      send(sd, (void *) &sent, bytes_to_read, 0);
      armTimer();

      sprintf(output, "Duplicate FIN: Retransmit");
      logstr(output);
      printf("%s\n", output);

      sprintf(output, "%*s | %*s | %*i | %*i | %*i | %*s | %*i | %*i | %is %insec", 8, "SEND", 7, getPacketType(sent.PacketType), 10, sent.SeqNum, 10, sent.AckNum, 6, sent.DataLength, 7, phase, 6, sent.WindowSize, 9, num_pkt_sent, (int) its.it_value.tv_sec, (int) its.it_value.tv_nsec);
      logstr(output);
      printf("%s\n", output);

      received.PacketType = EOT;
    }
    else if (poll(ufds, 1, 0) > 0)
    {
      recv(sd, &received, bytes_to_read, 0);
    }
  }

  logstr("Connection closed");
  printf("Connection closed\n");

  close(sd);
	return (0);
}
