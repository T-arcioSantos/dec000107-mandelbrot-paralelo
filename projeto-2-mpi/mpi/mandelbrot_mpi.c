#include "mandelbrot_mpi.h"

#include <stddef.h>
#include <string.h>

/* Calcula a particao escolhida sem mudar a ordem de nenhuma linha. */
void mpi_compute_partition(MpiStrategy strategy, size_t height,
                           int rank, int processes,
                           size_t *first_row, size_t *rows)
{
    if (strategy == MPI_STRATEGY_CONTIGUOUS) {
        const size_t base = height / (size_t) processes;
        const size_t remainder = height % (size_t) processes;
        const size_t process = (size_t) rank;

        /* As linhas que sobram são distribuídas aos primeiros ranks. */
        *rows = base + (process < remainder ? 1u : 0u);
        *first_row = process * base +
                     (process < remainder ? process : remainder);
    } else {
        const size_t process = (size_t) processes;
        const size_t process_rank = (size_t) rank;

        /* Rank p recebe p, p+P, p+2P, ...; ranks vazios recebem zero. */
        *first_row = process_rank;
        *rows = process_rank < height
            ? 1u + (height - 1u - process_rank) / process
            : 0u;
    }
}

void mpi_run_contiguous(const MpiConfig *options, int rank, int processes,
                        int32_t *local_matrix, int32_t *root_matrix,
                        const int *counts, const int *displacements,
                        RankMetrics *metrics)
{
    size_t first_row = 0;
    size_t rows = 0;
    size_t local_row;
    int32_t dummy = 0;
    const MandelbrotConfig *config = &options->mandelbrot;
    double stage = MPI_Wtime();

    mpi_compute_partition(MPI_STRATEGY_CONTIGUOUS, config->altura,
                          rank, processes, &first_row, &rows);
    metrics->distribution_s += MPI_Wtime() - stage;

    stage = MPI_Wtime();
    for (local_row = 0; local_row < rows; ++local_row) {
        mandelbrot_gerar_linha_local(
            config, first_row + local_row,
            local_matrix + local_row * config->largura);
    }
    metrics->compute_s += MPI_Wtime() - stage;
    metrics->rows = (unsigned long long) rows;
    metrics->tasks = rows > 0 ? 1ULL : 0ULL;

    /* Os deslocamentos colocam cada bloco diretamente na matriz global. */
    stage = MPI_Wtime();
    MPI_CHECK(rank, MPI_Gatherv(
        local_matrix != NULL ? local_matrix : &dummy,
        (int) (rows * config->largura),
        MPI_INT32_T,
        rank == 0 ? root_matrix : &dummy,
        rank == 0 ? counts : NULL,
        rank == 0 ? displacements : NULL,
        MPI_INT32_T,
        0,
        MPI_COMM_WORLD));
    metrics->gather_s += MPI_Wtime() - stage;
    metrics->communication_s += metrics->gather_s;
}

void mpi_run_cyclic(const MpiConfig *options, int rank, int processes,
                    int32_t *local_matrix, int32_t *gathered_matrix,
                    int32_t *root_matrix, const int *counts,
                    const int *displacements, RankMetrics *metrics)
{
    const MandelbrotConfig *config = &options->mandelbrot;
    size_t first_row = 0;
    size_t rows = 0;
    size_t local_row;
    int32_t dummy = 0;
    double stage = MPI_Wtime();

    mpi_compute_partition(MPI_STRATEGY_CYCLIC, config->altura,
                          rank, processes, &first_row, &rows);
    metrics->distribution_s += MPI_Wtime() - stage;

    stage = MPI_Wtime();
    for (local_row = 0; local_row < rows; ++local_row) {
        const size_t global_row = first_row + local_row * (size_t) processes;
        mandelbrot_gerar_linha_local(
            config, global_row,
            local_matrix + local_row * config->largura);
    }
    metrics->compute_s += MPI_Wtime() - stage;
    metrics->rows = (unsigned long long) rows;
    metrics->tasks = (unsigned long long) rows;

    /* A coleta coletiva pode incluir espera; o rank 0 recebe os blocos compactos. */
    stage = MPI_Wtime();
    MPI_CHECK(rank, MPI_Gatherv(
        local_matrix != NULL ? local_matrix : &dummy,
        (int) (rows * config->largura),
        MPI_INT32_T,
        rank == 0 ? gathered_matrix : &dummy,
        rank == 0 ? counts : NULL,
        rank == 0 ? displacements : NULL,
        MPI_INT32_T,
        0,
        MPI_COMM_WORLD));
    {
        const double communication = MPI_Wtime() - stage;
        metrics->communication_s += communication;
        metrics->gather_s += communication;
    }

    /* O rank 0 recoloca cada linha na posição row-major da imagem final. */
    if (rank == 0) {
        int process_rank;
        stage = MPI_Wtime();
        for (process_rank = 0; process_rank < processes; ++process_rank) {
            size_t source_row;
            size_t local_index = 0;
            for (source_row = (size_t) process_rank;
                 source_row < config->altura;
                 source_row += (size_t) processes, ++local_index) {
                memcpy(root_matrix + source_row * config->largura,
                       gathered_matrix +
                           (size_t) displacements[process_rank] +
                           local_index * config->largura,
                       config->largura * sizeof(*root_matrix));
            }
        }
        metrics->gather_s += MPI_Wtime() - stage;
    }
}
