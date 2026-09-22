#include "mandelbrot.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    double cr;
    double ci;
    int32_t max_iter;
    int32_t esperado;
    const char *descricao;
} CasoEscapeTime;

int main(void){
    const CasoEscapeTime casos[] = {
        {0.0, 0.0, 10, 10, "origem não escapa"},
        {-1.0, 0.0, 10, 10, "órbita de -1 permanece limitada"},
        {-2.0, 0.0, 10, 10, "módulo igual a 2 não escapa"},
        {1.0, 0.0, 10, 2, "1 escapa na terceira atualização"},
        {2.0, 0.0, 10, 1, "2 escapa na segunda atualização"},
        {3.0, 0.0, 10, 0, "3 escapa na primeira atualização"},
        {1.0, 0.0, 3, 2, "escape na última atualização permitida"},
        {2.0, 0.0, 1, 1, "limite atingido antes do escape"},
    };

    const size_t quantidade = sizeof(casos) /sizeof(casos[0]);

    for (size_t i = 0; i < quantidade; ++i) {
        const CasoEscapeTime caso = casos[i];
        const int32_t obtido = mandelbrot_escape_time(
            caso.cr, caso.ci,
            caso.max_iter
        );
        if(obtido != caso.esperado){
            fprintf(stderr, "Falhou: %s; esperado %" PRId32 ", obtido %" PRId32 "\n",
                caso.descricao, caso.esperado, obtido);
            return EXIT_FAILURE;
        }
    }
    printf("OK: %zu casos de escape-time passaram. \n", quantidade);
    return EXIT_SUCCESS;
}