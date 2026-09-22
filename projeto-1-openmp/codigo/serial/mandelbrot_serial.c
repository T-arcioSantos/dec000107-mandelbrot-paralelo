#include "mandelbrot.h"
#include "mandelbrot_io.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>
#include <string.h>
#include <omp.h>

static void exibir_uso(const char *programa, FILE *saida){
    fprintf(saida, 
        "Uso: %s [opcoes]\n"
        "\n"
        "Sem opcoes, executa o caso oficial 4096x4096, MAX_ITER=1000.\n"
        "\n"
        "Opcoes:\n"
        "  --largura N       numero de colunas\n"
        "  --altura N        numero de linhas\n"
        "  --max-iter N      limite de iteracoes\n"
        "  --re-min X        limite real inferior\n"
        "  --re-max X        limite real superior\n"
        "  --im-min X        limite imaginario inferior\n"
        "  --im-max X        limite imaginario superior\n"
        "  --ajuda            mostra esta mensagem\n",
        programa
    );
}

static int ler_size_t_positivo(const char *texto, size_t *valor){
    char *fim = NULL;
    errno = 0;

    if (texto[0]== '-'){
        return 0;
    }

    const uintmax_t lido = strtoumax(texto, &fim, 10);
    if(errno != 0 || fim == texto || *fim != '\0' || lido == 0 || lido > SIZE_MAX){
        return 0;
    }
    *valor = (size_t) lido;
    return 1;
}

static int ler_int32_positivo(const char* texto, int32_t *valor){
    char *fim = NULL;
    errno = 0;
    const intmax_t lido = strtoimax(texto, &fim, 10);

    if(errno != 0 || fim == texto || *fim != '\0' || lido <= 0 || lido > INT32_MAX){
        return 0;
    }
    *valor = (int32_t) lido;
    return 1;
}

static int ler_double_finito(const char *texto, double *valor){
    char *fim = NULL;
    errno = 0;
    const double lido = strtod(texto, &fim);

    if(errno != 0 || fim == texto || *fim != '\0' || !isfinite(lido)){
        return 0;
    }
    *valor = lido;
    return 1;
}

static int obter_valor(int argc, char **argv, int *indice, const char **valor){
    if(*indice + 1 >= argc){
        fprintf(stderr, "Erro: faltou um valor depois de %s.\n", argv[*indice]);
        return 0;
    }
    ++(*indice);
    *valor = argv[*indice];
    return 1;
}

//retorna 1 para executar, 0 para ajuda e -1 quando encontra erro.
 static int analisar_argumentos( int argc, char **argv, MandelbrotConfig *config){
    for (int indice = 1; indice < argc; ++indice){
        const char *opcao = argv[indice];

        if(strcmp(opcao, "--ajuda") == 0){
            exibir_uso(argv[0], stdout);
            return 0;
        }
        const int conhecida = strcmp(opcao, "--largura") == 0
            || strcmp(opcao, "--altura") == 0
            || strcmp(opcao, "--max-iter") == 0
            || strcmp(opcao, "--re-min") == 0
            || strcmp(opcao, "--re-max") == 0
            || strcmp(opcao, "--im-min") == 0
            || strcmp(opcao, "--im-max") == 0;

        if (!conhecida){
            fprintf(stderr, "Erro: opcao desconhecida: %s.\n", opcao);
            return -1;
        }

        const char *valor = NULL;
        if(!obter_valor(argc, argv, &indice, &valor)){
            return -1;
        }

        int valido = 1;
        if (strcmp(opcao, "--largura") == 0) {
            valido = ler_size_t_positivo(valor, &config->largura);
        } else if (strcmp(opcao, "--altura") == 0) {
            valido = ler_size_t_positivo(valor, &config->altura);
        } else if (strcmp(opcao, "--max-iter") == 0) {
            valido = ler_int32_positivo(valor, &config->max_iter);
        } else if (strcmp(opcao, "--re-min") == 0) {
            valido = ler_double_finito(valor, &config->re_min);
        } else if (strcmp(opcao, "--re-max") == 0) {
            valido = ler_double_finito(valor, &config->re_max);
        } else if (strcmp(opcao, "--im-min") == 0) {
            valido = ler_double_finito(valor, &config->im_min);
        } else {
            valido = ler_double_finito(valor, &config->im_max);
        }

        if (!valido) {
            fprintf(stderr, "Erro: valor invalido para %s: %s.\n", opcao, valor);
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


int main(int argc, char **argv){
    MandelbrotConfig config = {
        .re_min = -2.0,
        .re_max = 1.0,
        .im_min = -1.5,
        .im_max = 1.5,
        .largura = 4096,
        .altura = 4096,
        .max_iter = 1000,
    };

    const char *arquivo_binario = "saidas/matrizes/mandelbrot_serial.bin";

    const char *arquivo_pgm = "saidas/imagens/mandelbrot_serial.pgm";

    const int resultado_argumentos = analisar_argumentos(argc, argv, &config);
    if(resultado_argumentos == 0){
        return EXIT_SUCCESS;
    }   
    if(resultado_argumentos < 0){
        exibir_uso(argv[0], stderr);
        return EXIT_FAILURE;
    }

    if (config.largura > SIZE_MAX / config.altura) {
        fputs("Erro: as dimensoes da matriz sao grandes demais.\n", stderr);
        return EXIT_FAILURE;
    }

    const size_t total = config.largura * config.altura;
    if(total > SIZE_MAX / sizeof(int32_t)){
        fputs("Erro: a matriz nao cabe na memoria enderecavel.\n", stderr);
        return EXIT_FAILURE;
    }


    int32_t *matriz = malloc(total * sizeof(*matriz));
    if(matriz == NULL){
        fputs("Erro: nao foi possivel alocar a matriz.\n", stderr);
        return EXIT_FAILURE;
    }

    const double inicio_geracao = omp_get_wtime();
    mandelbrot_gerar_serial(&config, matriz);
    const double tempo_geracao = omp_get_wtime() - inicio_geracao;

    const double inicio_escrita = omp_get_wtime();
    if(!mandelbrot_salvar_binario(arquivo_binario, &config, matriz)){
        free(matriz);
        return EXIT_FAILURE;
    }

    if(!mandelbrot_salvar_pgm(arquivo_pgm,&config, matriz)){
        free(matriz);
        return EXIT_FAILURE;
    }

    const double tempo_escrita = omp_get_wtime() - inicio_escrita;

    printf( 
        "Matriz %zux%zu gerada com MAX_ITER=%" PRId32 ".\n"
        "Regiao: Re=[%.1f, %.1f], Im=[%.1f, %.1f].\n"
        "Amostras: primeiro=%" PRId32 ", meio=%" PRId32
        ", ultimo=%" PRId32 ".\n",
        config.largura,
        config.altura,
        config.max_iter,
        config.re_min,
        config.re_max,
        config.im_min,
        config.im_max,
        matriz[0],
        matriz[total / 2],
        matriz[total - 1]
    );

    printf("Tempo de geracao: %.6f segundos. \n", tempo_geracao);
    printf("Tempo de escrita: %.6f segundos. \n", tempo_escrita);

    printf("Matriz binaria salva em %s.\n", arquivo_binario);

    printf("Imagem PGM salva em %s.\n", arquivo_pgm);

    free(matriz);
    return EXIT_SUCCESS;
}

