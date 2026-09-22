#include "mandelbrot_io.h"
#include "mandelbrot_openmp.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <omp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void exibir_uso(const char *programa, FILE *saida){
    fprintf(
        saida, 
        "Uso: %s  [--threads N]\n"
        "\n"
        "Executa o caso oficial 4096x4096, MAX_ITER=1000, "
        "com schedule (static)"
        "\n"
        "opcoes: \n"
        "  --threads N      quantidade de threads\n"
        "  --ajuda         mostra esta mensagem\n",
        programa
    );
}

static int ler_threads(const char *texto, int *numero_threads){
    char *fim = NULL;
    errno = 0;

    const long lido = strtol(texto, &fim, 10);
    if(errno != 0 || fim == texto ||*fim != '\0' || lido <= 0 || lido > INT_MAX){
        return 0;
    }
    *numero_threads = (int) lido;
    return 1;
}

static int analisar_argumentos(int argc, char **argv, int *numero_threads){
    for(int indice = 1; indice < argc; ++indice){
        if (strcmp(argv[indice], "--ajuda") == 0){
            exibir_uso(argv[0], stdout);
            return 0;
        }

        if (strcmp(argv[indice], "--threads")!=0){
            fprintf(stderr, "Erro: opcao desconhecida: %s.\n", argv[indice]);
            return -1;
        }

        if(indice + 1>= argc){
            fputs("ERRO: faltou um valor depois de --threads.\n", stderr);
            return -1;
        }
        ++indice;
        if(!ler_threads(argv[indice], numero_threads)){
            fprintf(stderr, "Erro: valor invalido para --threads: %s.\n", argv[indice]);
            return -1;
        }
    } 
    return 1;
}

int main(int argc, char **argv){
    const MandelbrotConfig config = {
        .re_min = -2.0,
        .re_max = 1.0,
        .im_min = -1.5,
        .im_max = 1.5,
        .largura = 4096,
        .altura = 4096,
        .max_iter = 1000,

    };

    const char *arquivo_binario = "saidas/matrizes/mandelbrot_openmp_static.bin";
    const char *arquivo_pgm = "saidas/imagens/mandelbrot_openmp_static.pgm";

    int numero_threads = omp_get_max_threads(); 
    const int resultado_argumentos = analisar_argumentos(argc, argv, &numero_threads);
    if(resultado_argumentos == 0){
        return EXIT_SUCCESS;
    }
    if(resultado_argumentos < 0){
        exibir_uso(argv[0], stderr);
        return EXIT_FAILURE;
    }

    const size_t total = config.largura * config.altura;
    int32_t *matriz = malloc(total * sizeof(*matriz));
    if(matriz == NULL){
        fputs("Erro: nao foi possivel alocar a matriz.\n", stderr);
        return EXIT_FAILURE;
    }

    omp_set_dynamic(0);
    
    const double inicio_geracao = omp_get_wtime();
    mandelbrot_gerar_openmp_static(&config, matriz, numero_threads);
    const double tempo_geracao = omp_get_wtime() - inicio_geracao;

    const double inicio_escrita = omp_get_wtime();
    if(!mandelbrot_salvar_binario(arquivo_binario, &config, matriz)){
        free(matriz);
        return EXIT_FAILURE;
    }
    if(!mandelbrot_salvar_pgm(arquivo_pgm, &config, matriz)){
        free(matriz);
        return EXIT_FAILURE;
    }
    const double tempo_escrita = omp_get_wtime() - inicio_escrita;

    printf(
         "Matriz OpenMP %zux%zu gerada com MAX_ITER=%" PRId32 ".\n"
        "Escalonamento: static.\n"
        "Threads: %d.\n"
        "Tempo de geracao: %.6f segundos.\n"
        "Tempo de escrita: %.6f segundos.\n"
        "Amostras: primeiro=%" PRId32 ", meio=%" PRId32
        ", ultimo=%" PRId32 ".\n",
        config.largura,
        config.altura,
        config.max_iter,
        numero_threads,
        tempo_geracao,
        tempo_escrita,
        matriz[0],
        matriz[total / 2],
        matriz[total - 1]
    );

    printf("Matriz binaria salva em %s.\n", arquivo_binario);
    printf("Imagem PGM salva em %s.\n", arquivo_pgm);
    free(matriz);
    return EXIT_SUCCESS;
}