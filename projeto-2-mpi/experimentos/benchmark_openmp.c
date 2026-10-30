#include "mandelbrot.h"
#include "mandelbrot_openmp.h"
#include <omp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc != 11) {
        fprintf(stderr, "Uso: %s threads width height max_iter re_min re_max im_min im_max static|dynamic|guided chunk\n", argv[0]);
        return 2;
    }
    MandelbrotConfig c;
    int threads = atoi(argv[1]);
    c.largura = (size_t)strtoull(argv[2], NULL, 10);
    c.altura = (size_t)strtoull(argv[3], NULL, 10);
    c.max_iter = (int32_t)strtol(argv[4], NULL, 10);
    c.re_min = strtod(argv[5], NULL); c.re_max = strtod(argv[6], NULL);
    c.im_min = strtod(argv[7], NULL); c.im_max = strtod(argv[8], NULL);
    MandelbrotEscalonamento s = MANDELBROT_ESCALONAMENTO_STATIC;
    if (argv[9][0] == 'd') s = MANDELBROT_ESCALONAMENTO_DYNAMIC;
    else if (argv[9][0] == 'g') s = MANDELBROT_ESCALONAMENTO_GUIDED;
    int chunk = atoi(argv[10]);
    if (threads < 1 || c.largura < 1 || c.altura < 1 || c.max_iter < 1 || chunk < 1 ||
        c.largura > SIZE_MAX / c.altura || c.largura * c.altura > SIZE_MAX / sizeof(int32_t)) return 2;
    int32_t *matrix = malloc(c.largura * c.altura * sizeof(*matrix));
    if (matrix == NULL) return 3;
    omp_set_dynamic(0);
    MandelbrotBalanceamento b;
    const double start = omp_get_wtime();
    const int ok = mandelbrot_gerar_openmp_com_balanceamento(&c, matrix, threads, s, chunk, &b);
    const double total = omp_get_wtime() - start;
    if (!ok) { free(matrix); return 4; }
    const double fbc = b.tempo_maximo > 0.0 ? (b.tempo_maximo - b.tempo_minimo) / b.tempo_maximo : 0.0;
    printf("OMP;%d;%zu;%zu;%d;%s;%d;%.9f;%.9f;%.9f;%.9f;%.9f;%.9f\n",
        b.numero_threads,c.largura,c.altura,c.max_iter,argv[9],chunk,total,
        b.tempo_minimo,b.tempo_medio,b.tempo_maximo,b.fator_maximo_medio,fbc);
    free(matrix);
    return 0;
}
