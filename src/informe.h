#ifndef SHADOWASSET_INFORME_H
#define SHADOWASSET_INFORME_H

#include "analisis.h"
#include "dispositivo.h"
#include "sha256.h"

#define SHADOWASSET_VERSION "1.1.0"

/* Archivo de entrada analizado, con su huella para trazabilidad. */
struct Entrada {
    const char *rol;  /* "anterior", "actual", "observado", "inventario"... */
    const char *ruta;
    char sha256[SHA256_HEX];
    size_t dispositivos;
};

struct ContextoInforme {
    const char *comando;
    char separador;          /* ',' o ';' para el CSV */
    const char *ip_critica;  /* puerta de enlace u otra IP vigilada; puede ser NULL */
    struct Entrada entradas[2];
    int n_entradas;
};

#define INFORME_MAX_DETALLE 192

/* Texto legible que explica un hallazgo ("IP 10.0.0.1 -> 10.0.0.2", etc.). */
void informe_describir(const struct Resultado *resultado, const struct Hallazgo *hallazgo,
                       char detalle[INFORME_MAX_DETALLE]);

void informe_mostrar_lista(const struct ListaDispositivos *lista);
void informe_mostrar_alertas(const struct ListaAlertas *alertas);
void informe_mostrar_resultado(const struct Resultado *resultado);
void informe_mostrar_entradas(const struct ContextoInforme *ctx);

/* Exporta a un archivo nuevo: JSON si la ruta termina en ".json"; si no, CSV
 * apto para Excel (protegido contra inyección de fórmulas). */
bool informe_exportar(const char *ruta, const struct ContextoInforme *ctx,
                      const struct Resultado *resultado);

/* Fecha y hora UTC en formato ISO 8601 ("2026-11-02T21:30:00Z"). */
void informe_fecha_utc(char destino[32]);

#endif
