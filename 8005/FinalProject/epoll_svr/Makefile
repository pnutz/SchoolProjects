# make for tcp_svr
CC=gcc
CFLAGS=-Wall -ggdb

TARGET=epoll_svr

$(TARGET): $(TARGET).c ; $(CC) $(CFLAGS) $(TARGET).c -o $(TARGET) -lrt -lpthread

clean: ; rm -f $(TARGET)
