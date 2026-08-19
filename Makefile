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

VERSION = 1.4.27
RELEASE = 1
RPM_SOURCES = $(HOME)/rpmbuild/SOURCES

MODULES = sha1 ssl

all: $(TARGET) modules

.PHONY: all modules clean

$(TARGET): $(OBJ)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(LIBS) -o $(TARGET) $(OBJ)

$(SRC_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

modules:
	@chmod +x modules/build_modules.sh
	@cd modules && ./build_modules.sh
	#$(MAKE) -C $(MOD_DIR)

#$(MOD_SO): liblox_%.so; $(MOD_DIR)/liblox_%.c
#	@echo "Building module: %@"
#	$(CC) $(CFLAGS) $(MOD_CFLAGS) -shared -o  $@ $< $(MOD_LIBS)

clean:
	rm -rf $(SRC_DIR)/*.o $(SRC_DIR)/*.d $(BIN_DIR)

update-spec:
	sed -i 's/^Version:.*/Version:    $(VERSION)/' slox.spec
	sed -i 's/^Release:.*/Release:    $(RELEASE)%{?dist}/' slox.spec

dist: #update-spec
	@mkdir -p $(RPM_SOURCES)
	tar --exclude-vcs --transform "s/^/slox-$(VERSION)\//" \
		-czf $(RPM_SOURCES)/slox-$(VERSION).tar.gz \
		src modules lib examples Makefile README.md slox.spec extras tests scripts docs
	cp slox.spec $(HOME)/rpmbuild/SPECS/
	@echo "Clean whitelisted source tarball created at: $(RPM_SOURCES)/slox-$(VERSION).tar.gz"

-include $(DEPS)
