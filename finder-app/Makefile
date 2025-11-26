# The compiler is determined based on whether CROSS_COMPILE is defined
ifdef CROSS_COMPILE
CC := $(CROSS_COMPILE)gcc
else
CC := gcc
endif

.PHONY: default clean

default:
	$(CC) -o writer writer.c

clean:	
	rm -f *.o writer
