
#include "mandelbrot.h"
#include <math.h>

int32_t mandelbrot_escape_time(double cr, double ci, int32_t max_iter){

	double zr = 0.0;
	double zi = 0.0;

	for(int32_t iteracao = 0; iteracao < max_iter; ++iteracao){
		const double novo_zr = fma(zr, zr, -(zi * zi)) + cr;
		const double novo_zi = fma(zr, zi, zi * zr) + ci;

		zr = novo_zr;
		zi = novo_zi;

		if(zr*zr + zi * zi > 4.0){
			return iteracao;
		}
	}
	return max_iter;
}