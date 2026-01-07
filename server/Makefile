
.PHONY: all clean

CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -g
USE_AESD_CHAR_DEVICE ?= 1

all:
	$(CC) $(CFLAGS) -DUSE_AESD_CHAR_DEVICE=$(USE_AESD_CHAR_DEVICE) aesdsocket.c -o aesdsocket

clean:
	rm -f aesdsocket
