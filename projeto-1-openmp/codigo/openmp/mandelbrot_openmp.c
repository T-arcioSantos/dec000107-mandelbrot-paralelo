#include "mandelbrot_openmp.h"
#include <omp.h>

void mandelbrot_gerar_openmp_static(
    const MandelbrotConfig *config,
    int32_t *matriz,
    int numero_threads
)
{
    mandelbrot_gerar_openmp(
        config,
        matriz,
        numero_threads,
        MANDELBROT_ESCALONAMENTO_STATIC,
        0
    );
}

static omp_sched_t converter_escalonamento(
    MandelbrotEscalonamento escalonamento
)
{
    switch (escalonamento) {
        case MANDELBROT_ESCALONAMENTO_DYNAMIC:
            return omp_sched_dynamic;
        case MANDELBROT_ESCALONAMENTO_GUIDED:
            return omp_sched_guided;
        case MANDELBROT_ESCALONAMENTO_STATIC:
        default:
            return omp_sched_static;
    }
}

void mandelbrot_gerar_openmp(
    const MandelbrotConfig *config,
    int32_t *matriz,
    int numero_threads,
    MandelbrotEscalonamento escalonamento,
    int tamanho_chunk
)
{
    omp_set_schedule(converter_escalonamento(escalonamento), tamanho_chunk);

    #pragma omp parallel for \
        default(none) \
        shared(config, matriz) \
        num_threads(numero_threads) \
        schedule(runtime)
    for (size_t py = 0; py < config->altura; ++py) {
        mandelbrot_gerar_linha(config, py, matriz);
    }

}