/*---------------------------------------------------------------------------------------
--	SOURCE FILE:		tcp_clnt.c - A simple TCP file transfer client program.
--
--	PROGRAM:		tclnt.exe
--
--	FUNCTIONS:		Berkeley Socket API
--
--	DATE:			January 23, 2001
--
--	REVISIONS:		(Date and Description)
--				September 2014
--        Modification of Aman Abdulla's Berkeley Socket API tcp_clnt.c echo server.
--        Altered client functionality to receive files from and send files to server.
--
--	DESIGNERS:		Christopher Eng
--
--	PROGRAMMERS:		Christopher Eng
--
--	NOTES:
--	The program will establish a TCP connection to a user specifed server.
-- 	The server can be specified using a fully qualified domain name or an
--	IP address. The user is required to enter parameters for function (GET/SEND) and filename.
-- 	The program will send these parameters to the TCP server and receive a new connection from the server.
--  The program will then use this new connect to receive or send stated filename to the server.
---------------------------------------------------------------------------------------*/
#include <stdio.h>
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

#define SERVER_TCP_PORT		7005	// Server & default client port
#define BUFLEN			512  	// Buffer length
#define GET         0
#define SEND        1

void convertToUpperCase(char*);

int main (int argc, char **argv)
{
	int n, bytes_to_read;
	int sd, port;
	struct hostent	*hp;
	struct sockaddr_in server;
	char  *host, *bp, rbuf[BUFLEN], **pptr;
	char str[16];

	int get_flag;
	char *func;
	char *filename;
  FILE *fp;

	switch(argc)
	{
		case 4:
			host =	argv[1];	// Host name
			func = 	argv[2];
			filename = argv[3];
			port = SERVER_TCP_PORT;
		break;
		default:
			fprintf(stderr, "Usage: %s host function filename\n", argv[0]);
			exit(1);
	}

	convertToUpperCase(func);
	if (strcmp(func, (char*) "GET") != 0 && strcmp(func, (char*) "SEND") != 0)
	{
		fprintf(stderr, "Function argument options: GET, SEND\n");
		exit(1);
	}
	else if (strcmp(func, (char*) "GET") == 0)
	{
		get_flag = GET;

    fp = fopen(filename, "w");
    if (fp == NULL)
    {
      perror("Cannot save GET file");
      exit(1);
    }

    printf("Opened GET file %s successfully\n", filename);
	}
	else
	{
		get_flag = SEND;

		fp = fopen(filename, "r");
    if (fp == NULL)
    {
      perror("Cannot access SEND file");
      exit(1);
    }

    // check if file is empty
    fseek(fp, 0, SEEK_END);
    if (ftell(fp) > 0)
    {
      printf("Opened SEND file %s successfully\n", filename);
      fseek(fp, 0, SEEK_SET);
    }
    else
    {
      perror("SEND file does not exist");
      exit(1);
    }
  }

  sprintf(func, "%i", get_flag);

	// Create the socket
	if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
	{
		perror("Cannot create socket");
		exit(1);
	}

	bzero((char *)&server, sizeof(struct sockaddr_in));
	server.sin_family = AF_INET;
	server.sin_port = htons(port);
	if ((hp = gethostbyname(host)) == NULL)
	{
		fprintf(stderr, "Unknown server address\n");
		exit(1);
	}
	bcopy(hp->h_addr, (char *)&server.sin_addr, hp->h_length);

	// Connecting to the server
	if (connect (sd, (struct sockaddr *)&server, sizeof(server)) == -1)
	{
		fprintf(stderr, "Can't connect to server\n");
		perror("connect");
		exit(1);
	}
	printf("Connected:    Server Name: %s\n", hp->h_name);
	pptr = hp->h_addr_list;
	printf("\t\tIP Address: %s\n", inet_ntop(hp->h_addrtype, *pptr, str, sizeof(str)));
	printf("Transmit: %s\n", func);

	// Transmit function data through the socket
	send (sd, func, strlen(func), 0);

	printf("Waiting for Ack\n");
	bp = rbuf;

	// client makes repeated calls to recv until no more data is expected to arrive.
	while (bp < rbuf + 1)
	{
    n = recv(sd, bp, 1, 0);
		bp += n;
	}

	printf("Received Ack %c\n", rbuf[0]);
  printf("Transmit: %s\n", filename);

  // Transmit filename data through the socket
  send (sd, filename, BUFLEN, 0);

  printf("Waiting for Port Number\n");
  bp = rbuf;
  bytes_to_read = BUFLEN;
 
	// client makes repeated calls to recv until no more data is expected to arrive.
	while ((n = recv(sd, bp, bytes_to_read, 0)) < BUFLEN)
	{
    bp += n;
		bytes_to_read -= n;
	}

  port = atoi(rbuf);
	printf ("Received Port: %d\n", port);
  close(sd);

  socklen_t client_len;
  struct sockaddr_in client;
  int new_sd;

	// Create the socket
	if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
	{
		perror("Cannot create socket");
		exit(1);
	}

  // Bind an address to the socket
  bzero((char *)&server, sizeof(struct sockaddr_in));
  server.sin_family = AF_INET;
  server.sin_port = htons(port);
  server.sin_addr.s_addr = htonl(INADDR_ANY); // Accept connections from any client

  if (bind(sd, (struct sockaddr *) &server, sizeof(server)) == -1)
  {
    perror("Can't bind name to socket");
    exit(1);
  }

  // listen for connections
  printf(" Listening for Connections\n");
  // queue 1 connect request
  listen(sd, 1);

  while (fp != NULL)
  {
    client_len = sizeof(client);
    if ((new_sd = accept(sd, (struct sockaddr *)&client, &client_len)) == -1)
    {
      fprintf(stderr, "Can't accept client\n");
      exit(1);
    }

    char* ip_address = inet_ntoa(client.sin_addr);
    unsigned short port = client.sin_port;
    printf(" Remote Address:  %s\n", ip_address);
    printf(" Remote Port:  %d\n", port);

    char file_buf[BUFLEN];
    bzero(file_buf, BUFLEN);
    int file_data;

    // receive file from server
    if (get_flag == GET)
    {
      while ((file_data = recv(new_sd, file_buf, BUFLEN, 0)) > 0)
      {
        int write_data = fwrite(file_buf, sizeof(char), file_data, fp);
        if (write_data < file_data)
        {
          perror("Failed receiving file");
          exit(1);
        }
        bzero(file_buf, BUFLEN);
      }
      fseek(fp, 0, SEEK_END);
      if (ftell(fp) > 0)
      {
        printf("Finished receiving %s\n", filename);
      }
      else
      {
        perror("Failed receiving file");
        exit(1);
      }

      fclose(fp);
      fp = NULL;

      close(new_sd);
    }
    // send file to server
    else
    {
      while ((file_data = fread(file_buf, sizeof(char), BUFLEN, fp)) > 0)
      {
        if (send(new_sd, file_buf, file_data, 0) < 0)
        {
          perror("Failed sending file");
          exit(1);
        }
        bzero(file_buf, BUFLEN);
      }
      printf("Finished sending file %s\n", filename);

      fclose(fp);
      fp = NULL; 
    }
  }
 
  close(sd);
	return (0);
}

void convertToUpperCase(char *sPtr)
{
	while(*sPtr != '\0')
	{
		*sPtr = toupper((unsigned char)*sPtr);
		sPtr++;
	}
}
