# pi-memory build (bundled SQLite amalgamation — no system sqlite3 required)
#
# Build:   make
# Install: make install   (installs to ~/.pi/memory/pi-memory)
# Clean:   make clean

CC      = cc
CFLAGS  = -Wall -Wextra -Wpedantic -O2 -std=c11
TARGET  = pi-memory
SRC     = pi-memory.c sqlite3.c
INSTALL_DIR = $(HOME)/.pi/memory

$(TARGET): $(SRC) sqlite3.h
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) -lpthread -ldl -lm

install: $(TARGET)
	mkdir -p $(INSTALL_DIR)
	cp $(TARGET) $(INSTALL_DIR)/$(TARGET)
	chmod +x $(INSTALL_DIR)/$(TARGET)
	@echo "installed -> $(INSTALL_DIR)/$(TARGET)"

uninstall:
	rm -f $(INSTALL_DIR)/$(TARGET)
	@echo "removed $(INSTALL_DIR)/$(TARGET)"

clean:
	rm -f $(TARGET)

.PHONY: install uninstall clean
