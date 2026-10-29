#ifndef MANDELBROT_MPI_H
#define MANDELBROT_MPI_H

#include "mandelbrot.h"

#include <mpi.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    MPI_STRATEGY_CONTIGUOUS = 0,
    MPI_STRATEGY_CYCLIC = 1
} MpiStrategy;

typedef struct {
    MandelbrotConfig mandelbrot;
    MpiStrategy strategy;
    int no_write;
} MpiConfig;

typedef struct {
    double compute_s;
    double distribution_s;
    double communication_s;
    double gather_s;
    double total_s;
    unsigned long long rows;
    unsigned long long tasks;
} RankMetrics;

void mpi_check(int error, const char *operation, int rank);

#define MPI_CHECK(rank_value, operation) \
    mpi_check((operation), #operation, (rank_value))

void mpi_compute_partition(MpiStrategy strategy, size_t height,
                           int rank, int processes,
                           size_t *first_row, size_t *rows);
void mpi_run_contiguous(const MpiConfig *options, int rank, int processes,
                        int32_t *local_matrix, int32_t *root_matrix,
                        const int *counts, const int *displacements,
                        RankMetrics *metrics);

void mpi_run_cyclic(const MpiConfig *options, int rank, int processes,
                    int32_t *local_matrix, int32_t *gathered_matrix,
                    int32_t *root_matrix, const int *counts,
                    const int *displacements, RankMetrics *metrics);

#endif
