CC := gcc
CFLAGS := -Wextra -lpthread

# Server executable
SERVER_SRC := server.c
SERVER_OBJ := $(SERVER_SRC:.c=.o)
SERVER_BIN := server

# Client executable
CLIENT_SRC := client.c
CLIENT_OBJ := $(CLIENT_SRC:.c=.o)
CLIENT_BIN := client

all: $(SERVER_BIN) $(CLIENT_BIN)

$(SERVER_BIN): $(SERVER_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

$(CLIENT_BIN): $(CLIENT_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(SERVER_OBJ) $(CLIENT_OBJ) $(SERVER_BIN) $(CLIENT_BIN)
