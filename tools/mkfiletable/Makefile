# mkfiletable — build a mod's filetable.dat from its JSON manifest.
# Built the way tools/mkrom is: plain make, no cmake, no install step.

CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=c99
CFLAGS  += -Ivendor/parson

OBJS    = mkfiletable.o pdft_write.o vendor/parson/parson.o

all: mkfiletable

mkfiletable: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

# The test driver is not part of the tool; see pdft_test.py.
pdft_reencode: pdft_reencode.o pdft_write.o
	$(CC) $(CFLAGS) -o $@ pdft_reencode.o pdft_write.o

test: mkfiletable pdft_reencode
	python3 pdft_test.py ./pdft_reencode

clean:
	rm -f mkfiletable pdft_reencode $(OBJS) pdft_reencode.o

.PHONY: all test clean
