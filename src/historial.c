#include "historial.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif

#define PREFIJO "snapshot_"
#define SUFIJO ".csv"
/* snapshot_ + AAAAMMDD_HHMMSS (15) + .csv */
#define LARGO_NOMBRE (sizeof(PREFIJO) - 1 + 15 + sizeof(SUFIJO) - 1)

bool historial_es_nombre_valido(const char *nombre)
{
    if (strlen(nombre) != LARGO_NOMBRE || strncmp(nombre, PREFIJO, sizeof(PREFIJO) - 1) != 0 ||
        strcmp(nombre + LARGO_NOMBRE - (sizeof(SUFIJO) - 1), SUFIJO) != 0) {
        return false;
    }
    const char *marca = nombre + sizeof(PREFIJO) - 1;
    for (int i = 0; i < 15; i++) {
        bool esperado_guion = (i == 8);
        if (esperado_guion ? marca[i] != '_' : !isdigit((unsigned char)marca[i])) {
            return false;
        }
    }
    return true;
}

static bool unir(const char *carpeta, const char *nombre, char ruta[HISTORIAL_MAX_RUTA])
{
    size_t largo = strlen(carpeta);
    bool barra = largo > 0 && (carpeta[largo - 1] == '/' || carpeta[largo - 1] == '\\');
    int n = snprintf(ruta, HISTORIAL_MAX_RUTA, "%s%s%s", carpeta, barra ? "" : "/", nombre);
    return n > 0 && n < HISTORIAL_MAX_RUTA;
}

bool historial_ruta_nueva(const char *carpeta, char ruta[HISTORIAL_MAX_RUTA])
{
    time_t ahora = time(NULL);
    struct tm *local = localtime(&ahora);
    char nombre[64];
    if (local == NULL || strftime(nombre, sizeof(nombre), PREFIJO "%Y%m%d_%H%M%S" SUFIJO,
                                  local) == 0) {
        return false;
    }
    return unir(carpeta, nombre, ruta);
}

bool historial_ultima(const char *carpeta, char ruta[HISTORIAL_MAX_RUTA])
{
    char mejor[LARGO_NOMBRE + 1] = "";

#ifdef _WIN32
    char patron[HISTORIAL_MAX_RUTA];
    if (!unir(carpeta, PREFIJO "*" SUFIJO, patron)) {
        return false;
    }
    WIN32_FIND_DATAA datos;
    HANDLE busqueda = FindFirstFileA(patron, &datos);
    if (busqueda == INVALID_HANDLE_VALUE) {
        return false;
    }
    do {
        const char *nombre = datos.cFileName;
        if (!(datos.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            historial_es_nombre_valido(nombre) && strcmp(nombre, mejor) > 0) {
            memcpy(mejor, nombre, LARGO_NOMBRE + 1);
        }
    } while (FindNextFileA(busqueda, &datos));
    FindClose(busqueda);
#else
    DIR *dir = opendir(carpeta);
    if (dir == NULL) {
        return false;
    }
    struct dirent *entrada;
    while ((entrada = readdir(dir)) != NULL) {
        const char *nombre = entrada->d_name;
        if (historial_es_nombre_valido(nombre) && strcmp(nombre, mejor) > 0) {
            memcpy(mejor, nombre, LARGO_NOMBRE + 1);
        }
    }
    closedir(dir);
#endif

    return mejor[0] != '\0' && unir(carpeta, mejor, ruta);
}
