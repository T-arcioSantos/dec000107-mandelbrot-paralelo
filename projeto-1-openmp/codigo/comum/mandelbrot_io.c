#include "mandelbrot_io.h"

#include <stdint.h>
#include <stdio.h>

#include <math.h>
#include <stdlib.h>

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

static uint8_t tom_de_cinza(int32_t iteracoes, int32_t max_iter){
    if(iteracoes >= max_iter){
        return 0;
    }
    const double normalizado = log1p((double) iteracoes) / log1p((double) max_iter);
    return (uint8_t) lround(255.0 * (1.0 - normalizado));
}

int mandelbrot_salvar_pgm(const char *caminho, const MandelbrotConfig *config, const int32_t *matriz){
    FILE *arquivo = fopen(caminho, "wb");
    if(arquivo == NULL){
        fprintf(stderr, "Erro: nao foi possivel abrir %s. \n", caminho);
        return 0;
    }

if(fprintf(
        arquivo,
        "P5\n%zu %zu\n255\n",
        config->largura,
        config->altura
    ) < 0){
        fprintf(stderr, "Erro: falha ao escrever o cabeçalho de %s.\n", caminho);
        fclose(arquivo);
        return 0;
    }

    uint8_t *linha = malloc(config->largura * sizeof(*linha));
    if(linha == NULL){
        fprintf(stderr, "Erro: nao foi possivel criar uma linha do PGM.\n");
        fclose(arquivo);
        return 0;
    }

    int sucesso = 1;
    for(size_t linha_invertida = config->altura;
        linha_invertida > 0 && sucesso;
        --linha_invertida){
            const size_t py = linha_invertida - 1;

            for(size_t px = 0; px < config->largura; ++px){
                const size_t indice = py * config->largura + px;
                linha[px] = tom_de_cinza(matriz[indice], config->max_iter);
            }

            if(fwrite(linha, sizeof(*linha), config->largura, arquivo) != config->largura){
                sucesso = 0;
            }
        }

        free(linha);

        if(fclose(arquivo) != 0){
            sucesso = 0;
        }

        if(!sucesso){
            fprintf(stderr, "Erro: falha ao gravar a imagem em %s. \n", caminho);
        }

        return sucesso;
}