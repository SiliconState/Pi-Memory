# pi-memory build
#
# Build:   make
# Install: make install
# Clean:   make clean

CC      = cc
CFLAGS  = -Wall -Wextra -Wpedantic -O2 -std=c11
LDFLAGS = -lsqlite3
TARGET  = pi-memory
SRC     = pi-memory.c
INSTALL = /usr/local/bin

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

install: $(TARGET)
	cp $(TARGET) $(INSTALL)/$(TARGET)
	@echo "installed -> $(INSTALL)/$(TARGET)"

uninstall:
	rm -f $(INSTALL)/$(TARGET)
	@echo "removed $(INSTALL)/$(TARGET)"

clean:
	rm -f $(TARGET)

.PHONY: install uninstall clean
