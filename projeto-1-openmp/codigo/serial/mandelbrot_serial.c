#include "mandelbrot.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(void){
    const MandelbrotConfig config = {
        .re_min = -2.0,
        .re_max = 1.0,
        .im_min = -1.5,
        .im_max = 1.5,
        .largura = 4096,
        .altura = 4096,
        .max_iter = 1000,
    };

    const size_t total = config.largura * config.altura;

    int32_t *matriz = malloc(total * sizeof(*matriz));
    if(matriz == NULL){
        fputs("Erro: nao foi possivel alocar a matriz\n", stderr);
        return EXIT_FAILURE;
    }

    mandelbrot_gerar_serial(&config, matriz);

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

    free(matriz);
    return EXIT_SUCCESS;
}

