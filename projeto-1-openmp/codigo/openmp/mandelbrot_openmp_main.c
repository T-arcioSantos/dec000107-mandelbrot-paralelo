#include "mandelbrot_io.h"
#include "mandelbrot_openmp.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <omp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void exibir_uso(const char *programa, FILE *saida)
{

    fprintf(
        saida,
        "Uso: %s [opcoes]\n"
        "\n"
        "Sem opcoes, executa o caso oficial 4096x4096, MAX_ITER=1000.\n"
        "\n"
        "Opcoes:\n"
         "  --threads N       quantidade de threads\n"
        "  --schedule P      static, dynamic ou guided\n"
        "  --chunk N         quantidade de linhas por bloco\n"
        "  --largura N       numero de colunas\n"
        "  --altura N        numero de linhas\n"
        "  --max-iter N      limite de iteracoes\n"
        "  --re-min X        limite real inferior\n"
        "  --re-max X        limite real superior\n"
        "  --im-min X        limite imaginario inferior\n"
        "  --im-max X        limite imaginario superior\n"
        "  --ajuda           mostra esta mensagem\n",
        programa
    );
}

static int ler_inteiro_positivo(const char *texto, int *valor)
{
    char *fim = NULL;
    errno = 0;

    const long lido = strtol(texto, &fim, 10);
    if (errno != 0 || fim == texto || *fim != '\0'
        || lido <= 0 || lido > INT_MAX) {
        return 0;
    }

    *valor = (int) lido;
    return 1;
}

static int ler_size_t_positivo(const char *texto, size_t *valor)
{
   char *fim = NULL;
   errno = 0;


   if (texto[0] == '-') {
       return 0;
   }


   const uintmax_t lido = strtoumax(texto, &fim, 10);
   if (errno != 0 || fim == texto || *fim != '\0'
       || lido == 0 || lido > SIZE_MAX) {
       return 0;
   }


   *valor = (size_t) lido;
   return 1;
}


static int ler_int32_positivo(const char *texto, int32_t *valor)
{
   char *fim = NULL;
   errno = 0;
   const intmax_t lido = strtoimax(texto, &fim, 10);


   if (errno != 0 || fim == texto || *fim != '\0'
       || lido <= 0 || lido > INT32_MAX) {
       return 0;
   }


   *valor = (int32_t) lido;
   return 1;
}


static int ler_double_finito(const char *texto, double *valor)
{
   char *fim = NULL;
   errno = 0;
   const double lido = strtod(texto, &fim);


   if (errno != 0 || fim == texto || *fim != '\0' || !isfinite(lido)) {
       return 0;
   }


   *valor = lido;
   return 1;
}


static int ler_escalonamento(
    const char *texto,
    MandelbrotEscalonamento *escalonamento
)
{
    if (strcmp(texto, "static") == 0) {
        *escalonamento = MANDELBROT_ESCALONAMENTO_STATIC;
    } else if (strcmp(texto, "dynamic") == 0) {
        *escalonamento = MANDELBROT_ESCALONAMENTO_DYNAMIC;
    } else if (strcmp(texto, "guided") == 0) {
        *escalonamento = MANDELBROT_ESCALONAMENTO_GUIDED;
    } else {
        return 0;
    }

    return 1;
}

static const char *nome_escalonamento(
    MandelbrotEscalonamento escalonamento
)
{
    switch (escalonamento) {
        case MANDELBROT_ESCALONAMENTO_DYNAMIC:
            return "dynamic";
        case MANDELBROT_ESCALONAMENTO_GUIDED:
            return "guided";
        case MANDELBROT_ESCALONAMENTO_STATIC:
        default:
            return "static";
    }
}

static const char *caminho_binario(
    MandelbrotEscalonamento escalonamento
)
{
    switch (escalonamento) {
        case MANDELBROT_ESCALONAMENTO_DYNAMIC:
            return "saidas/matrizes/mandelbrot_openmp_dynamic.bin";
        case MANDELBROT_ESCALONAMENTO_GUIDED:
            return "saidas/matrizes/mandelbrot_openmp_guided.bin";
        case MANDELBROT_ESCALONAMENTO_STATIC:
        default:
            return "saidas/matrizes/mandelbrot_openmp_static.bin";
    }
}

static const char *caminho_pgm(
    MandelbrotEscalonamento escalonamento
)
{
    switch (escalonamento) {
        case MANDELBROT_ESCALONAMENTO_DYNAMIC:
            return "saidas/imagens/mandelbrot_openmp_dynamic.pgm";
        case MANDELBROT_ESCALONAMENTO_GUIDED:
            return "saidas/imagens/mandelbrot_openmp_guided.pgm";
        case MANDELBROT_ESCALONAMENTO_STATIC:
        default:
            return "saidas/imagens/mandelbrot_openmp_static.pgm";
    }
}

static int analisar_argumentos(
    int argc,
    char **argv,
    MandelbrotConfig *config,
    int *numero_threads,
    MandelbrotEscalonamento *escalonamento,
    int *tamanho_chunk
)
{
    for (int indice = 1; indice < argc; ++indice) {
        if (strcmp(argv[indice], "--ajuda") == 0) {
            exibir_uso(argv[0], stdout);
            return 0;
        }

        const char *opcao = argv[indice];
        const int conhecida = strcmp(opcao, "--threads") == 0
            || strcmp(opcao, "--schedule") == 0
            || strcmp(opcao, "--chunk") == 0
            || strcmp(opcao, "--largura") == 0
            || strcmp(opcao, "--altura") == 0
            || strcmp(opcao, "--max-iter") == 0
            || strcmp(opcao, "--re-min") == 0
            || strcmp(opcao, "--re-max") == 0
            || strcmp(opcao, "--im-min") == 0
            || strcmp(opcao, "--im-max") == 0;

        if (!conhecida) {
            fprintf(stderr, "Erro: opcao desconhecida: %s.\n", argv[indice]);
            return -1;
        }

        if (indice + 1 >= argc) {
            fprintf(stderr, "Erro: faltou um valor depois de %s.\n", opcao);
            return -1;
        }

        ++indice;
        int valido = 0;
        if (strcmp(opcao, "--threads") == 0) {
            valido = ler_inteiro_positivo(argv[indice], numero_threads);
        } else if (strcmp(opcao, "--schedule") == 0) {
            valido = ler_escalonamento(argv[indice], escalonamento);
        } else if (strcmp(opcao, "--chunk") == 0) {
            valido = ler_inteiro_positivo(argv[indice], tamanho_chunk);
        } else if (strcmp(opcao, "--largura") == 0) {
            valido = ler_size_t_positivo(argv[indice], &config->largura);
        } else if (strcmp(opcao, "--altura") == 0) {
            valido = ler_size_t_positivo(argv[indice], &config->altura);
        } else if (strcmp(opcao, "--max-iter") == 0) {
            valido = ler_int32_positivo(argv[indice], &config->max_iter);
        } else if (strcmp(opcao, "--re-min") == 0) {
            valido = ler_double_finito(argv[indice], &config->re_min);
        } else if (strcmp(opcao, "--re-max") == 0) {
            valido = ler_double_finito(argv[indice], &config->re_max);
        } else if (strcmp(opcao, "--im-min") == 0) {
            valido = ler_double_finito(argv[indice], &config->im_min);
        } else {
            valido = ler_double_finito(argv[indice], &config->im_max);
        }

        if (!valido) {
            fprintf(
                stderr,
                "Erro: valor invalido para %s: %s.\n",
                opcao,
                argv[indice]
            );
            return -1;
        }
    }

    if (config->re_min > config->re_max
        || config->im_min > config->im_max) {
        fputs("Erro: o limite inferior nao pode superar o superior.\n", stderr);
        return -1;
    }


    return 1;
}

int main(int argc, char **argv)
{
    MandelbrotConfig config = {
        .re_min = -2.0,
        .re_max = 1.0,
        .im_min = -1.5,
        .im_max = 1.5,
        .largura = 4096,
        .altura = 4096,
        .max_iter = 1000,
    };
    int numero_threads = omp_get_max_threads();
    MandelbrotEscalonamento escalonamento =
        MANDELBROT_ESCALONAMENTO_STATIC;
    int tamanho_chunk = 0;
    const int resultado_argumentos = analisar_argumentos(
        argc,
        argv,
        &config,
        &numero_threads,
        &escalonamento,
        &tamanho_chunk
    );
    if (resultado_argumentos == 0) {
        return EXIT_SUCCESS;
    }
    if (resultado_argumentos < 0) {
        exibir_uso(argv[0], stderr);
        return EXIT_FAILURE;
    }

    if (config.largura > SIZE_MAX / config.altura) {
        fputs("Erro: as dimensoes da matriz sao grandes demais.\n", stderr);
        return EXIT_FAILURE;
    }


    const char *arquivo_binario = caminho_binario(escalonamento);
    const char *arquivo_pgm = caminho_pgm(escalonamento);

    const size_t total = config.largura * config.altura;
    if (total > SIZE_MAX / sizeof(int32_t)) {
       fputs("Erro: a matriz nao cabe na memoria enderecavel.\n", stderr);
       return EXIT_FAILURE;
   }

    int32_t *matriz = malloc(total * sizeof(*matriz));
    if (matriz == NULL) {
        fputs("Erro: nao foi possivel alocar a matriz.\n", stderr);
        return EXIT_FAILURE;
    }

    omp_set_dynamic(0);

    const double inicio_geracao = omp_get_wtime();
    mandelbrot_gerar_openmp(
        &config,
        matriz,
        numero_threads,
        escalonamento,
        tamanho_chunk
    );
    const double tempo_geracao = omp_get_wtime() - inicio_geracao;

    const double inicio_escrita = omp_get_wtime();
    if (!mandelbrot_salvar_binario(arquivo_binario, &config, matriz)) {
        free(matriz);
        return EXIT_FAILURE;
    }
    if (!mandelbrot_salvar_pgm(arquivo_pgm, &config, matriz)) {
        free(matriz);
        return EXIT_FAILURE;
    }
    const double tempo_escrita = omp_get_wtime() - inicio_escrita;

    printf(
        "Matriz OpenMP %zux%zu gerada com MAX_ITER=%" PRId32 ".\n"
        "Regiao: Re=[%.9f, %.9f], Im=[%.9f, %.9f].\n"
        "Escalonamento: %s.\n"
        "Threads: %d.\n"
        "Tempo de geracao: %.6f segundos.\n"
        "Tempo de escrita: %.6f segundos.\n"
        "Amostras: primeiro=%" PRId32 ", meio=%" PRId32
        ", ultimo=%" PRId32 ".\n",
        config.largura,
        config.altura,
        config.max_iter,
        config.re_min,
        config.re_max,
        config.im_min,
        config.im_max,
        nome_escalonamento(escalonamento),
        numero_threads,
        tempo_geracao,
        tempo_escrita,
        matriz[0],
        matriz[total / 2],
        matriz[total - 1]
    );
    if (tamanho_chunk == 0) {
        puts("Chunk: padrao da politica.");
    } else {
        printf(
            "Chunk: %d %s.\n",
            tamanho_chunk,
            tamanho_chunk == 1 ? "linha" : "linhas"
        );
    }
    printf("Matriz binaria salva em %s.\n", arquivo_binario);
    printf("Imagem PGM salva em %s.\n", arquivo_pgm);

    free(matriz);
    return EXIT_SUCCESS;
}