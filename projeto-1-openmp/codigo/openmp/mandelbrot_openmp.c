#include "mandelbrot_openmp.h"

void mandelbrot_gerar_openmp_static(const MandelbrotConfig *config, int32_t *matriz, int numero_threads){
    #pragma omp parallel for \
        default(none) \
        shared(config, matriz) \
        num_threads(numero_threads)\
        schedule(static)
    for(size_t py = 0; py < config->altura; ++py){
        mandelbrot_gerar_linha(config, py, matriz);
    }

}