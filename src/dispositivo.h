#ifndef SHADOWASSET_DISPOSITIVO_H
#define SHADOWASSET_DISPOSITIVO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAX_IP 16    /* "255.255.255.255" + '\0' */
#define MAX_MAC 18   /* "AA:BB:CC:DD:EE:FF" + '\0' */
#define MAX_HOST 64  /* 63 caracteres + '\0' */
#define MAX_DISPOSITIVOS 100000u

struct Dispositivo {
    char ip[MAX_IP];
    char mac[MAX_MAC];       /* siempre normalizada: mayúsculas y ':' */
    char hostname[MAX_HOST]; /* puede estar vacío (desconocido) */
};

struct ListaDispositivos {
    struct Dispositivo *items;
    size_t cantidad;
    size_t capacidad;
};

struct Subred {
    uint32_t red;
    uint32_t mascara;
    int prefijo;
};

/* Validan un texto no confiable y, si es correcto, lo copian normalizado en
 * destino. Devuelven false (sin tocar destino) si el valor no es válido. */
bool validar_ip(const char *texto, char destino[MAX_IP]);
bool validar_mac(const char *texto, char destino[MAX_MAC]);
bool validar_hostname(const char *texto, char destino[MAX_HOST]);

/* Convierte una IPv4 ya validada a número (orden de red legible: a.b.c.d). */
bool ip_a_numero(const char *ip, uint32_t *numero);

/* Una MAC "administrada localmente" (bit 0x02 del primer byte) suele ser
 * aleatoria (móviles con privacidad activada) o de una máquina virtual. */
bool mac_es_local(const char *mac_normalizada);

/* Interpreta "192.168.1.0/24". Rechaza bits de host activos ("192.168.1.5/24"). */
bool subred_parsear(const char *texto, struct Subred *subred);
bool subred_contiene(const struct Subred *subred, const char *ip);

void lista_iniciar(struct ListaDispositivos *lista);
bool lista_agregar(struct ListaDispositivos *lista, const struct Dispositivo *d);
void lista_liberar(struct ListaDispositivos *lista);

/* Ordena por MAC. Devuelve el índice del primer duplicado o -1 si no hay. */
long lista_ordenar_por_mac(struct ListaDispositivos *lista);

/* Comparación sin distinguir mayúsculas (ASCII). */
bool texto_igual_sin_mayusculas(const char *a, const char *b);

#endif
