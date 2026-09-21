#ifndef MANDELBROT_H
#define MANDELBROT_H

#include <stdint.h>
#include <stddef.h>

typedef struct MandelbrotConfig {
    double re_min;
    double re_max;
    double im_min;
    double im_max;
    size_t largura;
    size_t altura;
    int32_t max_iter;
} MandelbrotConfig;

int32_t mandelbrot_escape_time(double cr, double ci, int32_t max_iter);

void mandelbrot_gerar_serial(const MandelbrotConfig *config, int32_t *matriz);

#endif // MANDELBROT_H