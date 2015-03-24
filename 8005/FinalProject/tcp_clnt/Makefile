# make for tcp_clnt
CC=gcc
CFLAGS=-Wall -ggdb

TARGET=tcp_clnt

$(TARGET): $(TARGET).c ; $(CC) $(CFLAGS) $(TARGET).c -o $(TARGET) -lrt -lpthread

clean: ; rm -f $(TARGET)
