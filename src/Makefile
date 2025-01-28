CFLAGS ?= -O2 -Wall -Werror -I.

default: dmiprint


dmiprint: dmiprint.c
	$(CC) $(CFLAGS) dmiprint.c -o dmiprint

clean:
	$(RM) dmiprint

.PHONY: default clean
