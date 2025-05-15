# ============================ Makefile =============================
# Usage:
#   make            – build biblioteca + server + client
#   make server     – só server
#   make client     – só client
#   make clean      – remove obj + bin
# ------------------------------------------------------------------
CC      = gcc
CFLAGS  = -Wall -Wextra -pedantic -std=c11 -pthread
LDFLAGS = -pthread

SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin

LIB_SRC = $(SRC_DIR)/powerudp.c
LIB_OBJ = $(OBJ_DIR)/powerudp.o
LIB_A   = $(BIN_DIR)/libpowerudp.a

SRV_SRC = $(SRC_DIR)/server.c
SRV_OBJ = $(OBJ_DIR)/server.o
SRV_BIN = $(BIN_DIR)/server

CLI_SRC = $(SRC_DIR)/client.c
CLI_OBJ = $(OBJ_DIR)/client.o
CLI_BIN = $(BIN_DIR)/client

$(OBJ_DIR) $(BIN_DIR):
	@mkdir -p $@

all: $(OBJ_DIR) $(BIN_DIR) $(LIB_A) $(SRV_BIN) $(CLI_BIN)

$(LIB_A): $(LIB_OBJ)
	ar rcs $@ $^

$(SRV_BIN): $(SRV_OBJ) $(LIB_A)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(CLI_BIN): $(CLI_OBJ) $(LIB_A)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

server: $(SRV_BIN)
client: $(CLI_BIN)

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
# ==================================================================
