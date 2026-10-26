CFLAGS = -ggdb -Wall -Wextra -MMD -MP

CURRENT_DIR := $(shell dirname $(realpath $(firstword $(MAKEFILE_LIST))))
BUILD_DIR = build

# SRC = $(wildcard src/*/*.c)
SRC = $(shell find src -name '*.c' -print)
OBJ = $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SRC))

all: axc

$(OBJ): $(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

axc: $(OBJ)
	gcc $(CFLAGS) $^ -o axc

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

	mkdir -p $(patsubst src/%,$(BUILD_DIR)/%,$(sort $(dir $(SRC))))

.PHONY: all clean compile format

clean:
	rm -rf $(BUILD_DIR)

compile: all
	./axc expr.ax
	qbe out.ssa -o out.s
	gcc out.s -o out

format:
	find src -name '*.[ch]' | xargs clang-format -i

-include $(OBJ:.o=.d)
