CC       = gcc
CFLAGS   = -std=c99 -Wall -D_POSIX_C_SOURCE=201112L -D_XOPEN_SOURCE=500 -D_GNU_SOURCE -static -g
LD       = gcc
LDFLAGS  = -lpthread -lrt

BIN_NAME = cfloor

OBJS     = obj/networking.o obj/linked.o obj/logging.o obj/signals.o obj/headers.o obj/misc.o obj/status.o obj/files.o obj/mime.o obj/cgi.o obj/util.o obj/ssl.o
DEPS     = $(OBJS:%.o=%.d)

all: $(BIN_NAME)

ssl: CFLAGS += -DSSL_SUPPORT -Icrypto
ssl: LDFLAGS += -lcrypto -lssl
ssl: obj/ssl.o $(BIN_NAME)

$(BIN_NAME): obj/main.o $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

test: obj/test.o $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

valgrind: CFLAGS += -static -g
valgrind: clean test
	valgrind --leak-check=yes ./test

-include $(DEPS)

obj/%.o: src/%.c
	$(CC) $(CFLAGS) -MMD -c -o $@ $<

obj:
	@mkdir -p obj

clean:
	@echo "Cleaning up..."
	@rm -f obj/*.o
	@rm -f obj/*.d
	@rm -f test
	@rm -f $(BIN_NAME)
