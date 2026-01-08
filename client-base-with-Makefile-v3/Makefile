# Compiler variables
CC = gcc
CFLAGS = -g -Wall -Wextra -std=c17 -D_POSIX_C_SOURCE=200809L
LDFLAGS = -lncurses

# Directory variables
OBJ_DIR = obj
BIN_DIR = bin
INCLUDE_DIR = include
CLIENT_DIR = src/client

# Executables
SERVER = Pacmanist
CLIENT = client

# Server objects
OBJS_SERVER = server.o board.o parser.o api.o debug.o display.o

# Client objects
OBJS_CLIENT = client_main.o api.o debug.o display.o

# Dependencies
display.o = display.h
board.o = board.h
parser.o = parser.h
api.o = api.h protocol.h

# Object files path
vpath %.o $(OBJ_DIR)
vpath %.c $(CLIENT_DIR) $(INCLUDE_DIR)

# Make targets
all: server client

server: $(BIN_DIR)/$(SERVER)

$(BIN_DIR)/$(SERVER): $(addprefix $(OBJ_DIR)/,$(OBJS_SERVER)) | folders
	$(CC) $(CFLAGS) $(addprefix $(OBJ_DIR)/,$(OBJS_SERVER)) -o $@ $(LDFLAGS)

client: $(BIN_DIR)/$(CLIENT)

$(BIN_DIR)/$(CLIENT): $(addprefix $(OBJ_DIR)/,$(OBJS_CLIENT)) | folders
	$(CC) $(CFLAGS) $(addprefix $(OBJ_DIR)/,$(OBJS_CLIENT)) -o $@ $(LDFLAGS)

# Server compilation
$(OBJ_DIR)/server.o: $(CLIENT_DIR)/server.c $(INCLUDE_DIR)/server.h \
	$(INCLUDE_DIR)/board.h $(INCLUDE_DIR)/parser.h $(INCLUDE_DIR)/protocol.h | folders
	$(CC) -I $(INCLUDE_DIR) $(CFLAGS) -o $(OBJ_DIR)/server.o -c $<

$(OBJ_DIR)/board.o: $(CLIENT_DIR)/board.c $(INCLUDE_DIR)/board.h \
	$(INCLUDE_DIR)/parser.h | folders
	$(CC) -I $(INCLUDE_DIR) $(CFLAGS) -o $(OBJ_DIR)/board.o -c $<

$(OBJ_DIR)/parser.o: $(CLIENT_DIR)/parser.c $(INCLUDE_DIR)/parser.h \
	$(INCLUDE_DIR)/board.h | folders
	$(CC) -I $(INCLUDE_DIR) $(CFLAGS) -o $(OBJ_DIR)/parser.o -c $<

$(OBJ_DIR)/api.o: $(CLIENT_DIR)/api.c $(INCLUDE_DIR)/api.h \
	$(INCLUDE_DIR)/protocol.h | folders
	$(CC) -I $(INCLUDE_DIR) $(CFLAGS) -o $(OBJ_DIR)/api.o -c $<

$(OBJ_DIR)/debug.o: $(CLIENT_DIR)/debug.c $(INCLUDE_DIR)/debug.h | folders
	$(CC) -I $(INCLUDE_DIR) $(CFLAGS) -o $(OBJ_DIR)/debug.o -c $<

$(OBJ_DIR)/display.o: $(CLIENT_DIR)/display.c $(INCLUDE_DIR)/display.h | folders
	$(CC) -I $(INCLUDE_DIR) $(CFLAGS) -o $(OBJ_DIR)/display.o -c $<

$(OBJ_DIR)/client_main.o: $(CLIENT_DIR)/client_main.c $(INCLUDE_DIR)/api.h \
	$(INCLUDE_DIR)/protocol.h $(INCLUDE_DIR)/display.h $(INCLUDE_DIR)/debug.h | folders
	$(CC) -I $(INCLUDE_DIR) $(CFLAGS) -o $(OBJ_DIR)/client_main.o -c $<

# Create folders
folders:
	mkdir -p $(OBJ_DIR)
	mkdir -p $(BIN_DIR)

# Clean object files and executables
clean:
	rm -f $(OBJ_DIR)/*.o
	rm -f $(BIN_DIR)/$(SERVER)
	rm -f $(BIN_DIR)/$(CLIENT)
	rm -f *.log

# Run the server (requires arguments: <levels_dir> <max_games> <register_pipe>)
run-server: server
	@echo "Usage: ./$(BIN_DIR)/$(SERVER) <levels_dir> <max_games> <register_pipe>"
	@echo "Example: ./$(BIN_DIR)/$(SERVER) ./levels 1 /tmp/server_pipe"

# Run the client executable (requires arguments: <client_id> <register_pipe> [commands_file])
run-client: client
	@echo "Usage: ./$(BIN_DIR)/$(CLIENT) <client_id> <register_pipe> [commands_file]"
	@echo "Example: ./$(BIN_DIR)/$(CLIENT) 1 /tmp/server_pipe"
	@echo "To run with arguments, use: make run-client ARGS='<client_id> <register_pipe> [commands_file]'"

# Identify targets that do not create files
.PHONY: all server client clean run-server run-client folders

