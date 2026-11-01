CFLAGS = -ggdb -Wall -Wextra -MMD -MP

CURRENT_DIR := $(shell dirname $(realpath $(firstword $(MAKEFILE_LIST))))
BUILD_DIR = build

# SRC = $(wildcard src/*/*.c)
SRC = $(shell find src -name '*.c' -print)
OBJ = $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SRC))

all: mothc

$(OBJ): $(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

mothc: $(OBJ)
	gcc $(CFLAGS) $^ -o mothc

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

	mkdir -p $(patsubst src/%,$(BUILD_DIR)/%,$(sort $(dir $(SRC))))

.PHONY: all clean compile format test

clean:
	rm -rf $(BUILD_DIR)

compile: all
	./mothc test.mh out.ssa
	qbe out.ssa -o out.s
	gcc out.s -o out

format:
	find src -name '*.[ch]' | xargs clang-format -i

-include $(OBJ:.o=.d)
