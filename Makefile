CC       = gcc
CFLAGS   = -Wall -Wpedantic
LD       = gcc
LDFLAGS  = -lpthread -lrt

BIN_NAME = cfloor

all: $(BIN_NAME)

test: obj/test.o obj/networking.o obj/linked.o
	$(LD) $(LDFLAGS) -o $@ $^
valgrind: test
	valgrind --leak-check=yes ./test

obj/test.o: src/networking.h
obj/networking.o: src/networking.h src/headers.h
obj/linked.o: src/linked.h

obj/%.o: src/%.c obj
	$(CC) $(CFLAGS) -c -o $@ $<

obj:
	@mkdir -p obj

clean:
	@echo "Cleaning up..."
	@rm -f obj/*.o
	@rm -f test
	@rm -f $(BIN_NAME)
