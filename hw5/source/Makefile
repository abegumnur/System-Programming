CC = gcc
CFLAGS =-pthread -Wall 

POOLSIZE = 100
BUFFERSIZE = 100
SOURCEDIR = ../PA7
DESTDIR = ../test

all: main

run:
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./main $(BUFFERSIZE) $(POOLSIZE) $(SOURCEDIR) $(DESTDIR)

main: main.o consumer.o producer.o utility.o
	$(CC) $(CFLAGS) main.o producer.o consumer.o utility.o -o main 

main.o: main.c 
	$(CC) $(CFLAGS) -c main.c

consumer.o:consumer.c
	$(CC) $(CFLAGS) -c consumer.c

producer.o:producer.c
	$(CC) $(CFLAGS) -c producer.c

utility.o: utility.c
	$(CC) $(CFLAGS) -c utility.c

clean:
	rm -f main *.o
