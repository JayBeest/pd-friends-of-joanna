CC ?= cc
CFLAGS = -Wall -Wextra -O2 -std=c99
LDFLAGS = -lz

PREFIX ?= /usr/local

SRCS = pdheadedit.c
OBJS = $(SRCS:.c=.o)
BIN  = pdheadedit

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(BIN)

install: $(BIN)
	install -d $(PREFIX)/bin
	install -m 755 $(BIN) $(PREFIX)/bin/

.PHONY: all clean install
