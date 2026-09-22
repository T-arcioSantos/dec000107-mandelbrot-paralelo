#include "mandelbrot.h"
#include "mandelbrot_openmp.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int comparar_com_serial(const MandelbrotConfig *config, int numero_threads){
    const size_t total = config->largura * config->altura;
    int32_t *serial = malloc(total * sizeof(*serial));
    int32_t *openmp = malloc(total * sizeof(*openmp));

    if(serial == NULL || openmp == NULL){
        fputs("Erro: falaha ao alocar matrizes do teste.\n", stderr);
        free(serial);
        free(openmp);
        return 0;
    }

    mandelbrot_gerar_serial(config, serial);
    mandelbrot_gerar_openmp_static(config, openmp, numero_threads);

    int iguais = 1;
    for (size_t indice = 0; indice < total; ++indice){
        if (serial[indice] != openmp[indice]){
            fprintf(
                stderr, 
                "Falhou: threads=%d; indice = %zu, serial=%" PRId32 
                ", openmp=%" PRId32 ".\n",
                numero_threads,
                indice,
                serial[indice],
                openmp[indice]
            );
            iguais = 0;
            break;

        }
    }
    free(serial);
    free(openmp);
    return iguais;
}

int main(void){
    const MandelbrotConfig casos[] = {
        {
            .re_min = -2.0,
            .re_max = 2.0,
            .im_min = -2.0,
            .im_max = 2.0,
            .largura = 3,
            .altura = 3,
            .max_iter = 5,
        },
        {
            .re_min = -0.81,
            .re_max = -0.71,
            .im_min = 0.05,
            .im_max = 0.15,
            .largura = 17,
            .altura = 11,
            .max_iter = 200,
        },
    };
    const int quantidades_threads[] = {1, 2, 4};

    const size_t total_casos = sizeof(casos) / sizeof(casos[0]);
    const size_t total_quantidades = sizeof(quantidades_threads) / sizeof(quantidades_threads[0]);

    for (size_t caso = 0; caso < total_casos; ++caso){
        for (size_t quantidade = 0;
            quantidade < total_quantidades; ++quantidade){
            if(!comparar_com_serial(&casos[caso], quantidades_threads[quantidade])){
                return EXIT_FAILURE;
             }
        }
    }

    printf(
        "OK: OpenMP static coincide com a serial em %zu casos "
        "e %zu quantidades de threads.\n",
        total_casos,
        total_quantidades
    );
    return EXIT_SUCCESS;

}