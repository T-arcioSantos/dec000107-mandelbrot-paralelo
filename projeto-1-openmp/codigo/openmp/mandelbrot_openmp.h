#ifndef MANDELBROT_OPENMP_H
#define MANDELBROT_OPENMP_H

#include "mandelbrot.h"
#include <stdint.h>

void mandelbrot_gerar_openmp_static(const MandelbrotConfig *config, int32_t *matriz, int numero_threads);

#endif // MANDELBROT_OPENMP_H