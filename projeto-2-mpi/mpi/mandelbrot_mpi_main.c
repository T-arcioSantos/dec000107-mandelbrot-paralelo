#include "mandelbrot_mpi.h"
#include "mandelbrot_io.h"

#include <errno.h>
#include <float.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void mpi_check(int error, const char *operation, int rank)
{
    if (error != MPI_SUCCESS) {
        char message[MPI_MAX_ERROR_STRING];
        int length = 0;
        MPI_Error_string(error, message, &length);
        fprintf(stderr, "rank %d: %s failed: %.*s\n",
                rank, operation, length, message);
        MPI_Abort(MPI_COMM_WORLD, error);
    }
}

static void show_usage(const char *program)
{
    fprintf(stderr,
        "Uso: %s [opcoes]\n"
        "Padrao: 4096x4096, MAX_ITER=1000, estrategia contiguous.\n"
        "  --strategy contiguous|cyclic  distribuicao das linhas\n"
        "  --largura N --altura N      dimensoes da imagem\n"
        "  --max-iter N                limite de iteracoes\n"
        "  --re-min X --re-max X       intervalo real\n"
        "  --im-min X --im-max X       intervalo imaginario\n"
        "  --no-write                  nao grava binario/PGM (para repeticoes cronometradas)\n"
        "  --ajuda                     mostra esta ajuda\n",
        program);
}

static int parse_size(const char *text, size_t *value)
{
    char *end = NULL;
    unsigned long long parsed;

    errno = 0;
    if (text[0] == '-') {
        return 0;
    }
    parsed = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' ||
        parsed == 0 || parsed > (unsigned long long) SIZE_MAX) {
        return 0;
    }
    *value = (size_t) parsed;
    return 1;
}

static int parse_positive_int(const char *text, int *value)
{
    char *end = NULL;
    long parsed;

    errno = 0;
    parsed = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' ||
        parsed <= 0 || parsed > INT_MAX) {
        return 0;
    }
    *value = (int) parsed;
    return 1;
}

static int parse_finite_double(const char *text, double *value)
{
    char *end = NULL;
    double parsed;

    errno = 0;
    parsed = strtod(text, &end);
    if (errno != 0 || end == text || *end != '\0' || !isfinite(parsed)) {
        return 0;
    }
    *value = parsed;
    return 1;
}

static int get_value(int argc, char **argv, int *index, const char **value)
{
    if (*index + 1 >= argc) {
        fprintf(stderr, "Erro: faltou um valor depois de %s.\n", argv[*index]);
        return 0;
    }
    ++(*index);
    *value = argv[*index];
    return 1;
}

static int parse_arguments(int argc, char **argv, MpiConfig *options)
{
    int index;

    options->mandelbrot.re_min = -2.0;
    options->mandelbrot.re_max = 1.0;
    options->mandelbrot.im_min = -1.5;
    options->mandelbrot.im_max = 1.5;
    options->mandelbrot.largura = 4096;
    options->mandelbrot.altura = 4096;
    options->mandelbrot.max_iter = 1000;
    options->strategy = MPI_STRATEGY_CONTIGUOUS;
    options->no_write = 0;

    for (index = 1; index < argc; ++index) {
        const char *option = argv[index];
        const char *value = NULL;
        int valid = 1;

        if (strcmp(option, "--ajuda") == 0) {
            show_usage(argv[0]);
            return 0;
        }
        if (strcmp(option, "--no-write") == 0) {
            options->no_write = 1;
            continue;
        }
        if (strcmp(option, "--strategy") == 0) {
            if (!get_value(argc, argv, &index, &value)) {
                return -1;
            }
            if (strcmp(value, "contiguous") == 0) {
                options->strategy = MPI_STRATEGY_CONTIGUOUS;
            } else if (strcmp(value, "cyclic") == 0) {
                options->strategy = MPI_STRATEGY_CYCLIC;
            } else {
                fprintf(stderr, "Erro: estrategia desconhecida: %s.\n", value);
                return -1;
            }
            continue;
        }
        if (strcmp(option, "--largura") != 0 &&
            strcmp(option, "--altura") != 0 &&
            strcmp(option, "--max-iter") != 0 &&
            strcmp(option, "--re-min") != 0 &&
            strcmp(option, "--re-max") != 0 &&
            strcmp(option, "--im-min") != 0 &&
            strcmp(option, "--im-max") != 0) {
            fprintf(stderr, "Erro: opcao desconhecida: %s.\n", option);
            return -1;
        }
        if (!get_value(argc, argv, &index, &value)) {
            return -1;
        }

        if (strcmp(option, "--largura") == 0) {
            valid = parse_size(value, &options->mandelbrot.largura);
        } else if (strcmp(option, "--altura") == 0) {
            valid = parse_size(value, &options->mandelbrot.altura);
        } else if (strcmp(option, "--max-iter") == 0) {
            int parsed;
            valid = parse_positive_int(value, &parsed);
            if (valid) {
                options->mandelbrot.max_iter = (int32_t) parsed;
            }
        } else if (strcmp(option, "--re-min") == 0) {
            valid = parse_finite_double(value, &options->mandelbrot.re_min);
        } else if (strcmp(option, "--re-max") == 0) {
            valid = parse_finite_double(value, &options->mandelbrot.re_max);
        } else if (strcmp(option, "--im-min") == 0) {
            valid = parse_finite_double(value, &options->mandelbrot.im_min);
        } else {
            valid = parse_finite_double(value, &options->mandelbrot.im_max);
        }

        if (!valid) {
            fprintf(stderr, "Erro: valor invalido para %s: %s.\n",
                    option, value);
            return -1;
        }
    }

    if (options->mandelbrot.re_min > options->mandelbrot.re_max ||
        options->mandelbrot.im_min > options->mandelbrot.im_max) {
        fputs("Erro: limite inferior maior que limite superior.\n", stderr);
        return -1;
    }

    if (options->mandelbrot.largura >
        SIZE_MAX / options->mandelbrot.altura) {
        fputs("Erro: dimensoes causam overflow.\n", stderr);
        return -1;
    }

    {
        const size_t total = options->mandelbrot.largura *
                             options->mandelbrot.altura;
        if (total > SIZE_MAX / sizeof(int32_t) ||
            total > (size_t) INT_MAX) {
            fputs("Erro: matriz excede o limite de contagem MPI int.\n",
                  stderr);
            return -1;
        }
    }

    return 1;
}

static const char *strategy_name(MpiStrategy strategy)
{
    return strategy == MPI_STRATEGY_CYCLIC ? "cyclic" : "contiguous";
}

static int file_exists(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return 0;
    }
    fclose(file);
    return 1;
}

static int save_new_output(const MpiConfig *options, const int32_t *matrix,
                           int processes, int rank, double *write_s)
{
    char binary_path[512];
    char image_path[512];
    const MandelbrotConfig *config = &options->mandelbrot;

    if (snprintf(binary_path, sizeof(binary_path),
            "saidas/matrizes/mandelbrot_mpi_%s_%zux%zu_np%d.bin",
            strategy_name(options->strategy), config->largura,
            config->altura, processes) >= (int) sizeof(binary_path) ||
        snprintf(image_path, sizeof(image_path),
            "saidas/imagens/mandelbrot_mpi_%s_%zux%zu_np%d.pgm",
            strategy_name(options->strategy), config->largura,
            config->altura, processes) >= (int) sizeof(image_path)) {
        fputs("Erro: caminho de saida grande demais.\n", stderr);
        return 0;
    }

    if (file_exists(binary_path) || file_exists(image_path)) {
        fprintf(stderr,
            "Erro: recusando sobrescrever saida existente (%s ou %s).\n",
            binary_path, image_path);
        return 0;
    }

    {
        const double start = MPI_Wtime();
        const int binary_ok =
            mandelbrot_salvar_binario(binary_path, config, matrix);
        const int image_ok = binary_ok
            ? mandelbrot_salvar_pgm(image_path, config, matrix)
            : 0;
        *write_s = MPI_Wtime() - start;
        if (!binary_ok || !image_ok) {
            return 0;
        }
    }

    if (rank == 0) {
        printf("Matriz binaria salva em %s.\n", binary_path);
        printf("Imagem PGM salva em %s.\n", image_path);
    }
    return 1;
}

static void print_metrics(const MpiConfig *options, int rank, int processes,
                          const RankMetrics *all_metrics,
                          double write_s, int write_ok)
{
    int r;
    double total_max = 0.0;
    double compute_min = DBL_MAX;
    double compute_max = 0.0;
    double worker_min = DBL_MAX;
    double worker_max = 0.0;
    double distribution_max = 0.0;
    double communication_max = 0.0;
    double gather_max = 0.0;
    double fbc_all = 0.0;
    double fbc_workers = 0.0;
    const char *name = strategy_name(options->strategy);

    if (rank != 0) {
        return;
    }

    for (r = 0; r < processes; ++r) {
        const RankMetrics *m = &all_metrics[r];
        if (m->total_s > total_max) total_max = m->total_s;
        if (m->compute_s < compute_min) compute_min = m->compute_s;
        if (m->compute_s > compute_max) compute_max = m->compute_s;
        if (m->distribution_s > distribution_max)
            distribution_max = m->distribution_s;
        if (m->communication_s > communication_max)
            communication_max = m->communication_s;
        if (m->gather_s > gather_max) gather_max = m->gather_s;

        if (m->rows > 0) {
            if (m->compute_s < worker_min) worker_min = m->compute_s;
            if (m->compute_s > worker_max) worker_max = m->compute_s;
        }
    }

    /* FBC compara a diferenca relativa entre os extremos de computacao. */
    if (compute_max > 0.0 && compute_min != DBL_MAX) {
        fbc_all = (compute_max - compute_min) / compute_max;
    }
    if (worker_max > 0.0 && worker_min != DBL_MAX) {
        fbc_workers = (worker_max - worker_min) / worker_max;
    }

    printf("RUN;%s;%d;%zu;%zu;%" PRId32 ";%.9f;%.9f;%.9f;"
           "%.9f;%.9f;%.9f;%.9f;%.9f;%.9f;%d\n",
           name, processes,
           options->mandelbrot.largura,
           options->mandelbrot.altura,
           options->mandelbrot.max_iter,
           total_max,
           compute_min == DBL_MAX ? 0.0 : compute_min,
           compute_max,
           distribution_max,
           communication_max,
           gather_max,
           write_s,
           fbc_all,
           fbc_workers,
           write_ok);
    for (r = 0; r < processes; ++r) {
        const RankMetrics *m = &all_metrics[r];
        printf("RANK;%s;%d;%d;%llu;%llu;%.9f;%.9f;%.9f;%.9f;%.9f\n",
               name, processes, r, m->rows, m->tasks,
               m->compute_s, m->distribution_s,
               m->communication_s, m->gather_s, m->total_s);
    }
    fflush(stdout);
}

int main(int argc, char **argv)
{
    int rank = 0;
    int processes = 1;
    int parse_status = 1;
    int validation_status = 1;
    int write_ok = 1;
    size_t total_elements;
    size_t local_rows = 0;
    size_t local_count = 0;
    int *counts = NULL;
    int *displacements = NULL;
    int32_t *local_matrix = NULL;
    int32_t *root_matrix = NULL;
    int32_t *gathered_matrix = NULL;
    RankMetrics metrics = {0};
    RankMetrics *all_metrics = NULL;
    MpiConfig options;
    double write_s = -1.0;
    double config_broadcast_s;
    double stage;
    double work_s;

    if (MPI_Init(&argc, &argv) != MPI_SUCCESS) {
        fputs("Erro: MPI_Init falhou.\n", stderr);
        return EXIT_FAILURE;
    }
    MPI_CHECK(rank, MPI_Comm_rank(MPI_COMM_WORLD, &rank));
    MPI_CHECK(rank, MPI_Comm_size(MPI_COMM_WORLD, &processes));

    if (rank == 0) {
        parse_status = parse_arguments(argc, argv, &options);
    }
    /* Todos os ranks precisam encerrar juntos se os argumentos forem invalidos. */
    MPI_CHECK(rank, MPI_Bcast(&parse_status, 1, MPI_INT, 0, MPI_COMM_WORLD));
    if (parse_status <= 0) {
        MPI_Finalize();
        return parse_status == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    {
        double real_bounds[4];
        unsigned long long dimensions[2];
        int integer_options[3];

        if (rank == 0) {
            real_bounds[0] = options.mandelbrot.re_min;
            real_bounds[1] = options.mandelbrot.re_max;
            real_bounds[2] = options.mandelbrot.im_min;
            real_bounds[3] = options.mandelbrot.im_max;
            dimensions[0] =
                (unsigned long long) options.mandelbrot.largura;
            dimensions[1] =
                (unsigned long long) options.mandelbrot.altura;
            integer_options[0] = options.mandelbrot.max_iter;
            integer_options[1] = options.strategy;
            integer_options[2] = options.no_write;
        }

        /* Alinha os processos antes de medir o broadcast da configuracao. */
        MPI_CHECK(rank, MPI_Barrier(MPI_COMM_WORLD));
        stage = MPI_Wtime();
        /* Todos os ranks precisam usar os mesmos limites e dimensoes. */
        MPI_CHECK(rank, MPI_Bcast(real_bounds, 4, MPI_DOUBLE, 0, MPI_COMM_WORLD));
        MPI_CHECK(rank, MPI_Bcast(dimensions, 2, MPI_UNSIGNED_LONG_LONG,
                            0, MPI_COMM_WORLD));
        MPI_CHECK(rank, MPI_Bcast(integer_options, 3, MPI_INT, 0, MPI_COMM_WORLD));
        config_broadcast_s = MPI_Wtime() - stage;
        metrics.distribution_s = config_broadcast_s;
        metrics.communication_s = config_broadcast_s;

        if (rank != 0) {
            options.mandelbrot.re_min = real_bounds[0];
            options.mandelbrot.re_max = real_bounds[1];
            options.mandelbrot.im_min = real_bounds[2];
            options.mandelbrot.im_max = real_bounds[3];
            options.mandelbrot.largura = (size_t) dimensions[0];
            options.mandelbrot.altura = (size_t) dimensions[1];
            options.mandelbrot.max_iter = (int32_t) integer_options[0];
            options.strategy = (MpiStrategy) integer_options[1];
            options.no_write = integer_options[2];
        }
    }

    total_elements = options.mandelbrot.largura *
                     options.mandelbrot.altura;

    {
        size_t first_row = 0;
        mpi_compute_partition(options.strategy, options.mandelbrot.altura,
                              rank, processes, &first_row, &local_rows);
        local_count = local_rows * options.mandelbrot.largura;
    }

    counts = rank == 0
        ? (int *) malloc((size_t) processes * sizeof(*counts)) : NULL;
    displacements = rank == 0
        ? (int *) malloc((size_t) processes * sizeof(*displacements)) : NULL;
    if (rank == 0 && counts != NULL && displacements != NULL) {
        size_t packed_offset = 0;
        int r;
        for (r = 0; r < processes; ++r) {
            size_t first = 0;
            size_t rows = 0;
            mpi_compute_partition(options.strategy, options.mandelbrot.altura,
                                  r, processes, &first, &rows);
            if (options.strategy == MPI_STRATEGY_CONTIGUOUS) {
                displacements[r] = (int) (first * options.mandelbrot.largura);
            } else {
                displacements[r] = (int) packed_offset;
                packed_offset += rows * options.mandelbrot.largura;
            }
            counts[r] = (int) (rows * options.mandelbrot.largura);
        }
    }

    if (rank == 0) {
        root_matrix = (int32_t *) malloc(total_elements * sizeof(int32_t));
        if (options.strategy == MPI_STRATEGY_CYCLIC) {
            gathered_matrix =
                (int32_t *) malloc(total_elements * sizeof(int32_t));
        }
    }
    if (local_count > 0) {
        local_matrix =
            (int32_t *) malloc(local_count * sizeof(int32_t));
    } else {
        local_matrix = (int32_t *) malloc(sizeof(int32_t));
    }

    if (rank == 0) {
        all_metrics =
            (RankMetrics *) calloc((size_t) processes, sizeof(*all_metrics));
    }

    if ((rank == 0 && (root_matrix == NULL || all_metrics == NULL ||
                       (options.strategy == MPI_STRATEGY_CYCLIC &&
                        gathered_matrix == NULL))) ||
        (local_count > 0 && local_matrix == NULL) ||
        (rank == 0 && (counts == NULL || displacements == NULL))) {
        validation_status = 0;
    }
    {
        int all_allocated = 0;
        /* Evita que um rank falhe localmente enquanto os outros entram na coletiva. */
        MPI_CHECK(rank, MPI_Allreduce(&validation_status, &all_allocated, 1,
                                MPI_INT, MPI_MIN, MPI_COMM_WORLD));
        if (!all_allocated) {
            if (rank == 0) {
                fputs("Erro: falha de alocacao em um ou mais processos.\n",
                      stderr);
            }
            free(all_metrics);
            free(root_matrix);
            free(gathered_matrix);
            free(local_matrix);
            free(counts);
            free(displacements);
            MPI_Finalize();
            return EXIT_FAILURE;
        }
    }

    MPI_CHECK(rank, MPI_Barrier(MPI_COMM_WORLD));
    stage = MPI_Wtime();
    if (options.strategy == MPI_STRATEGY_CONTIGUOUS) {
        mpi_run_contiguous(&options, rank, processes, local_matrix,
                           root_matrix, counts, displacements, &metrics);
    } else {
        mpi_run_cyclic(&options, rank, processes, local_matrix,
                       gathered_matrix, root_matrix, counts,
                       displacements, &metrics);
    }
    work_s = MPI_Wtime() - stage;
    metrics.total_s = config_broadcast_s + work_s;

    {
        double local_times[5] = {
            metrics.compute_s,
            metrics.distribution_s,
            metrics.communication_s,
            metrics.gather_s,
            metrics.total_s
        };
        unsigned long long local_counts[2] = {
            metrics.rows,
            metrics.tasks
        };
        double *all_times = rank == 0
            ? (double *) malloc((size_t) processes * 5 * sizeof(double))
            : NULL;
        unsigned long long *all_counts = rank == 0
            ? (unsigned long long *) malloc((size_t) processes * 2 *
                                            sizeof(unsigned long long))
            : NULL;

        validation_status =
            (rank != 0 || (all_times != NULL && all_counts != NULL));
        {
            int all_allocated = 0;
            MPI_CHECK(rank, MPI_Allreduce(&validation_status, &all_allocated, 1,
                                    MPI_INT, MPI_MIN, MPI_COMM_WORLD));
            if (!all_allocated) {
                if (rank == 0) {
                    fputs("Erro: falha ao coletar metricas MPI.\n", stderr);
                }
                free(all_times);
                free(all_counts);
                free(all_metrics);
                free(root_matrix);
                free(gathered_matrix);
                free(local_matrix);
                free(counts);
                free(displacements);
                MPI_Finalize();
                return EXIT_FAILURE;
            }
        }

        /* Rank 0 recebe tempos e contagens para imprimir RUN/RANK e calcular FBC. */
        MPI_CHECK(rank, MPI_Gather(local_times, 5, MPI_DOUBLE,
                             all_times, 5, MPI_DOUBLE, 0, MPI_COMM_WORLD));
        MPI_CHECK(rank, MPI_Gather(local_counts, 2, MPI_UNSIGNED_LONG_LONG,
                             all_counts, 2, MPI_UNSIGNED_LONG_LONG,
                             0, MPI_COMM_WORLD));

        if (rank == 0) {
            int r;
            for (r = 0; r < processes; ++r) {
                all_metrics[r].compute_s = all_times[(size_t) r * 5];
                all_metrics[r].distribution_s = all_times[(size_t) r * 5 + 1];
                all_metrics[r].communication_s = all_times[(size_t) r * 5 + 2];
                all_metrics[r].gather_s = all_times[(size_t) r * 5 + 3];
                all_metrics[r].total_s = all_times[(size_t) r * 5 + 4];
                all_metrics[r].rows = all_counts[(size_t) r * 2];
                all_metrics[r].tasks = all_counts[(size_t) r * 2 + 1];
            }
        }
        free(all_times);
        free(all_counts);
    }

    if (rank == 0 && !options.no_write) {
        write_ok = save_new_output(&options, root_matrix, processes,
                                   rank, &write_s);
    }
    MPI_CHECK(rank, MPI_Bcast(&write_ok, 1, MPI_INT, 0, MPI_COMM_WORLD));

    print_metrics(&options, rank, processes, all_metrics, write_s, write_ok);

    if (rank == 0) {
        printf("Matriz %zux%zu gerada com MAX_ITER=%" PRId32
               ", estrategia=%s, processos=%d.\n",
               options.mandelbrot.largura,
               options.mandelbrot.altura,
               options.mandelbrot.max_iter,
               strategy_name(options.strategy),
               processes);
        printf("Tempo de escrita: %.9f s.\n", write_s);
    }

    free(all_metrics);
    free(root_matrix);
    free(gathered_matrix);
    free(local_matrix);
    free(counts);
    free(displacements);
    MPI_Finalize();
    return write_ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
