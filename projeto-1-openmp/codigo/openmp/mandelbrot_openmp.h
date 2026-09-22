#ifndef MANDELBROT_OPENMP_H
#define MANDELBROT_OPENMP_H

#include "mandelbrot.h"
#include <stdint.h>

void mandelbrot_gerar_openmp_static(const MandelbrotConfig *config, int32_t *matriz, int numero_threads);

typedef enum {
    MANDELBROT_ESCALONAMENTO_STATIC,
    MANDELBROT_ESCALONAMENTO_DYNAMIC,
    MANDELBROT_ESCALONAMENTO_GUIDED
} MandelbrotEscalonamento;

void mandelbrot_gerar_openmp(
    const MandelbrotConfig *config,
    int32_t *matriz,
    int numero_threads,
    MandelbrotEscalonamento escalonamento,
    int tamanho_chunk
);

#endif // MANDELBROT_OPENMP_H