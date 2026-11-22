CC      = gcc
CFLAGS  = -Wall -Wextra -g -Icommon
BIN_DIR = bin

COMMON_SRC = common/net_utils.c
DEVICE_SRC = device_sim/device_server.c
CLIENT_SRC = client_app/user_client.c client_app/home_manager.c
WEB_SRC    = client_app/web_dashboard.c client_app/home_manager.c

all: $(BIN_DIR)/device_server $(BIN_DIR)/user_client $(BIN_DIR)/web_dashboard

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BIN_DIR)/device_server: $(DEVICE_SRC) $(COMMON_SRC) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(DEVICE_SRC) $(COMMON_SRC) -o $(BIN_DIR)/device_server

$(BIN_DIR)/user_client: $(CLIENT_SRC) $(COMMON_SRC) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(CLIENT_SRC) $(COMMON_SRC) -o $(BIN_DIR)/user_client

$(BIN_DIR)/web_dashboard: $(WEB_SRC) $(COMMON_SRC) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(WEB_SRC) $(COMMON_SRC) -o $(BIN_DIR)/web_dashboard

test: all
	python3 scripts/test_protocol.py --spawn-device

clean:
	rm -rf $(BIN_DIR) device_sim/device_server *.log client_data.db

.PHONY: all clean test
