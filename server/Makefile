# The compiler is determined based on whether CROSS_COMPILE is defined
ifdef CROSS_COMPILE
CC := $(CROSS_COMPILE)gcc
else
CC := gcc
endif

CFLAGS ?= -Wall -Werror
LDFLAGS ?=

.PHONY: all default clean

all: default

default:
	$(CC) $(CFLAGS) $(LDFLAGS) -o aesdsocket aesdsocket.c

clean:	
	rm -f *.o aesdsocket
