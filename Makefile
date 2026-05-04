CFLAGS = -rdynamic -D_GNU_SOURCE -g
CFLAGS += $(shell pkg-config --cflags gobject-introspection-1.0 gtk4 readline libpcre2-8)

#-Wall -Wextra -g
TARGET = slox
SRC = $(wildcard *.c)
HEADERS = $(wildcard *.h)
OBJ = $(SRC:.c=.o)
DEPS = $(SRC:.c=.d)
CC = gcc
LIBS = -lm
LIBS += $(shell pkg-config --libs gobject-introspection-1.0 gtk4 readline libpcre2-8) 
#OBJS    := ${patsubst %.c, %.o, ${wildcard *.c}}

MOD_DIR = modules
MOD_SRC = $(wildcard $(MOD_DIR)/liblox_*.c)
MOD_SO = $(patsubst $(MOD_DIR)/%.c,%.so,$(MOD_SRC))

MOD_CFLAGS = -fPIC $(shell pkg-config --cflags gobject-introspection-1.0 gtk4 gdk)
MOD_LIBS = $(shell pkg-config --libs gobject-introspection-1.0 gtk4 gdk)

all:	$(TARGET) $(MOD_SO)

.all: $(TARGET) $(MOD_SO)

.PHONY:	clean all

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(LIBS) -o $(TARGET) $(OBJ)

CFLAGS += -MMD -MP
%.o:	%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC): $(HEADERS)

$(MOD_SO): liblox_%.so: $(MOD_DIR)/liblox_%.c
	@echo "Building module: $@"
	$(CC) $(CFLAGS) $(MOD_CFLAGS) -shared -o $@ $< $(MOD_LIBS)

clean:
	rm -rf $(OBJ) $(TARGET) $(DEPS) $(MOD_SO)
	rm -rf $(MOD_DIR)/*.d

-include $(DEPS)
