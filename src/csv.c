#include "csv.h"

#include "registro.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static char *recortar(char *texto)
{
    while (*texto == ' ' || *texto == '\t') {
        texto++;
    }
    size_t largo = strlen(texto);
    while (largo > 0 && (texto[largo - 1] == ' ' || texto[largo - 1] == '\t')) {
        texto[--largo] = '\0';
    }
    return texto;
}

int csv_dividir_linea(char *linea, char separador, char *campos[], int max_campos)
{
    int n = 0;
    char *leer = linea;

    for (;;) {
        if (n == max_campos) {
            return -1;
        }
        while (*leer == ' ' || *leer == '\t') {
            leer++;
        }

        char *inicio = leer;
        char *escribir = leer;
        bool entre_comillas = (*leer == '"');

        if (entre_comillas) {
            leer++;
            for (;;) {
                if (*leer == '\0') {
                    return -1; /* comillas sin cerrar */
                }
                if (*leer == '"') {
                    if (leer[1] == '"') { /* "" dentro de comillas = " */
                        *escribir++ = '"';
                        leer += 2;
                        continue;
                    }
                    leer++;
                    break;
                }
                *escribir++ = *leer++;
            }
            while (*leer == ' ' || *leer == '\t') {
                leer++;
            }
            if (*leer != separador && *leer != '\0') {
                return -1; /* texto después de cerrar comillas */
            }
        } else {
            while (*leer != separador && *leer != '\0') {
                if (*leer == '"') {
                    return -1; /* comilla en medio de un campo sin comillas */
                }
                escribir++;
                leer++;
            }
        }

        char siguiente = *leer;
        *escribir = '\0';
        campos[n++] = entre_comillas ? inicio : recortar(inicio);
        if (siguiente == '\0') {
            return n;
        }
        leer++;
    }
}

struct Columnas {
    int ip;
    int mac;
    int hostname;
    int total;
};

static bool leer_cabecera(const char *nombre, char *linea, char separador, struct Columnas *col)
{
    char *campos[CSV_MAX_COLUMNAS];
    int n = csv_dividir_linea(linea, separador, campos, CSV_MAX_COLUMNAS);
    if (n < 0) {
        registro_mensaje("%s:1: cabecera CSV invalida\n", nombre);
        return false;
    }

    col->ip = col->mac = col->hostname = -1;
    col->total = n;
    for (int i = 0; i < n; i++) {
        int *destino = NULL;
        if (texto_igual_sin_mayusculas(campos[i], "ip")) {
            destino = &col->ip;
        } else if (texto_igual_sin_mayusculas(campos[i], "mac")) {
            destino = &col->mac;
        } else if (texto_igual_sin_mayusculas(campos[i], "hostname")) {
            destino = &col->hostname;
        }
        if (destino != NULL) {
            if (*destino >= 0) {
                registro_mensaje("%s:1: la columna '%s' esta repetida\n", nombre, campos[i]);
                return false;
            }
            *destino = i;
        }
    }
    if (col->ip < 0 || col->mac < 0) {
        registro_mensaje("%s:1: la cabecera debe incluir las columnas 'ip' y 'mac'\n", nombre);
        return false;
    }
    return true;
}

static bool linea_vacia(const char *linea)
{
    for (; *linea != '\0'; linea++) {
        if (!isspace((unsigned char)*linea)) {
            return false;
        }
    }
    return true;
}

static bool procesar_fila(const char *nombre, unsigned long numero, char *linea,
                          char separador, const struct Columnas *col,
                          struct ListaDispositivos *lista)
{
    char *campos[CSV_MAX_COLUMNAS];
    int n = csv_dividir_linea(linea, separador, campos, CSV_MAX_COLUMNAS);
    if (n != col->total) {
        registro_mensaje("%s:%lu: se esperaban %d campos y hay %s\n", nombre, numero,
                col->total, n < 0 ? "un error de comillas" : "otra cantidad");
        return false;
    }

    struct Dispositivo d;
    const char *host = col->hostname >= 0 ? campos[col->hostname] : "";
    if (!validar_ip(campos[col->ip], d.ip)) {
        registro_mensaje("%s:%lu: IP no valida\n", nombre, numero);
        return false;
    }
    if (!validar_mac(campos[col->mac], d.mac)) {
        registro_mensaje("%s:%lu: MAC no valida (formato AA:BB:CC:DD:EE:FF)\n", nombre, numero);
        return false;
    }
    if (!validar_hostname(host, d.hostname)) {
        registro_mensaje("%s:%lu: hostname no valido (letras, digitos, '-', '_', '.')\n",
                nombre, numero);
        return false;
    }
    if (!lista_agregar(lista, &d)) {
        registro_mensaje("%s:%lu: demasiados dispositivos o memoria insuficiente\n",
                nombre, numero);
        return false;
    }
    return true;
}

bool csv_parsear_texto(const char *nombre, char *texto, size_t largo,
                       struct ListaDispositivos *lista)
{
    lista_iniciar(lista);

    if (largo >= 2 && (unsigned char)texto[0] == 0xFF && (unsigned char)texto[1] == 0xFE) {
        registro_mensaje("%s: el archivo esta en UTF-16; guardelo como CSV UTF-8\n", nombre);
        return false;
    }
    if (memchr(texto, '\0', largo) != NULL) {
        registro_mensaje("%s: contiene bytes nulos (no parece un CSV de texto)\n", nombre);
        return false;
    }

    char *p = texto;
    char *fin = texto + largo;
    if (largo >= 3 && memcmp(p, "\xEF\xBB\xBF", 3) == 0) {
        p += 3; /* BOM UTF-8 (Excel lo añade al guardar) */
    }

    unsigned long numero = 0;
    bool cabecera_vista = false;
    char separador = ',';
    struct Columnas col = {0};
    bool ok = true;

    while (ok && p < fin) {
        numero++;
        char *salto = memchr(p, '\n', (size_t)(fin - p));
        char *fin_linea = salto != NULL ? salto : fin;
        char *siguiente = salto != NULL ? salto + 1 : fin;
        *fin_linea = '\0';
        if (fin_linea > p && fin_linea[-1] == '\r') {
            fin_linea[-1] = '\0';
        }

        if (strlen(p) > CSV_MAX_LINEA) {
            registro_mensaje("%s:%lu: linea demasiado larga (max %d)\n",
                    nombre, numero, CSV_MAX_LINEA);
            ok = false;
        } else if (linea_vacia(p)) {
            /* se ignora */
        } else if (!cabecera_vista) {
            /* Excel en español guarda con ';' */
            if (strchr(p, ';') != NULL && strchr(p, ',') == NULL) {
                separador = ';';
            }
            ok = leer_cabecera(nombre, p, separador, &col);
            cabecera_vista = true;
        } else {
            ok = procesar_fila(nombre, numero, p, separador, &col, lista);
        }
        p = siguiente;
    }

    if (ok && !cabecera_vista) {
        registro_mensaje("%s: archivo vacio o sin cabecera\n", nombre);
        ok = false;
    }
    if (ok) {
        long duplicado = lista_ordenar_por_mac(lista);
        if (duplicado >= 0) {
            registro_mensaje("%s: la MAC %s aparece mas de una vez\n",
                    nombre, lista->items[duplicado].mac);
            ok = false;
        }
    }
    if (!ok) {
        lista_liberar(lista);
    }
    return ok;
}

/* Lee el archivo completo en memoria (con límite de tamaño). Así la huella
 * SHA-256 corresponde exactamente a los bytes que se analizan. */
static char *leer_archivo(const char *ruta, size_t *largo)
{
    FILE *archivo = fopen(ruta, "rb");
    if (archivo == NULL) {
        registro_mensaje("Error: no se pudo abrir '%s': %s\n", ruta, strerror(errno));
        return NULL;
    }

    size_t capacidad = 4096;
    size_t usado = 0;
    char *datos = malloc(capacidad + 1);
    bool ok = datos != NULL;

    while (ok) {
        if (usado == capacidad) {
            if (capacidad >= CSV_MAX_BYTES) {
                registro_mensaje("Error: '%s' supera el maximo de %u bytes\n",
                        ruta, CSV_MAX_BYTES);
                ok = false;
                break;
            }
            capacidad *= 2;
            char *nuevo = realloc(datos, capacidad + 1);
            if (nuevo == NULL) {
                registro_mensaje("Error: memoria insuficiente\n");
                ok = false;
                break;
            }
            datos = nuevo;
        }
        size_t leidos = fread(datos + usado, 1, capacidad - usado, archivo);
        usado += leidos;
        if (leidos == 0) {
            if (ferror(archivo)) {
                registro_mensaje("Error de lectura en '%s'\n", ruta);
                ok = false;
            }
            break;
        }
    }
    fclose(archivo);

    if (!ok) {
        if (datos == NULL) {
            registro_mensaje("Error: memoria insuficiente\n");
        }
        free(datos);
        return NULL;
    }
    datos[usado] = '\0';
    *largo = usado;
    return datos;
}

bool csv_cargar(const char *ruta, struct ListaDispositivos *lista, char sha256[SHA256_HEX])
{
    lista_iniciar(lista);
    size_t largo = 0;
    char *datos = leer_archivo(ruta, &largo);
    if (datos == NULL) {
        return false;
    }
    if (sha256 != NULL) {
        sha256_texto(datos, largo, sha256);
    }
    bool ok = csv_parsear_texto(ruta, datos, largo, lista);
    free(datos);
    return ok;
}

FILE *csv_crear_archivo_nuevo(const char *ruta)
{
    /* "x" (C11): falla si el archivo ya existe; evita pisar datos. */
    FILE *salida = fopen(ruta, "wx");
    if (salida == NULL) {
        if (errno == EEXIST) {
            registro_mensaje("Error: '%s' ya existe; elija otro nombre\n", ruta);
        } else {
            registro_mensaje("Error: no se pudo crear '%s': %s\n", ruta, strerror(errno));
        }
    }
    return salida;
}

bool csv_guardar_instantanea(const char *ruta, const struct ListaDispositivos *lista)
{
    FILE *salida = csv_crear_archivo_nuevo(ruta);
    if (salida == NULL) {
        return false;
    }
    fprintf(salida, "ip,mac,hostname\n");
    for (size_t i = 0; i < lista->cantidad; i++) {
        const struct Dispositivo *d = &lista->items[i];
        fprintf(salida, "%s,%s,%s\n", d->ip, d->mac, d->hostname);
    }
    bool ok = !ferror(salida);
    if (fclose(salida) != 0) {
        ok = false;
    }
    if (!ok) {
        registro_mensaje("Error al escribir '%s'\n", ruta);
    }
    return ok;
}

void csv_escribir_campo_seguro(FILE *salida, const char *valor)
{
    fputc('"', salida);
    if (valor[0] != '\0' && strchr("=+-@\t\r", valor[0]) != NULL) {
        fputc('\'', salida); /* Excel muestra el texto en vez de ejecutarlo */
    }
    for (const char *p = valor; *p != '\0'; p++) {
        if (*p == '"') {
            fputc('"', salida);
        }
        fputc(*p, salida);
    }
    fputc('"', salida);
}
