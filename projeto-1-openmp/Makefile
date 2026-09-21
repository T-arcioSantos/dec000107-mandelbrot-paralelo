CC ?= gcc
CPPFLAGS := -Icodigo/comum
CFLAGS := -std=c11 -O2 -Wall -Wextra -Wpedantic -Wconversion -ffp-contract=off
LDLIBS := -lm

BUILD_DIR := compilacao
TEST_TARGET := $(BUILD_DIR)/test_mandelbrot
TEST_SOURCES := codigo/comum/mandelbrot.c validacao/casos/test_mandelbrot.c

.PHONY: all test clean

all: $(TEST_TARGET)

$(TEST_TARGET): $(TEST_SOURCES) codigo/comum/mandelbrot.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_SOURCES) -o $@ $(LDLIBS)

test: $(TEST_TARGET)
	./$(TEST_TARGET)

$(BUILD_DIR):
	mkdir -p $@

clean:
	rm -f $(TEST_TARGET)