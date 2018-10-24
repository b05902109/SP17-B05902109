.PHONY: all clean

# compiler name
CC=gcc

# place options like -O2, -g here
CFLAGS=

all: boss

boss: boss.c boss.h
	$(CC) -o boss $(CFLAGS) boss.h boss.c -lcrypto -lssl

clean:
	rm -f boss
