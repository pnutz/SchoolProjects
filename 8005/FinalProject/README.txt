port_fwd v1.0
Latest update 03/23/2015 by Christopher Eng
---------------------------
This minimum-functionality "Port Forwarder" was developed in C for COMP 8005 - Network and Security Applications Development.
The source code, configuration files, and Makefile can be found in the port_fwd directory (Makefile, port_fwd.c, port_fwd_reader.c, port_fwd_table.config).
For testing purposes, two modules have been included to this project submission in their respective directories:
    - tcp_clnt - TCP client program (Makefile, tcp_clnt.c)
    - epoll_svr - Multi-threaded Epoll Echo Server program (Makefile, epoll_svr.c)

The programs are developed for use in a Linux environment, utilizing the pthread library, the epoll system call, and several other Unix libraries.

Compilation
-----------------------
To compile the source code, simply run the Makefile in each directory using 'make'.  You can then run the programs based on the following command strings:
port_fwd: ./port_fwd
tcp_clnt: ./tcp_clnt <host> <# of connections to create> <# of data sends> <# of seconds to wait between sends> <optional: server port (default 7000)> <optional: data send length - bytes>
epoll_svr: ./epoll_svr <optional: server port (default 7000)>

Most linux environments are defaulted to a ulimit of 1024 file descriptors.
The following commands will set the ulimit to 32768 fds:
    echo 32768 > /proc/sys/fs/file-max
    ulimit -n 32768

Port Forward
-----------------------
The port forwarder acts as a relay between a client and a server.  The client connects to the port forwarder through a defined port in the Port Forward Table.  The port forwarder creates a connection to the defined destination IP:port pair.
Any data sent from the client will be sent to the server.  All responses from the server will be relayed back to the originating client.
The receive buffer length is set to 5000 bytes.  The program will cut off any messages that are longer than 5000 bytes.
The port forwarder can handle multiple concurrent connections.  The port forwarder is designed to handle at most 80000 concurrent connections.  This equates to 40000 client connections, since each client creates an associated server connection.  However, the user should not expect to hit this limit in runtime.  It is meant to be a defined upper bound.
The port forward program reads from the configuration file "port_fwd_table.config".  The format of this file is found in the following section.
The output of this program is saved to "port_fwd_connections.txt".

Port Forward Table
-----------------------
The configuration file can hold up to 100 forwarded ports.  The first line is always ignored, so it can be used to write any comments.  Any sequential lines after must be in the following format:
{PORT}={SVR_ADDR}|{SVR_PORT}
where PORT is the forwarded port, SVR_ADDR is the address of the destination server, and SVR_PORT is the port connection to the destination server.
If there are duplicate forwarded ports in the configuration file, the first instance of the port configuration will be taken.

TCP Client
-----------------------
The TCP client is a client program that connects to a host through an optionally defined port (or the default 7000).  It sends a message every <# of seconds to wait between sends> of optionally defined size (or default 255 bytes).
The <# of connections to create> will create a separate thread for another connection for each additional number entered.
The output of this program is saved to "clnt_connections.txt".

Epoll Echo Server
-----------------------
The Epoll Echo Server is a server program that listens on an optionally defined port (or the default 7000).  It receives any messages and responds to the sending client with an echo of the message.
The receive buffer length is set to 5000 bytes.  The program will cut off any messages that are longer than 5000 bytes.
The output of this program is saved to "svr_connections.txt".
The Epoll Server is designed to handle at most 80000 concurrent connections.  However, the user should not expect to hit this limit in runtime.  It is meant to be a defined upper bound.


The program can be tested by running multiple servers and a port forwarder.  The port forward table must point to the running server instances.  Run TCP clients to the port forwarder's forwarded ports and it should be relayed to the defined epoll servers.