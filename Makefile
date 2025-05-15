/* =========================== Makefile ============================
# Usage:
#   make all        # build library + server + client
#   make server     # build only server
#   make client     # build only client
#   make clean      # remove objects & binaries

CC      = gcc
CFLAGS  = -Wall -Wextra -pedantic -std=c11 -pthread
LDFLAGS = -pthread

SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin

LIB_SRC = $(SRC_DIR)/powerudp.c
LIB_OBJ = $(OBJ_DIR)/powerudp.o

SERVER_SRC = $(SRC_DIR)/server.c
CLIENT_SRC = $(SRC_DIR)/client.c

SERVER_OBJ = $(OBJ_DIR)/server.o
CLIENT_OBJ = $(OBJ_DIR)/client.o

LIB_TARGET = $(BIN_DIR)/libpowerudp.a
SERVER_BIN = $(BIN_DIR)/server
CLIENT_BIN = $(BIN_DIR)/client

all: $(SERVER_BIN) $(CLIENT_BIN)

$(BIN_DIR) $(OBJ_DIR):
	@mkdir -p $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIB_TARGET): $(LIB_OBJ) | $(BIN_DIR)
	ar rcs $@ $^

$(SERVER_BIN): $(LIB_TARGET) $(SERVER_OBJ) | $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(CLIENT_BIN): $(LIB_TARGET) $(CLIENT_OBJ) | $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

.PHONY: all clean