# pi-memory build (bundled SQLite amalgamation — no system sqlite3 required)
#
# Unix:
#   make
#   make install   (installs to ~/.pi/memory/pi-memory)
#
# Windows (MSVC — run from Developer Command Prompt):
#   nmake /F Makefile.win
#
# Cross-compile note: prebuilt binaries are shipped in prebuilds/
# and used by postinstall. This Makefile is for development only.

CC      = cc
CFLAGS  = -Wall -Wextra -Wpedantic -O2 -std=c11
TARGET  = pi-memory
SRC     = pi-memory.c sqlite3.c
INSTALL_DIR = $(HOME)/.pi/memory

# macOS doesn't need -ldl or -lpthread (included in system libs)
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
  LIBS = -lm
else
  LIBS = -lpthread -ldl -lm
endif

$(TARGET): $(SRC) sqlite3.h compat.h
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LIBS)

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
