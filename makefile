CFLAGS = -g -Wall -Wvla -fsanitize=address
CC = gcc
A = shell

all: 
	$(CC) $(CFLAGS) -o $(A) $(A).c
