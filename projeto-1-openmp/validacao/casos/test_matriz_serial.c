#include "mandelbrot.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int testar_matriz_3x3(void){
    const MandelbrotConfig config = {
        .re_min = -2.0,
        .re_max = 2.0,
        .im_min = -2.0,
        .im_max = 2.0,
        .largura = 3,
        .altura = 3,
        .max_iter = 5,

    };
    const int32_t esperado[] = {
        0, 1, 0,
        5, 5, 1,
        0, 1, 0,
    };

    int32_t obtido[9];

    mandelbrot_gerar_serial(&config, obtido);

    for (size_t indice = 0; indice < 9; ++indice){
        if (obtido[indice] != esperado[indice]){
            fprintf(stderr,
                "Falhou: matriz[%zu]; esperando =%" PRId32
                ", obtido = %" PRId32 "\n",
                indice, 
                esperado[indice], 
                obtido[indice]);  
            return EXIT_FAILURE;
        }
    }
    return EXIT_SUCCESS;
}

static int testar_matriz_1x1(void){
    const MandelbrotConfig config = {
        .re_min = 3.0,
        .re_max = 4.0,
        .im_min = 0.0,
        .im_max = 1.0,
        .largura = 1,
        .altura = 1,
        .max_iter = 10,
    };
    int32_t obtido[1];

    mandelbrot_gerar_serial(&config, obtido);

    if (obtido[0] != 0){
        fprintf(stderr,
            "Falhou: matriz 1x1; esperando = 0, obtido = %" PRId32 "\n",
            obtido[0]);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

int main(void){
    if(testar_matriz_3x3() != EXIT_SUCCESS || testar_matriz_1x1() != EXIT_SUCCESS){
        return EXIT_FAILURE;
    }
    puts("Ok: geracao da matriz serial passou em 2 casos");
    return EXIT_SUCCESS;
}