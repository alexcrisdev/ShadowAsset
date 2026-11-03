#include "dispositivo.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* Lee un octeto decimal (0-255, sin ceros a la izquierda). Avanza *p. */
static bool leer_octeto(const char **p, unsigned *valor)
{
    const char *inicio = *p;
    unsigned v = 0;
    int digitos = 0;
    while (isdigit((unsigned char)**p)) {
        v = v * 10u + (unsigned)(**p - '0');
        digitos++;
        (*p)++;
        if (digitos > 3) {
            return false;
        }
    }
    /* "010" se rechaza: algunas herramientas lo interpretan como octal. */
    if (digitos == 0 || v > 255u || (digitos > 1 && *inicio == '0')) {
        return false;
    }
    *valor = v;
    return true;
}

static bool parsear_ipv4(const char *texto, uint32_t *numero)
{
    const char *p = texto;
    uint32_t resultado = 0;
    for (int i = 0; i < 4; i++) {
        unsigned octeto;
        if (!leer_octeto(&p, &octeto)) {
            return false;
        }
        resultado = (resultado << 8) | octeto;
        if (i < 3) {
            if (*p != '.') {
                return false;
            }
            p++;
        }
    }
    if (*p != '\0') {
        return false;
    }
    *numero = resultado;
    return true;
}

bool validar_ip(const char *texto, char destino[MAX_IP])
{
    size_t largo = strlen(texto);
    uint32_t numero;
    if (largo < 7 || largo >= MAX_IP || !parsear_ipv4(texto, &numero)) {
        return false;
    }
    memcpy(destino, texto, largo + 1);
    return true;
}

bool ip_a_numero(const char *ip, uint32_t *numero)
{
    return parsear_ipv4(ip, numero);
}

bool validar_mac(const char *texto, char destino[MAX_MAC])
{
    if (strlen(texto) != MAX_MAC - 1) {
        return false;
    }

    char separador = texto[2];
    if (separador != ':' && separador != '-') {
        return false;
    }

    char normalizada[MAX_MAC];
    for (size_t i = 0; i < MAX_MAC - 1; i++) {
        unsigned char c = (unsigned char)texto[i];
        if (i % 3 == 2) {
            if (c != (unsigned char)separador) {
                return false; /* separadores mezclados o mal ubicados */
            }
            normalizada[i] = ':';
        } else {
            if (!isxdigit(c)) {
                return false;
            }
            normalizada[i] = (char)toupper(c);
        }
    }
    normalizada[MAX_MAC - 1] = '\0';

    memcpy(destino, normalizada, MAX_MAC);
    return true;
}

bool validar_hostname(const char *texto, char destino[MAX_HOST])
{
    size_t largo = strlen(texto);
    if (largo >= MAX_HOST) {
        return false;
    }
    for (size_t i = 0; i < largo; i++) {
        unsigned char c = (unsigned char)texto[i];
        if (!isalnum(c) && c != '-' && c != '_' && c != '.') {
            return false;
        }
    }
    /* Un nombre no puede empezar por '-' ni '.' (además evita fórmulas). */
    if (largo > 0 && (texto[0] == '-' || texto[0] == '.')) {
        return false;
    }

    memcpy(destino, texto, largo + 1);
    return true;
}

bool mac_es_local(const char *mac)
{
    /* mac[1] es el nibble bajo del primer byte; el bit 0x2 es "local". */
    char c = mac[1];
    int valor = isdigit((unsigned char)c) ? c - '0' : toupper((unsigned char)c) - 'A' + 10;
    return (valor & 0x2) != 0;
}

bool subred_parsear(const char *texto, struct Subred *subred)
{
    const char *barra = strchr(texto, '/');
    if (barra == NULL || barra == texto || (size_t)(barra - texto) >= MAX_IP) {
        return false;
    }

    char ip[MAX_IP];
    memcpy(ip, texto, (size_t)(barra - texto));
    ip[barra - texto] = '\0';

    uint32_t red;
    if (!parsear_ipv4(ip, &red)) {
        return false;
    }

    const char *p = barra + 1;
    int prefijo = 0;
    int digitos = 0;
    while (isdigit((unsigned char)*p) && digitos < 3) {
        prefijo = prefijo * 10 + (*p - '0');
        p++;
        digitos++;
    }
    if (digitos == 0 || *p != '\0' || prefijo > 32) {
        return false;
    }

    uint32_t mascara = prefijo == 0 ? 0u : 0xFFFFFFFFu << (32 - prefijo);
    if ((red & ~mascara) != 0) {
        return false; /* p. ej. 192.168.1.5/24: ¿se quería .0/24? */
    }

    subred->red = red;
    subred->mascara = mascara;
    subred->prefijo = prefijo;
    return true;
}

bool subred_contiene(const struct Subred *subred, const char *ip)
{
    uint32_t numero;
    return parsear_ipv4(ip, &numero) && (numero & subred->mascara) == subred->red;
}

void lista_iniciar(struct ListaDispositivos *lista)
{
    lista->items = NULL;
    lista->cantidad = 0;
    lista->capacidad = 0;
}

bool lista_agregar(struct ListaDispositivos *lista, const struct Dispositivo *d)
{
    if (lista->cantidad == lista->capacidad) {
        if (lista->capacidad >= MAX_DISPOSITIVOS) {
            return false;
        }
        size_t nueva = lista->capacidad == 0 ? 16 : lista->capacidad * 2;
        if (nueva > MAX_DISPOSITIVOS) {
            nueva = MAX_DISPOSITIVOS;
        }
        struct Dispositivo *items = realloc(lista->items, nueva * sizeof(*items));
        if (items == NULL) {
            return false;
        }
        lista->items = items;
        lista->capacidad = nueva;
    }
    lista->items[lista->cantidad++] = *d;
    return true;
}

void lista_liberar(struct ListaDispositivos *lista)
{
    free(lista->items);
    lista_iniciar(lista);
}

static int comparar_mac(const void *a, const void *b)
{
    const struct Dispositivo *da = a;
    const struct Dispositivo *db = b;
    return strcmp(da->mac, db->mac);
}

long lista_ordenar_por_mac(struct ListaDispositivos *lista)
{
    if (lista->cantidad == 0) {
        return -1;
    }
    qsort(lista->items, lista->cantidad, sizeof(lista->items[0]), comparar_mac);
    for (size_t i = 1; i < lista->cantidad; i++) {
        if (strcmp(lista->items[i - 1].mac, lista->items[i].mac) == 0) {
            return (long)i;
        }
    }
    return -1;
}

bool texto_igual_sin_mayusculas(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return false;
        }
        a++;
        b++;
    }
    return *a == *b;
}
