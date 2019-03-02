CC       = gcc
CFLAGS   = -Wall -Wpedantic
LD       = gcc
LDFLAGS  =

BIN_NAME = cfloor

all: $(BIN_NAME)

test: obj/test.o obj/networking.o
	$(LD) -o $@ $^

obj/networking.o: src/networking.h src/headers.h

obj/%.o: src/%.c obj
	$(CC) -c -o $@ $<

obj:
	@mkdir -p obj

clean:
	@echo "Cleaning up..."
	@rm -f obj/*.o
	@rm -f test
	@rm -f $(BIN_NAME)
