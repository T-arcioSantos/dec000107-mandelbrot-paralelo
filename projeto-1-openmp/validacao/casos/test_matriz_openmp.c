#include "mandelbrot.h"
#include "mandelbrot_openmp.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int comparar_com_serial(
    const MandelbrotConfig *config,
    int numero_threads,
    MandelbrotEscalonamento escalonamento,
    int tamanho_chunk
)
{
    const size_t total = config->largura * config->altura;
    int32_t *serial = malloc(total * sizeof(*serial));
    int32_t *openmp = malloc(total * sizeof(*openmp));

    if (serial == NULL || openmp == NULL) {
        fputs("ERRO: falha ao alocar matrizes do teste.\n", stderr);
        free(serial);
        free(openmp);
        return 0;
    }

    mandelbrot_gerar_serial(config, serial);
    MandelbrotBalanceamento balanceamento;

    if (!mandelbrot_gerar_openmp_com_balanceamento(
           config,
           openmp,
           numero_threads,
           escalonamento,
           tamanho_chunk,
           &balanceamento
       )) {
       fputs("ERRO: falha ao medir balanceamento.\n", stderr);
       free(serial);
       free(openmp);
       return 0;
   }


   if (balanceamento.numero_threads < 1
       || balanceamento.numero_threads > numero_threads
       || balanceamento.tempo_minimo > balanceamento.tempo_medio
       || balanceamento.tempo_medio > balanceamento.tempo_maximo
       || balanceamento.fator_maximo_medio < 0.0) {
       fputs("FALHOU: metricas de balanceamento invalidas.\n", stderr);
       free(serial);
       free(openmp);
       return 0;
   }

    int iguais = 1;
    for (size_t indice = 0; indice < total; ++indice) {
        if (serial[indice] != openmp[indice]) {
            fprintf(
                stderr,
                "FALHOU: politica=%d, threads=%d, chunk=%d, indice=%zu, "
                "serial=%" PRId32
                ", openmp=%" PRId32 ".\n",
                (int) escalonamento,
                numero_threads,
                tamanho_chunk,
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

int main(void)
{
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

    const MandelbrotEscalonamento escalonamentos[] = {
        MANDELBROT_ESCALONAMENTO_STATIC,
        MANDELBROT_ESCALONAMENTO_DYNAMIC,
        MANDELBROT_ESCALONAMENTO_GUIDED,
    };
    const int tamanhos_chunk[] = {0, 1, 4};

    const size_t total_casos = sizeof(casos) / sizeof(casos[0]);
    const size_t total_quantidades = sizeof(quantidades_threads)
        / sizeof(quantidades_threads[0]);
    const size_t total_escalonamentos = sizeof(escalonamentos)
        / sizeof(escalonamentos[0]);
    const size_t total_chunks = sizeof(tamanhos_chunk)
        / sizeof(tamanhos_chunk[0]);

    for (size_t caso = 0; caso < total_casos; ++caso) {
        for (size_t politica = 0;
             politica < total_escalonamentos;
             ++politica) {
            for (size_t chunk = 0; chunk < total_chunks; ++chunk) {
                for (size_t quantidade = 0;
                     quantidade < total_quantidades;
                     ++quantidade) {
                    if (!comparar_com_serial(
                            &casos[caso],
                            quantidades_threads[quantidade],
                            escalonamentos[politica],
                            tamanhos_chunk[chunk]
                        )) {
                        return EXIT_FAILURE;
                    }
                }
            }
        }
    }

    printf(
        "OK: %zu politicas OpenMP coincidem com a serial em %zu casos, "
        "%zu quantidades de threads e %zu chunks.\n",
        total_escalonamentos,
        total_casos,
        total_quantidades,
        total_chunks
    );
    return EXIT_SUCCESS;
}
