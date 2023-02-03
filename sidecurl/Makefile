CURL_LIB_DIR = $(HOME)/curl/lib/.libs
QUICHE_LIB_DIR = $(HOME)/quiche-sidecar/target/release

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

sidecurl: sidecurl.c writeout.c
	$(CC) $^ $(CFLAGS) -o $@
