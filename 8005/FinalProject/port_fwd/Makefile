# make for port_fwd
CC=gcc
CFLAGS=-Wall -ggdb

TARGET=port_fwd

$(TARGET): $(TARGET).c ; $(CC) $(CFLAGS) $(TARGET).c -o $(TARGET) -lrt -lpthread

clean: ; rm -f $(TARGET)
