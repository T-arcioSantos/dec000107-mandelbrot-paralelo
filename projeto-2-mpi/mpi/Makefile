COMMON_DIR = ../../projeto-1-openmp/codigo/comum
OUT_DIR ?= .
CFLAGS = -std=c11 -O2 -Wall -Wextra -Wpedantic -Wconversion -ffp-contract=off

ifeq ($(OS),Windows_NT)
MSYS2_ROOT ?= C:/msys64
UCRT64_ROOT ?= $(MSYS2_ROOT)/ucrt64
CC = $(UCRT64_ROOT)/bin/gcc.exe
MPI_CFLAGS = -I$(UCRT64_ROOT)/include
MPI_LIBS = -L$(UCRT64_ROOT)/lib -l:libmsmpi.dll.a
EXE = .exe
else
CC = mpicc
MPI_CFLAGS =
MPI_LIBS = -lm
EXE =
endif

TARGET = $(OUT_DIR)/mandelbrot_mpi$(EXE)
SOURCES = mandelbrot_mpi_main.c mandelbrot_mpi.c \
	$(COMMON_DIR)/mandelbrot.c $(COMMON_DIR)/mandelbrot_io.c
HEADERS = mandelbrot_mpi.h \
	$(COMMON_DIR)/mandelbrot.h $(COMMON_DIR)/mandelbrot_io.h

.PHONY: all
all: $(TARGET)

$(TARGET): $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) -I$(COMMON_DIR) $(MPI_CFLAGS) $(SOURCES) -o $@ $(MPI_LIBS)
