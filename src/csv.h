#ifndef SHADOWASSET_CSV_H
#define SHADOWASSET_CSV_H

#include <stdbool.h>
#include <stdio.h>

#include "dispositivo.h"
#include "sha256.h"

#define CSV_MAX_LINEA 1024
#define CSV_MAX_COLUMNAS 32
#define CSV_MAX_BYTES (16u * 1024u * 1024u)

/* Carga un CSV de dispositivos. La cabecera debe incluir las columnas "ip" y
 * "mac" (y opcionalmente "hostname"), en cualquier orden y sin distinguir
 * mayúsculas; las demás columnas se ignoran. El separador (',' o ';') se
 * detecta en la cabecera. Se valida cada fila y se rechazan MAC duplicadas.
 * La lista queda ordenada por MAC. Si sha256 no es NULL, recibe la huella
 * de los bytes exactos que se analizaron. Los errores van a stderr. */
bool csv_cargar(const char *ruta, struct ListaDispositivos *lista, char sha256[SHA256_HEX]);

/* Igual que csv_cargar pero sobre un texto en memoria (se modifica).
 * texto[largo] debe ser '\0'. "nombre" solo se usa en los mensajes. */
bool csv_parsear_texto(const char *nombre, char *texto, size_t largo,
                       struct ListaDispositivos *lista);

/* Divide una línea CSV (RFC 4180, sin saltos de línea dentro de comillas).
 * Modifica la línea. Devuelve el número de campos o -1 si es inválida. */
int csv_dividir_linea(char *linea, char separador, char *campos[], int max_campos);

/* Escribe una instantánea "ip,mac,hostname". Nunca sobrescribe un archivo
 * existente, para conservar el historial. */
bool csv_guardar_instantanea(const char *ruta, const struct ListaDispositivos *lista);

/* Abre un archivo nuevo para escritura; falla si ya existe. */
FILE *csv_crear_archivo_nuevo(const char *ruta);

/* Escribe un campo entre comillas, apto para Excel: duplica las comillas y
 * neutraliza valores que Excel interpretaría como fórmula (=, +, -, @...). */
void csv_escribir_campo_seguro(FILE *salida, const char *valor);

#endif
