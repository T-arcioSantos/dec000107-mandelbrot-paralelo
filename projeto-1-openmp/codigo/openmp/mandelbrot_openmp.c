#include "mandelbrot_openmp.h"
#include <float.h>
#include <omp.h>
#include <stdlib.h>


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

static int gerar(
    const MandelbrotConfig *config,
    int32_t *matriz,
    int numero_threads,
    MandelbrotEscalonamento escalonamento,
    int tamanho_chunk,
    MandelbrotBalanceamento *balanceamento
)
{
    double *tempos = NULL;
    if (balanceamento != NULL) {
       tempos = calloc((size_t) numero_threads, sizeof(*tempos));
       if (tempos == NULL) {
           return 0;
       }
    }


    omp_set_schedule(converter_escalonamento(escalonamento), tamanho_chunk);

    int threads_usadas = 0;

    #pragma omp parallel \
        default(none) \
        shared(config, matriz, tempos, threads_usadas) \
        num_threads(numero_threads) 
    {
        const int id_thread = omp_get_thread_num();


       #pragma omp single
       threads_usadas = omp_get_num_threads();


       const double inicio = tempos == NULL ? 0.0 : omp_get_wtime();


       #pragma omp for schedule(runtime) nowait
       for (size_t py = 0; py < config->altura; ++py) {
           mandelbrot_gerar_linha(config, py, matriz);
       }


       if (tempos != NULL) {
           tempos[id_thread] = omp_get_wtime() - inicio;
       }
   }


   if (balanceamento != NULL) {
       double minimo = DBL_MAX;
       double maximo = 0.0;
       double soma = 0.0;


       for (int id_thread = 0; id_thread < threads_usadas; ++id_thread) {
           const double tempo = tempos[id_thread];
           if (tempo < minimo) {
               minimo = tempo;
           }
           if (tempo > maximo) {
               maximo = tempo;
           }
           soma += tempo;
       }


       const double medio = threads_usadas > 0
           ? soma / (double) threads_usadas
           : 0.0;


       balanceamento->numero_threads = threads_usadas;
       balanceamento->tempo_minimo = threads_usadas > 0 ? minimo : 0.0;
       balanceamento->tempo_medio = medio;
       balanceamento->tempo_maximo = maximo;
       balanceamento->fator_maximo_medio = medio > 0.0
           ? maximo / medio
           : 0.0;
   }


   free(tempos);
   return 1;
}


void mandelbrot_gerar_openmp(
   const MandelbrotConfig *config,
   int32_t *matriz,
   int numero_threads,
   MandelbrotEscalonamento escalonamento,
   int tamanho_chunk
)
{
   (void) gerar(
       config,
       matriz,
       numero_threads,
       escalonamento,
       tamanho_chunk,
       NULL
   );
}


int mandelbrot_gerar_openmp_com_balanceamento(
   const MandelbrotConfig *config,
   int32_t *matriz,
   int numero_threads,
   MandelbrotEscalonamento escalonamento,
   int tamanho_chunk,
   MandelbrotBalanceamento *balanceamento
)
{
   return gerar(
       config,
       matriz,
       numero_threads,
       escalonamento,
       tamanho_chunk,
       balanceamento
   );
}

