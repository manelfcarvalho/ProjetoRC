/* =========================== Makefile ============================
# Usage:
#   make all            # build everything
#   make server         # build only server binary
#   make client         # build only client binary
#   make runserver      # kill whatever is using $(PORT) then run server
#   make killport       # just kill whatever is using $(PORT)
#   make clean          # remove objects & binaries
#   PORT=7000 make …    # override default port in runserver / killport

CC      = gcc
CFLAGS  = -Wall -Wextra -pedantic -std=c11 -pthread
LDFLAGS = -pthread

SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin
TOOLS_DIR = tools

LIB_SRC = $(SRC_DIR)/powerudp.c
LIB_OBJ = $(OBJ_DIR)/powerudp.o

SERVER_SRC = $(SRC_DIR)/server.c
CLIENT_SRC = $(SRC_DIR)/client.c

SERVER_OBJ = $(OBJ_DIR)/server.o
CLIENT_OBJ = $(OBJ_DIR)/client.o

LIB_TARGET = $(BIN_DIR)/libpowerudp.a
SERVER_BIN = $(BIN_DIR)/server
CLIENT_BIN = $(BIN_DIR)/client

PORT ?= 7000

all: $(SERVER_BIN) $(CLIENT_BIN)

$(BIN_DIR) $(OBJ_DIR) $(TOOLS_DIR):
	@mkdir -p $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIB_TARGET): $(LIB_OBJ) | $(BIN_DIR)
	ar rcs $@ $^

$(SERVER_BIN): $(LIB_TARGET) $(SERVER_OBJ) | $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(CLIENT_BIN): $(LIB_TARGET) $(CLIENT_OBJ) | $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Helper: kill whatever is occupying the chosen PORT
killport: | $(TOOLS_DIR)
	bash $(TOOLS_DIR)/killport.sh $(PORT)

runserver: all killport
	$(SERVER_BIN) $(PORT)

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

.PHONY: all clean killport runserver

