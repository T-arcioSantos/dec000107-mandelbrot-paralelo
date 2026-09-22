
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

static double coordenada(double minimo, double maximo, size_t indice, size_t quantidade){
	if(quantidade == 1){
		return minimo;
	}

	if(indice == quantidade -1){
		return maximo;
	}
	const double passo = (maximo - minimo) / (double)(quantidade -1);
	return minimo + (double) indice * passo;
}

void mandelbrot_gerar_linha(const MandelbrotConfig *config, size_t py, int32_t *matriz){
	const double ci = coordenada(
		config -> im_min,
		config-> im_max,
		py,
		config->altura
	);

	for(size_t px = 0; px < config->largura; ++px){
		const double cr = coordenada(
			config->re_min,
			config->re_max,
			px,
			config->largura
		);
		const size_t indice = py * config->largura + px;
		matriz[indice] = mandelbrot_escape_time(
			cr,
			ci,
			config->max_iter
		);
	}
}

void mandelbrot_gerar_serial(const MandelbrotConfig *config, int32_t *matriz){
	for(size_t py = 0; py < config->altura; ++py){
		mandelbrot_gerar_linha(config, py, matriz);
	}
}