CFLAGS += -I../include
CFLAGS += -L$(PWD)/../lib/.libs
CFLAGS += -lcurl
CFLAGS += -Wno-deprecated-declarations
CFLAGS += -g

sidecurl: sidecurl.c writeout.c
	$(CC) $^ $(CFLAGS) -o $@
