CFLAGS = -rdynamic -D_GNU_SOURCE -g
CFLAGS += $(shell pkg-config --cflags readline libpcre2-8)
CFLAGS += -Isrc -MMD -MP

SRC_DIR = src
BIN_DIR = bin
MOD_DIR = modules

TARGET = $(BIN_DIR)/slox
SRC = $(wildcard $(SRC_DIR)/*.c)
OBJ = $(SRC:.c=.o)
DEPS = $(SRC:.c=.d)

CC = gcc
LIBS = -lm
LIBS += $(shell pkg-config --libs readline libpcre2-8)

MODULES = sha1 ssl

all: $(TARGET) modules

.PHONY: all modules clean

$(TARGET): $(OBJ)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(LIBS) -o $(TARGET) $(OBJ)

$(SRC_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

modules:
	$(MAKE) -C $(MOD_DIR)

$(MOD_SO): liblox_%.so; $(MOD_DIR)/liblox_%.c
	@echo "Building m odule: %@"
	$(CC) $(CFLAGS) $(MOD_CFLAGS) -shared -o  $@ $< $(MOD_LIBS)

clean:
	rm -rf $(SRC_DIR)/*.o $(SRC_DIR)/*.d $(BIN_DIR)

-include $(DEPS)
