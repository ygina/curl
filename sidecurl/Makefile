SIDECAR_HOME = $(HOME)/sidecar
CURL_LIB_DIR = $(SIDECAR_HOME)/http3_integration/curl/lib/.libs
QUICHE_LIB_DIR = $(SIDECAR_HOME)/http3_integration/quiche/target/release

CFLAGS += -I../include
CFLAGS += -Wno-deprecated-declarations
CFLAGS += -g
CFLAGS += -O3

# Link curl
CFLAGS += -L$(CURL_LIB_DIR)
CFLAGS += -Xlinker "-rpath=$(CURL_LIB_DIR)"
CFLAGS += -lcurl

# Link quiche
CFLAGS += -L$(QUICHE_LIB_DIR)
CFLAGS += -Xlinker "-rpath=$(QUICHE_LIB_DIR)"
CFLAGS += -lquiche

all: sidecurl tcpsidecurl

sidecurl: sidecurl.c writeout.c
	$(CC) $^ $(CFLAGS) -o $@

tcpsidecurl: tcpsidecurl.c writeout.c
	$(CC) $^ $(CFLAGS) -o $@

.PHONY: all
