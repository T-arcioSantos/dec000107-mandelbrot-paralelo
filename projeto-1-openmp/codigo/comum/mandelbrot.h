#ifndef MANDELBROT_H
#define MANDELBROT_H

#include <stdint.h>

int32_t mandelbrot_escape_time(double cr, double ci, int32_t max_iter);

#endif // MANDELBROT_H