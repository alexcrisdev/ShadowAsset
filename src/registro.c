#include "registro.h"

#include <stdarg.h>
#include <stdio.h>

static ManejadorMensajes manejador_actual = NULL;
static void *contexto_actual = NULL;

void registro_establecer(ManejadorMensajes manejador, void *contexto)
{
    manejador_actual = manejador;
    contexto_actual = contexto;
}

void registro_mensaje(const char *formato, ...)
{
    char mensaje[1024];
    va_list args;
    va_start(args, formato);
    vsnprintf(mensaje, sizeof(mensaje), formato, args);
    va_end(args);

    if (manejador_actual != NULL) {
        manejador_actual(mensaje, contexto_actual);
    } else {
        fputs(mensaje, stderr);
    }
}
