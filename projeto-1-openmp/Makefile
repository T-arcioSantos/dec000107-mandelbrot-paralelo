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

MATRIX_TEST_TARGET := $(BUILD_DIR)/test_matriz_serial
MATRIX_TEST_SOURCES := codigo/comum/mandelbrot.c \
	validacao/casos/test_matriz_serial.c


.PHONY: test-matriz clean-matriz


test-matriz: $(MATRIX_TEST_TARGET)
	./$(MATRIX_TEST_TARGET)


$(MATRIX_TEST_TARGET): $(MATRIX_TEST_SOURCES) codigo/comum/mandelbrot.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(MATRIX_TEST_SOURCES) -o $@ $(LDLIBS)


clean-matriz:
	rm -f $(MATRIX_TEST_TARGET)

SERIAL_TARGET := $(BUILD_DIR)/mandelbrot_serial
SERIAL_SOURCES := codigo/comum/mandelbrot.c \
	codigo/serial/mandelbrot_serial.c


.PHONY: serial clean-serial


serial: $(SERIAL_TARGET)


$(SERIAL_TARGET): $(SERIAL_SOURCES) codigo/comum/mandelbrot.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SERIAL_SOURCES) -o $@ $(LDLIBS)


clean-serial:
	rm -f $(SERIAL_TARGET)

SERIAL_SOURCES += codigo/comum/mandelbrot_io.c


$(SERIAL_TARGET): codigo/comum/mandelbrot_io.c codigo/comum/mandelbrot_io.h