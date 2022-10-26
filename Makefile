CC = gcc
CFLAGS = -g -std=gnu99 -pedantic -Wall -Wextra -Werror
LDLIBS = -pthread -lrt

.PHONY: all zip clean

all: proj2

proj2.o: proj2.c
	$(CC) $(CFLAGS) -c proj2.c -o proj2.o

proj2: proj2.o
	$(CC) proj2.o $(LDLIBS) -o proj2

zip:
	zip proj2.zip *.c *.h Makefile

clean:
	rm -f *.o