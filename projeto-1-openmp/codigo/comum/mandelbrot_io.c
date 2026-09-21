#include "mandelbrot_io.h"

#include <stdint.h>
#include <stdio.h>

int mandelbrot_salvar_binario(const char *caminho, const MandelbrotConfig *config, const int32_t *matriz){
    FILE *arquivo = fopen(caminho, "wb");
    if(arquivo == NULL){
        fprintf(stderr, "Erro: nao foi possivel abrir %s \n", caminho);
        return 0;
    } 

    const size_t total = config->largura * config->altura;
    const size_t escritos = fwrite(matriz, sizeof(*matriz), total, arquivo);

    const int fechou = fclose(arquivo) == 0;

    if (escritos != total || !fechou){
        fprintf(stderr, "Erro: falha ao gravar a matriz em %s \n", caminho);
        return 0;
    }

    return 1;
}