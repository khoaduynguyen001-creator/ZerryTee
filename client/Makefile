CC = gcc
CFLAGS = -O2 -Wall
LIBS = -lsodium -lcurl

all: client

client: client.c
	$(CC) $(CFLAGS) client.c -o client $(LIBS)

clean:
	rm -f client keypair.bin
