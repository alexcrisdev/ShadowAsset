#ifndef SHADOWASSET_DESCUBRIMIENTO_H
#define SHADOWASSET_DESCUBRIMIENTO_H

#include "dispositivo.h"

/* Lee la tabla de vecinos IPv4 (caché ARP) del sistema operativo.
 * Es una lectura pasiva: no envía paquetes ni requiere administrador.
 * Solo incluye vecinos con MAC unicast. Si "alcance" no es NULL, descarta
 * las IP fuera de esa subred. La lista queda ordenada por MAC y sin
 * duplicados. El hostname queda vacío (no se resuelve por DNS). */
bool descubrir_vecinos(const struct Subred *alcance, struct ListaDispositivos *lista);

/* Obtiene la IPv4 de la puerta de enlace por defecto (ruta 0.0.0.0/0 con
 * menor métrica). Devuelve false si no hay o no está soportado. */
bool descubrir_puerta_enlace(char ip[MAX_IP]);

#endif
