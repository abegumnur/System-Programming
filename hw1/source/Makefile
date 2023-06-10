# Makefile for compiling the program
# Set the source files
SRCS = appendMeMore.c part2.c part3.c

# Set the object files
OBJS = $(SRCS:.c=.o)

# Set the compiler options
CC = gcc
CFLAGS = -Wall -g

# Define the targets
all: appendMeMore part2 part3

%.o: %.c
	$(CC) $(CFLAGS) $< -o $@
#clean all files - object - source - executable
clean:
	rm -f *.o $(SRCS) appendMeMore part2 part3








