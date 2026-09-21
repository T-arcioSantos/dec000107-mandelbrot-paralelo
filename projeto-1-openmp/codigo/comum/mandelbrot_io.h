#ifndef MANDELBROT_IO_H
#define MANDELBROT_IO_H

#include <mandelbrot.h>

#include <stdint.h>

int mandelbrot_salvar_binario(const char *caminho, const MandelbrotConfig *config, const int32_t *matriz);

#endif // MANDELBROT_IO_H