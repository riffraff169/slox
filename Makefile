CFLAGS = -rdynamic -D_GNU_SOURCE -g
CFLAGS += $(shell pkg-config --cflags readline libpcre2-8)

#-Wall -Wextra -g
TARGET = slox
SRC = $(wildcard *.c)
HEADERS = $(wildcard *.h)
OBJ = $(SRC:.c=.o)
DEPS = $(SRC:.c=.d)
CC = gcc
LIBS = -lm
LIBS += $(shell pkg-config --libs readline libpcre2-8) 
MOD_DIR = modules
MODULES = sha1 ssl
#OBJS    := ${patsubst %.c, %.o, ${wildcard *.c}}

all:	$(TARGET) modules

.PHONY:	all modules clean

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(LIBS) -o $(TARGET) $(OBJ)

CFLAGS += -MMD -MP
%.o:	%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC): $(HEADERS)

modules:
	$(MAKE) -C $(MOD_DIR)

$(MOD_SO): liblox_%.so: $(MOD_DIR)/liblox_%.c
	@echo "Building module: $@"
	$(CC) $(CFLAGS) $(MOD_CFLAGS) -shared -o $@ $< $(MOD_LIBS)

clean:
	rm -rf $(OBJ) $(TARGET) $(DEPS)

-include $(DEPS)
