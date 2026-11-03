#ifndef SHADOWASSET_REGISTRO_H
#define SHADOWASSET_REGISTRO_H

/* Destino de los mensajes de error y aviso de los módulos.
 * Por defecto se escriben en stderr (versión de consola). La interfaz
 * gráfica instala su propio manejador para mostrarlos en una ventana. */

typedef void (*ManejadorMensajes)(const char *mensaje, void *contexto);

/* NULL restaura el comportamiento por defecto (stderr). */
void registro_establecer(ManejadorMensajes manejador, void *contexto);

#if defined(__MINGW32__)
#include <stdio.h> /* define __MINGW_PRINTF_FORMAT */
#define REGISTRO_FORMATO __attribute__((format(__MINGW_PRINTF_FORMAT, 1, 2)))
#elif defined(__GNUC__)
#define REGISTRO_FORMATO __attribute__((format(printf, 1, 2)))
#else
#define REGISTRO_FORMATO
#endif

void registro_mensaje(const char *formato, ...) REGISTRO_FORMATO;

#endif
