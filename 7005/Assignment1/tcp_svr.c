/*---------------------------------------------------------------------------------------
--	SOURCE FILE:		tcp_svr.c -   A simple file transfer server using TCP
--
--	PROGRAM:		tsvr.exe
--
--	FUNCTIONS:		Berkeley Socket API
--
--	DATE:			January 23, 2001
--
--	REVISIONS:		(Date and Description)
--				September 2014
--        Modification of Aman Abdulla's Berkeley Socket API tcp_svr.c echo server.
--        Altered server functionality to receive files from and send files to clients.
--
--	DESIGNERS:		Christopher Eng
--
--	PROGRAMMERS:		Christopher Eng
--
--	NOTES:
--	The program will accept TCP connections from client machines.
--  The program will read a function (GET/SEND) and filename from the client socket,
--  setup a new connection to the client, and based on the function, send or receive stated file.
---------------------------------------------------------------------------------------*/
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>

#define SERVER_TCP_PORT 7005	// Default port
#define BUFLEN	512		//Buffer length
#define GET   0
#define SEND  1

int main (int argc, char **argv)
{
	int	n, bytes_to_read;
	int	sd, new_sd;
  socklen_t client_len;
	struct	sockaddr_in server, client;
	char	*bp, buf[1];
  
  int func;
  char filename[BUFLEN];

	int	server_port = SERVER_TCP_PORT;

	// Create a stream socket
	if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
	{
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
		perror("Can't bind name to socket");
		exit(1);
	}

	// Listen for connections
	// queue up to 5 connect requests
	listen(sd, 5);

  client_len= sizeof(client);
  if ((new_sd = accept (sd, (struct sockaddr *)&client, &client_len)) == -1)
  {
    fprintf(stderr, "Can't accept client\n");
    exit(1);
  }

  char* ip_address = inet_ntoa(client.sin_addr);
  unsigned short client_port = client.sin_port;
  printf(" Remote Address:  %s\n", ip_address);
  printf(" Remote Port:  %d\n", client_port);
  bp = buf;
  // keep receiving until function arrives
  while (bp < buf + 1)
  {
    n = recv(new_sd, bp, 1, 0);
    bp += n;
  }
  func = buf[0] - '0';

  printf("Sending Ack: 1\n");

  send (new_sd, (char*) "1", 1, 0);

  bp = filename;
  bytes_to_read = BUFLEN;
  // keep receiving until filename arrives
  while ((n = recv(new_sd, bp, bytes_to_read, 0)) < BUFLEN)
  {
    bp += n;
    bytes_to_read -= n;
  }
  printf("Function: %i\n", func);
  printf("Filename: %s\n", filename);
  printf("Sending Client Port: %d\n", client_port);
  char port_data[5];
  sprintf(port_data, "%d", client_port);
  send (new_sd, port_data, BUFLEN, 0);

  close(new_sd);
  close(sd);
 
  // Create the socket
  if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
  {
    perror("Cannot create socket");
    exit(1);
  } 

  // wait for socket to properly close - 120 seconds to ensure address is not in use
  sleep(20);

  // bind port # 7005 to this client
  server.sin_port = server_port;
  if (bind(sd, (struct sockaddr *) &server, sizeof(server)) == -1)
  {
    perror("Can't bind name to socket");
    exit(1);
  }

  printf(" Connecting To Server\n");

  client.sin_port = htons(client_port);
  if (connect(sd, (struct sockaddr *)&client, sizeof(client)) == -1)
  {
    fprintf(stderr, "Can't connect to client\n");
    perror("connect");
    exit(1);
  }

  FILE *fp;
  char file_buf[BUFLEN];
  bzero(file_buf, BUFLEN);
  int file_data;

  // send file to client
  if (func == GET)
  {
    fp = fopen(filename, "r");
    if (fp == NULL)
    {
      perror("Cannot access requested file");
      exit(1);
    }
    
    // check file length
    fseek(fp, 0, SEEK_END);
    if (ftell(fp) > 0)
    {
      printf("Opened requested file %s successfully\n", filename);
      // return to beginning of file
      fseek(fp, 0, SEEK_SET);
    }
    else
    {
      perror("Requested file does not exist");
      exit(1);
    }
    
    while ((file_data = fread(file_buf, sizeof(char), BUFLEN, fp)) > 0)
    {
      if (send(sd, file_buf, file_data, 0) < 0)
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
  // receive file from client
  else
  {
    fp = fopen(filename, "w");
    if (fp == NULL)
    {
      perror("Cannot save sent file");
      exit(1);
    }
    
    printf("Opened sent file %s successfully\n", filename);

    while ((file_data = recv(sd, file_buf, BUFLEN, 0)) > 0)
    {
      int write_data = fwrite(file_buf, sizeof(char), file_data, fp);
      if (write_data < file_data)
      {
        perror("Failed receiving file");
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
  }
	close(sd);
	return(0);
}
