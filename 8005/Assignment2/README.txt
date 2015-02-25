The assignment is split up into 4 segments, found in their respective directories:
tcp_clnt - the client process
tcp_svr - the traditional multi-threaded server
select_svr - the select multiplexed server
epoll_svr - the epoll asynchronous server

To compile the source code, simply run the Makefile in the directory using 'make'.  You can then run the client or server based on the following command strings:

tcp_clnt: ./tcp_clnt <host> <# of connections to create> <# of data sends> <# of seconds to wait between sends> <optional: server port>
tcp_svr: ./tcp_svr <optional: server port>
select_svr: ./select_svr <optional: server port>
epoll_svr: ./epoll_svr <optional: server port>

The server port is defaulted to 7000.  If the optional parameter is added as an argument, the associated client/server must also enter the port parameter.
