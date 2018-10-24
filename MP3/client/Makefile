.PHONY: all clean

# compiler name
CC=gcc

# place options like -O2, -g here
CFLAGS=

all: miner

miner: miner.c
	$(CC) -o miner $(CFLAGS) miner.c -lcrypto -lssl

clean:
	rm -f miner
