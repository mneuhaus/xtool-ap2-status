# Makefile for xTool AP2 Status Monitor
# Target: Waveshare ESP32-S3-Touch-LCD-2
# Requires: arduino-cli (brew install arduino-cli)

# Project Configuration
SKETCH := ap2-status.ino
SKETCH_DIR := ap2-status
BOARD_FQBN := esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB
PORT := $(shell arduino-cli board list | grep "usbmodem" | awk '{print $$1}' | head -n 1)
BAUD := 115200

# Colors for output
RED := \033[0;31m
GREEN := \033[0;32m
YELLOW := \033[0;33m
BLUE := \033[0;34m
CYAN := \033[0;36m
NC := \033[0m

# Default target
.PHONY: help
help:
	@echo "$(CYAN)╔════════════════════════════════════════════════════════════╗$(NC)"
	@echo "$(CYAN)║       xTool AP2 Status Monitor - Build System              ║$(NC)"
	@echo "$(CYAN)║       Target: Waveshare ESP32-S3-Touch-LCD-2               ║$(NC)"
	@echo "$(CYAN)╚════════════════════════════════════════════════════════════╝$(NC)"
	@echo ""
	@echo "$(GREEN)Quick Start:$(NC)"
	@echo "  $(YELLOW)make setup$(NC)      - First time setup (install deps + board + libs)"
	@echo "  $(YELLOW)make flash$(NC)      - Compile and upload to board"
	@echo "  $(YELLOW)make monitor$(NC)    - Open serial monitor"
	@echo ""
	@echo "$(GREEN)Available Commands:$(NC)"
	@echo "  $(YELLOW)make install$(NC)    - Install arduino-cli"
	@echo "  $(YELLOW)make setup$(NC)      - Setup ESP32-S3 board + LVGL libraries"
	@echo "  $(YELLOW)make compile$(NC)    - Compile only"
	@echo "  $(YELLOW)make flash$(NC)      - Compile and upload"
	@echo "  $(YELLOW)make upload$(NC)     - Alias for flash"
	@echo "  $(YELLOW)make monitor$(NC)    - Serial monitor (Ctrl+C to exit)"
	@echo "  $(YELLOW)make all$(NC)        - Flash + monitor"
	@echo "  $(YELLOW)make clean$(NC)      - Clean build artifacts"
	@echo "  $(YELLOW)make boards$(NC)     - List connected boards"
	@echo "  $(YELLOW)make config$(NC)     - Show current configuration"
	@echo ""

.PHONY: check-cli
check-cli:
	@which arduino-cli > /dev/null || (echo "$(RED)Error: arduino-cli not found!$(NC)" && \
		echo "Run: $(YELLOW)make install$(NC)" && exit 1)

.PHONY: install
install:
	@echo "$(GREEN)Installing arduino-cli...$(NC)"
	@if command -v brew >/dev/null 2>&1; then \
		brew install arduino-cli; \
	else \
		curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh; \
	fi
	@echo "$(GREEN)✓ Done. Run: make setup$(NC)"

.PHONY: setup
setup: check-cli
	@echo "$(GREEN)Setting up ESP32-S3 + LVGL...$(NC)"
	@echo ""
	@echo "$(BLUE)[1/5] Initializing config...$(NC)"
	@arduino-cli config init 2>/dev/null || true
	@echo "$(BLUE)[2/5] Adding ESP32 board index...$(NC)"
	@arduino-cli config add board_manager.additional_urls https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json 2>/dev/null || true
	@echo "$(BLUE)[3/5] Updating board index...$(NC)"
	@arduino-cli core update-index
	@echo "$(BLUE)[4/5] Installing ESP32 core (takes a few minutes)...$(NC)"
	@arduino-cli core install esp32:esp32
	@echo "$(BLUE)[5/5] Installing libraries...$(NC)"
	@arduino-cli lib install "lvgl"
	@arduino-cli lib install "GFX Library for Arduino"
	@arduino-cli lib install "TFT_eSPI"
	@arduino-cli lib install "NimBLE-Arduino"
	@arduino-cli lib install "ArduinoJson"
	@echo ""
	@echo "$(GREEN)╔════════════════════════════════════════════════════════════╗$(NC)"
	@echo "$(GREEN)║  ✓ Setup complete!                                         ║$(NC)"
	@echo "$(GREEN)╚════════════════════════════════════════════════════════════╝$(NC)"
	@echo ""
	@echo "$(YELLOW)Next steps:$(NC)"
	@echo "  1. Connect your Waveshare ESP32-S3-Touch-LCD-2 via USB-C"
	@echo "  2. Run: $(CYAN)make flash$(NC)"
	@echo ""

.PHONY: compile
compile: check-cli
	@echo "$(GREEN)Compiling $(SKETCH)...$(NC)"
	@if [ ! -f "$(SKETCH_DIR)/$(SKETCH)" ]; then \
		echo "$(RED)Error: $(SKETCH_DIR)/$(SKETCH) not found!$(NC)"; \
		exit 1; \
	fi
	@arduino-cli compile --fqbn $(BOARD_FQBN) $(SKETCH_DIR)
	@echo "$(GREEN)✓ Compilation successful$(NC)"

.PHONY: flash upload
flash upload: check-cli
	@echo "$(GREEN)Flashing $(SKETCH)...$(NC)"
	@if [ -z "$(PORT)" ]; then \
		echo "$(RED)Error: No ESP32-S3 detected!$(NC)"; \
		echo "$(YELLOW)Connect your board and try again$(NC)"; \
		echo "$(YELLOW)Tip: Hold BOOT button while connecting USB$(NC)"; \
		echo "Run: $(CYAN)make boards$(NC) to see connected devices"; \
		exit 1; \
	fi
	@echo "$(BLUE)Port: $(PORT)$(NC)"
	@arduino-cli compile --fqbn $(BOARD_FQBN) $(SKETCH_DIR)
	@arduino-cli upload -p $(PORT) --fqbn $(BOARD_FQBN) $(SKETCH_DIR)
	@echo ""
	@echo "$(GREEN)✓ Upload complete!$(NC)"
	@echo "Run: $(CYAN)make monitor$(NC) to view output"

.PHONY: monitor
monitor: check-cli
	@if [ -z "$(PORT)" ]; then \
		echo "$(RED)Error: No board detected!$(NC)"; \
		exit 1; \
	fi
	@echo "$(GREEN)Serial Monitor @ $(PORT) ($(BAUD) baud)$(NC)"
	@echo "$(YELLOW)Press Ctrl+C to exit$(NC)"
	@echo "$(BLUE)════════════════════════════════════════════════$(NC)"
	@arduino-cli monitor -p $(PORT) -c baudrate=$(BAUD)

.PHONY: all
all: flash monitor

.PHONY: clean
clean:
	@echo "$(GREEN)Cleaning build artifacts...$(NC)"
	@rm -rf $(SKETCH_DIR)/build
	@echo "$(GREEN)✓ Clean$(NC)"

.PHONY: boards
boards: check-cli
	@echo "$(GREEN)Connected boards:$(NC)"
	@arduino-cli board list

.PHONY: config
config: check-cli
	@echo "$(GREEN)Current Configuration:$(NC)"
	@echo "  $(BLUE)Sketch:$(NC)    $(SKETCH_DIR)/$(SKETCH)"
	@echo "  $(BLUE)Board:$(NC)     Waveshare ESP32-S3-Touch-LCD-2"
	@echo "  $(BLUE)FQBN:$(NC)      $(BOARD_FQBN)"
	@echo "  $(BLUE)Port:$(NC)      $(PORT)"
	@echo "  $(BLUE)Baud:$(NC)      $(BAUD)"
	@echo ""
	@echo "$(GREEN)Installed cores:$(NC)"
	@arduino-cli core list | grep esp32 || echo "  $(YELLOW)ESP32 core not installed. Run: make setup$(NC)"
	@echo ""
	@echo "$(GREEN)Installed libraries:$(NC)"
	@arduino-cli lib list | grep -iE "lvgl|gfx|tft|nimble|json" || echo "  $(YELLOW)Required libraries not installed. Run: make setup$(NC)"

.PHONY: update
update: check-cli
	@echo "$(GREEN)Updating cores and libraries...$(NC)"
	@arduino-cli core update-index
	@arduino-cli core upgrade
	@arduino-cli lib upgrade
	@echo "$(GREEN)✓ Updated$(NC)"

.PHONY: bootloader
bootloader:
	@echo "$(YELLOW)To enter bootloader mode:$(NC)"
	@echo "  1. Hold the BOOT button"
	@echo "  2. Press and release RESET (or reconnect USB)"
	@echo "  3. Release BOOT button"
	@echo "  4. Run: make flash"
