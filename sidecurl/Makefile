CFLAGS += -I../buildtarget/include
CFLAGS += -L$(PWD)/../buildtarget/lib
CFLAGS += -lcurl
CFLAGS += -Wno-deprecated-declarations
CFLAGS += -g

sidecurl: sidecurl.c
	$(CC) $^ $(CFLAGS) -o $@
