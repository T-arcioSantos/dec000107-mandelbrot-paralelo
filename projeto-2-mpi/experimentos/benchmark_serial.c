#include "mandelbrot.h"
#include <omp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char **argv){
 if(argc!=8){fprintf(stderr,"Usage: %s width height max_iter re_min re_max im_min im_max\n",argv[0]);return 2;}
 MandelbrotConfig c={(strtod(argv[4],NULL)),(strtod(argv[5],NULL)),(strtod(argv[6],NULL)),(strtod(argv[7],NULL)),(size_t)strtoull(argv[1],NULL,10),(size_t)strtoull(argv[2],NULL,10),(int32_t)strtol(argv[3],NULL,10)};
 if(!c.largura||!c.altura||!c.max_iter||c.largura>SIZE_MAX/c.altura||c.largura*c.altura>SIZE_MAX/sizeof(int32_t))return 2;
 int32_t *m=malloc(c.largura*c.altura*sizeof(*m));if(!m)return 3;
 double start=omp_get_wtime();
 mandelbrot_gerar_serial(&c,m);
 double elapsed=omp_get_wtime()-start;
 printf("SERIAL;%zu;%zu;%d;%.9f\n",c.largura,c.altura,c.max_iter,elapsed);free(m);return 0;
}
