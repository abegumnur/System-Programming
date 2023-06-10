all: client server

server: server.o 
	gcc -o server server.o

server.o: server.c
	gcc -std=gnu99 -c server.c

client: client.o 
	gcc -o client client.o -lm

client.o: client.c
	gcc -std=gnu99 -c client.c

clean:
	rm -f server.o client.o server client	
