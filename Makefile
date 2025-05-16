# ============================  Makefile  =============================
# Principais alvos:
#   make              -> compila lib + server + client
#   make tests        -> idem + test_powerudp
#   make server       -> só binário server
#   make client       -> só binário client
#   make runserver    -> arranca server na $(PORT)
#   make clean        -> remove obj/ bin/
# ---------------------------------------------------------------------

CC      = gcc
CFLAGS  = -Wall -Wextra -pedantic -std=c11 -pthread
LDFLAGS = -pthread

SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin
TEST_DIR= tests

LIB_SRC = $(SRC_DIR)/powerudp.c
SRV_SRC = $(SRC_DIR)/server.c
CLI_SRC = $(SRC_DIR)/client.c
TEST_SRC= $(TEST_DIR)/test_powerudp.c

LIB_OBJ = $(OBJ_DIR)/powerudp.o
LIB_A   = $(BIN_DIR)/libpowerudp.a

SRV_OBJ = $(OBJ_DIR)/server.o
SRV_BIN = $(BIN_DIR)/server

CLI_OBJ = $(OBJ_DIR)/client.o
CLI_BIN = $(BIN_DIR)/client

TEST_OBJ= $(OBJ_DIR)/test_powerudp.o
TEST_BIN= $(BIN_DIR)/test_powerudp

$(OBJ_DIR) $(BIN_DIR):
	@mkdir -p $@

all: $(BIN_DIR) $(LIB_A) $(SRV_BIN) $(CLI_BIN)

$(LIB_A): $(LIB_OBJ) | $(BIN_DIR)
	ar rcs $@ $^

$(OBJ_DIR)/powerudp.o: $(LIB_SRC) | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

server: $(SRV_BIN)
$(SRV_BIN): $(SRV_OBJ) $(LIB_A)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(OBJ_DIR)/server.o: $(SRV_SRC) | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

client: $(CLI_BIN)
$(CLI_BIN): $(CLI_OBJ) $(LIB_A)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(OBJ_DIR)/client.o: $(CLI_SRC) | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

tests: $(TEST_BIN)
$(TEST_BIN): $(TEST_OBJ) $(LIB_A) | $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(OBJ_DIR)/test_powerudp.o: $(TEST_SRC) | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# ---------- conveniência ---------------------------------------------
PORT ?= 7010

runserver: all
	@echo "Starting server on port $(PORT)…"
	@$(SRV_BIN) $(PORT)

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
# =====================================================================
