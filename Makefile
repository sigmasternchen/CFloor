CC       = gcc
CFLAGS   = -std=c99 -Wall -D_POSIX_C_SOURCE=199506L
LD       = gcc
LDFLAGS  = -lpthread -lrt

BIN_NAME = cfloor

all: $(BIN_NAME)

test: obj/test.o obj/networking.o obj/linked.o obj/logging.o obj/signals.o
	$(LD) $(LDFLAGS) -o $@ $^
valgrind: CFLAGS += -static -g
valgrind: clean test
	valgrind --leak-check=yes ./test

obj/test.o: src/networking.h src/linked.h src/logging.h src/signals.h
obj/networking.o: src/networking.h src/headers.h src/linked.h
obj/linked.o: src/linked.h
obj/loggin.o: src/logging.h
obj/signals.o: src/signals.h

obj/%.o: src/%.c obj
	$(CC) $(CFLAGS) -c -o $@ $<

obj:
	@mkdir -p obj

clean:
	@echo "Cleaning up..."
	@rm -f obj/*.o
	@rm -f test
	@rm -f $(BIN_NAME)
