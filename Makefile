CFLAGS = -rdynamic
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

all:	$(TARGET)

.PHONY:	clean all


$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(LIBS) -o $(TARGET) $(OBJ)

CFLAGS += -MMD -MP

%.o:	%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC): $(HEADERS)

-include $(DEPS)

clean:
	rm -rf $(OBJ) $(TARGET) $(DEPS) 

