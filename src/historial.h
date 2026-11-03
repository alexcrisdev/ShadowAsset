#ifndef SHADOWASSET_HISTORIAL_H
#define SHADOWASSET_HISTORIAL_H

#include <stdbool.h>
#include <stddef.h>

#define HISTORIAL_MAX_RUTA 512

/* Las instantáneas se llaman "snapshot_AAAAMMDD_HHMMSS.csv", de modo que el
 * orden alfabético coincide con el cronológico. */

/* Construye la ruta de una instantánea nueva con la hora local actual. */
bool historial_ruta_nueva(const char *carpeta, char ruta[HISTORIAL_MAX_RUTA]);

/* Busca la instantánea más reciente de la carpeta. Devuelve false si no hay
 * ninguna (o la carpeta no existe). */
bool historial_ultima(const char *carpeta, char ruta[HISTORIAL_MAX_RUTA]);

/* true si el nombre de archivo tiene el formato de instantánea. */
bool historial_es_nombre_valido(const char *nombre);

#endif
