#ifndef _SPI_H_
#define _SPI_H_

//Nils Wei� 
//20.04.2012
//Compiler CC5x


void spi_init();
void spi_send(char data);
char spi_receive(char data);
void spi_send_arr(char *array, char length);
#ifdef OLD
void spi_send_ledbuf(char *array_r, char *array_g, char *array_b);
#else
void spi_send_ledbuf(char *array);
#endif

#include "include_files\spi.c"

#endif