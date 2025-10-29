CC = gcc
CFLAGS = -Wall -Wextra -g -Isrc/include -pthread
LDFLAGS = -pthread

# Directories
SRC_DIR = src
BUILD_DIR = build
BIN_DIR = bin

# Source files
CORE_SRC = $(SRC_DIR)/core/network.c $(SRC_DIR)/core/peer.c $(SRC_DIR)/core/keypair.c
TRANSPORT_SRC = $(SRC_DIR)/transport/transport.c
TUN_SRC = $(SRC_DIR)/tun/tun.c
CONTROLLER_SRC = $(SRC_DIR)/controller/controller.c
CLIENT_SRC = $(SRC_DIR)/client/client.c

# Object files
CORE_OBJ = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(CORE_SRC))
TRANSPORT_OBJ = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(TRANSPORT_SRC))
TUN_OBJ = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(TUN_SRC))
CONTROLLER_OBJ = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(CONTROLLER_SRC))
CLIENT_OBJ = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(CLIENT_SRC))

ALL_OBJ = $(CORE_OBJ) $(TRANSPORT_OBJ) $(TUN_OBJ) $(CONTROLLER_OBJ) $(CLIENT_OBJ)

# Executables
CONTROLLER_BIN = $(BIN_DIR)/zerrytee-controller
CLIENT_BIN = $(BIN_DIR)/zerrytee-client

.PHONY: all clean dirs controller client help

all: dirs controller client

dirs:
	@mkdir -p $(BUILD_DIR)/core
	@mkdir -p $(BUILD_DIR)/transport
	@mkdir -p $(BUILD_DIR)/tun
	@mkdir -p $(BUILD_DIR)/controller
	@mkdir -p $(BUILD_DIR)/client
	@mkdir -p $(BIN_DIR)

# Compile object files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Controller executable
controller: dirs $(CORE_OBJ) $(TRANSPORT_OBJ) $(CONTROLLER_OBJ)
	$(CC) $(LDFLAGS) $(CORE_OBJ) $(TRANSPORT_OBJ) $(CONTROLLER_OBJ) \
		src/controller/main.c -o $(CONTROLLER_BIN) $(CFLAGS)
	@echo "Built $(CONTROLLER_BIN)"

# Client executable
client: dirs $(CORE_OBJ) $(TRANSPORT_OBJ) $(TUN_OBJ) $(CLIENT_OBJ)
	$(CC) $(LDFLAGS) $(CORE_OBJ) $(TRANSPORT_OBJ) $(TUN_OBJ) $(CLIENT_OBJ) \
		src/client/main.c -o $(CLIENT_BIN) $(CFLAGS)
	@echo "Built $(CLIENT_BIN)"

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)
	@echo "Cleaned build artifacts"

help:
	@echo "Zerrytee Build System"
	@echo "====================="
	@echo "Targets:"
	@echo "  all         - Build controller and client (default)"
	@echo "  controller  - Build controller only"
	@echo "  client      - Build client only"
	@echo "  clean       - Remove build artifacts"
	@echo ""
	@echo "Usage:"
	@echo "  make controller  # Build controller"
	@echo "  make client      # Build client"
	@echo "  make             # Build everything"