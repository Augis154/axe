CFLAGS = -ggdb

SRC = $(wildcard src/*.c)
OBJ = $(patsubst src/%.c, build/%.o, $(SRC))

all: axc

$(OBJ): build/%.o: src/%.c | build
	gcc $(CFLAGS) -c $< -o $@

axc: $(OBJ)
	gcc $(CFLAGS) $^ -o axc

build:
	mkdir -p build

.PHONY: clean

clean:
	rm -r build
