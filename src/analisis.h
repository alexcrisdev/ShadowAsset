#ifndef SHADOWASSET_ANALISIS_H
#define SHADOWASSET_ANALISIS_H

#include "dispositivo.h"

enum Modo {
    MODO_CAMBIOS,      /* referencia = instantánea anterior, observado = actual */
    MODO_CUMPLIMIENTO  /* referencia = inventario de TI, observado = medición */
};

enum Categoria {
    CAT_SOLO_OBSERVADO,   /* NUEVO / NO_INVENTARIADO */
    CAT_SOLO_REFERENCIA,  /* NO_OBSERVADO */
    CAT_CAMBIO,           /* CAMBIO / DISCREPANCIA */
    CAT_IGUAL,            /* PERSISTENTE / CONFORME */
    CAT_TOTAL
};

struct Hallazgo {
    enum Categoria categoria;
    const struct Dispositivo *referencia; /* NULL si no existe */
    const struct Dispositivo *observado;  /* NULL si no existe */
};

enum Severidad { SEV_INFO, SEV_BAJA, SEV_MEDIA, SEV_ALTA };

enum TipoAlerta {
    ALERTA_IP_DUPLICADA,         /* dos MAC con la misma IP en una medición */
    ALERTA_IP_REASIGNADA,        /* una IP conocida responde ahora con otra MAC */
    ALERTA_POSIBLE_MISMO_EQUIPO, /* mismo hostname, distinta MAC */
    ALERTA_MAC_LOCAL             /* MAC aleatoria o virtual */
};

#define MAX_MENSAJE 256

struct Alerta {
    enum TipoAlerta tipo;
    enum Severidad severidad;
    char ip[MAX_IP];
    char mac[MAX_MAC];
    char mensaje[MAX_MENSAJE];
};

struct ListaAlertas {
    struct Alerta *items;
    size_t cantidad;
    size_t capacidad;
};

struct Resultado {
    enum Modo modo;
    struct Hallazgo *hallazgos;
    size_t cantidad;
    size_t por_categoria[CAT_TOTAL];
    struct ListaAlertas alertas;
};

/* Alertas que se pueden detectar en una sola medición: IP duplicadas y
 * MAC administradas localmente. */
bool alertas_de_lista(const struct ListaDispositivos *lista, struct ListaAlertas *alertas);

/* Compara dos listas ordenadas por MAC (csv_cargar ya las ordena) y genera
 * hallazgos y alertas. ip_critica (puede ser NULL) suele ser la puerta de
 * enlace: si cambia de MAC la alerta sube a severidad ALTA.
 * El resultado apunta a las listas: no liberarlas antes que el resultado. */
bool analizar(enum Modo modo,
              const struct ListaDispositivos *referencia,
              const struct ListaDispositivos *observado,
              const char *ip_critica,
              struct Resultado *resultado);

/* true si hay algo que una persona debería revisar. */
bool resultado_requiere_revision(const struct Resultado *resultado);
bool alertas_requieren_revision(const struct ListaAlertas *alertas);

void resultado_liberar(struct Resultado *resultado);
void alertas_liberar(struct ListaAlertas *alertas);

const char *categoria_nombre(enum Modo modo, enum Categoria categoria);
const char *severidad_nombre(enum Severidad severidad);
const char *alerta_nombre(enum TipoAlerta tipo);

#endif
