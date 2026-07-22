CFLAGS += -fPIC $(shell pkg-config --cflags gobject-introspection-1.0 gtk4 gdk) -I../src
LIBS += $(shell pkg-config --libs gobject-introspection-1.0 gtk4 gdk)
LDFLAGS = -shared
BEARSSL_DIR = BearSSL
BEARSSL_A = $(BEARSSL_DIR)/build/libbearssl.a

TARGET_DIR = .

MODULE_TARGETS = $(TARGET_DIR)/liblox_sha1.so $(TARGET_DIR)/liblox_image.so #$(TARGET_DIR)/liblox_gi.so

ifneq ($(wildcard BearSSL/src),)
	MODULE_TARGETS += $(TARGET_DIR)/liblox_ssl.so
endif

.PHONY: all clean
all: $(MODULE_TARGETS)

$(TARGET_DIR)/liblox_sha1.so: liblox_sha1.c
	@echo "Building standard module: $@"
	$(CC) $(CFLAGS) -shared -o $@ $<


#BEARSSL_INC = -IBearSSL/inc -IBearSSL/src -DHAVE_BEARSSL
#BEARSSL_SRC = $(wildcard BearSSL/src/**/*.c) \
#			  $(wildcard BearSSL/src/*.c)

$(TARGET_DIR)/liblox_ssl.so: CFLAGS += -I$(BEARSSL_DIR)/inc -fPIC -DHAVE_BEARSSL
$(TARGET_DIR)/liblox_ssl.so: LDFLAGS += -Wl,--whole-archive $(BEARSSL_A) -Wl,--no-whole-archive

$(TARGET_DIR)/liblox_ssl.so: liblox_ssl.c
	make -C BearSSL

	@echo "Creating TrustAnchors.h"
	$(BEARSSL_DIR)/build/brssl ta /etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem > TrustAnchors.h
	@echo "Building complex module: $@"

	$(CC) $(CFLAGS) -shared -o $@ liblox_ssl.c $(LDFLAGS)

$(TARGET_DIR)/liblox_image.so: liblox_image.c
	@echo "Building standard module: $@"
	$(CC) $(CFLAGS) -shared -o $@ $<

clean:
	rm -f $(MODULE_TARGETS) *.d
