#include "informe.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "csv.h"
#include "registro.h"



static const char *nombre_visible(const char *hostname)
{
    return hostname[0] != '\0' ? hostname : "(desconocido)";
}

void informe_fecha_utc(char destino[32])
{
    time_t ahora = time(NULL);
    struct tm *utc = gmtime(&ahora);
    if (utc == NULL || strftime(destino, 32, "%Y-%m-%dT%H:%M:%SZ", utc) == 0) {
        snprintf(destino, 32, "desconocida");
    }
}

void informe_describir(const struct Resultado *r, const struct Hallazgo *h,
                      char detalle[INFORME_MAX_DETALLE])
{
    detalle[0] = '\0';
    switch (h->categoria) {
    case CAT_SOLO_OBSERVADO:
        snprintf(detalle, INFORME_MAX_DETALLE, "%s",
                 r->modo == MODO_CAMBIOS ? "Observado por primera vez"
                                         : "Revisar: no figura en el inventario aprobado");
        break;
    case CAT_SOLO_REFERENCIA:
        snprintf(detalle, INFORME_MAX_DETALLE, "%s",
                 "No observado en la medicion (no implica que este apagado)");
        break;
    case CAT_CAMBIO: {
        /* Cambios: "anterior -> actual". Auditoria: "inventario vs observado". */
        const char *flecha = r->modo == MODO_CAMBIOS ? "->" : "(inventario) vs";
        size_t usado = 0;
        if (strcmp(h->referencia->ip, h->observado->ip) != 0) {
            int n = snprintf(detalle, INFORME_MAX_DETALLE, "IP %s %s %s",
                             h->referencia->ip, flecha, h->observado->ip);
            usado = n > 0 ? (size_t)n : 0;
        }
        if (!texto_igual_sin_mayusculas(h->referencia->hostname, h->observado->hostname) &&
            usado < INFORME_MAX_DETALLE) {
            snprintf(detalle + usado, INFORME_MAX_DETALLE - usado, "%shostname %s %s %s",
                     usado > 0 ? "; " : "",
                     nombre_visible(h->referencia->hostname), flecha,
                     nombre_visible(h->observado->hostname));
        }
        break;
    }
    case CAT_IGUAL:
    case CAT_TOTAL:
        break;
    }
}

/* ---------- Consola ---------- */

void informe_mostrar_lista(const struct ListaDispositivos *lista)
{
    printf("%-15s  %-17s  %s\n", "IP", "MAC", "HOSTNAME");
    printf("%-15s  %-17s  %s\n", "---------------", "-----------------", "--------");
    for (size_t i = 0; i < lista->cantidad; i++) {
        const struct Dispositivo *d = &lista->items[i];
        printf("%-15s  %-17s  %s\n", d->ip, d->mac, nombre_visible(d->hostname));
    }
    printf("\nTotal: %zu dispositivo(s)\n", lista->cantidad);
}

void informe_mostrar_alertas(const struct ListaAlertas *alertas)
{
    if (alertas->cantidad == 0) {
        printf("\nAlertas: ninguna\n");
        return;
    }
    printf("\nAlertas (%zu):\n", alertas->cantidad);
    for (size_t i = 0; i < alertas->cantidad; i++) {
        const struct Alerta *a = &alertas->items[i];
        printf("  [%-5s] %-20s %-15s %s\n", severidad_nombre(a->severidad),
               alerta_nombre(a->tipo), a->ip, a->mensaje);
    }
}

void informe_mostrar_entradas(const struct ContextoInforme *ctx)
{
    printf("Entradas:\n");
    for (int i = 0; i < ctx->n_entradas; i++) {
        const struct Entrada *e = &ctx->entradas[i];
        printf("  %-10s %s (%zu disp.)\n             sha256 %s\n",
               e->rol, e->ruta, e->dispositivos, e->sha256);
    }
    printf("\n");
}

void informe_mostrar_resultado(const struct Resultado *r)
{
    printf("%-16s  %-17s  %-15s  %-18s  %s\n", "ESTADO", "MAC", "IP", "HOSTNAME", "DETALLE");
    for (size_t i = 0; i < r->cantidad; i++) {
        const struct Hallazgo *h = &r->hallazgos[i];
        const struct Dispositivo *d = h->observado ? h->observado : h->referencia;
        char detalle[INFORME_MAX_DETALLE];
        informe_describir(r, h, detalle);
        printf("%-16s  %-17s  %-15s  %-18s  %s\n",
               categoria_nombre(r->modo, h->categoria), d->mac, d->ip,
               nombre_visible(d->hostname), detalle);
    }

    printf("\nResumen:\n");
    for (int c = 0; c < CAT_TOTAL; c++) {
        printf("  %-16s %zu\n", categoria_nombre(r->modo, (enum Categoria)c),
               r->por_categoria[c]);
    }
    informe_mostrar_alertas(&r->alertas);
    if (r->modo == MODO_CUMPLIMIENTO) {
        printf("\nNota: las discrepancias son para revision manual; no indican que un\n"
               "dispositivo sea malicioso.\n");
    }
}

/* ---------- CSV ---------- */

static void fila_csv(FILE *salida, char sep, const char *const campos[], size_t n)
{
    for (size_t c = 0; c < n; c++) {
        csv_escribir_campo_seguro(salida, campos[c]);
        fputc(c + 1 < n ? sep : '\n', salida);
    }
}

static void exportar_csv(FILE *salida, const struct ContextoInforme *ctx,
                         const struct Resultado *r)
{
    char sep = ctx->separador;
    fputs("\xEF\xBB\xBF", salida); /* BOM: Excel reconoce UTF-8 */

    const char *const cabecera_cambios[] = {
        "tipo", "estado", "severidad", "mac", "ip_anterior", "ip_actual",
        "hostname_anterior", "hostname_actual", "detalle"};
    const char *const cabecera_auditoria[] = {
        "tipo", "estado", "severidad", "mac", "ip_inventario", "ip_observada",
        "hostname_inventario", "hostname_observado", "detalle"};
    fila_csv(salida, sep, r->modo == MODO_CAMBIOS ? cabecera_cambios : cabecera_auditoria, 9);

    for (size_t i = 0; i < r->cantidad; i++) {
        const struct Hallazgo *h = &r->hallazgos[i];
        const struct Dispositivo *d = h->observado ? h->observado : h->referencia;
        char detalle[INFORME_MAX_DETALLE];
        informe_describir(r, h, detalle);
        const char *const campos[] = {
            "HALLAZGO",
            categoria_nombre(r->modo, h->categoria),
            "",
            d->mac,
            h->referencia ? h->referencia->ip : "",
            h->observado ? h->observado->ip : "",
            h->referencia ? h->referencia->hostname : "",
            h->observado ? h->observado->hostname : "",
            detalle,
        };
        fila_csv(salida, sep, campos, 9);
    }

    for (size_t i = 0; i < r->alertas.cantidad; i++) {
        const struct Alerta *a = &r->alertas.items[i];
        const char *const campos[] = {
            "ALERTA", alerta_nombre(a->tipo), severidad_nombre(a->severidad),
            a->mac, "", a->ip, "", "", a->mensaje,
        };
        fila_csv(salida, sep, campos, 9);
    }
}

/* ---------- JSON ---------- */

static void json_texto(FILE *salida, const char *texto)
{
    fputc('"', salida);
    for (const unsigned char *p = (const unsigned char *)texto; *p != '\0'; p++) {
        switch (*p) {
        case '"': fputs("\\\"", salida); break;
        case '\\': fputs("\\\\", salida); break;
        case '\n': fputs("\\n", salida); break;
        case '\r': fputs("\\r", salida); break;
        case '\t': fputs("\\t", salida); break;
        default:
            if (*p < 0x20) {
                fprintf(salida, "\\u%04x", *p);
            } else {
                fputc(*p, salida);
            }
        }
    }
    fputc('"', salida);
}

static void json_par(FILE *salida, const char *clave, const char *valor, bool coma)
{
    json_texto(salida, clave);
    fputs(": ", salida);
    if (valor == NULL) {
        fputs("null", salida);
    } else {
        json_texto(salida, valor);
    }
    fputs(coma ? ", " : "", salida);
}

static void json_dispositivo(FILE *salida, const char *clave, const struct Dispositivo *d)
{
    json_texto(salida, clave);
    fputs(": ", salida);
    if (d == NULL) {
        fputs("null", salida);
        return;
    }
    fputs("{", salida);
    json_par(salida, "ip", d->ip, true);
    json_par(salida, "mac", d->mac, true);
    json_par(salida, "hostname", d->hostname, false);
    fputs("}", salida);
}

static void exportar_json(FILE *salida, const struct ContextoInforme *ctx,
                          const struct Resultado *r)
{
    char fecha[32];
    informe_fecha_utc(fecha);

    fputs("{\n  ", salida);
    json_par(salida, "herramienta", "ShadowAsset", true);
    json_par(salida, "version", SHADOWASSET_VERSION, true);
    fputs("\n  ", salida);
    json_par(salida, "generado_utc", fecha, true);
    json_par(salida, "comando", ctx->comando, true);
    fputs("\n  ", salida);
    json_par(salida, "ip_critica", ctx->ip_critica, true);
    json_texto(salida, "requiere_revision");
    fprintf(salida, ": %s,\n", resultado_requiere_revision(r) ? "true" : "false");

    fputs("  \"entradas\": [\n", salida);
    for (int i = 0; i < ctx->n_entradas; i++) {
        const struct Entrada *e = &ctx->entradas[i];
        fputs("    {", salida);
        json_par(salida, "rol", e->rol, true);
        json_par(salida, "ruta", e->ruta, true);
        json_par(salida, "sha256", e->sha256, true);
        fprintf(salida, "\"dispositivos\": %zu}%s\n", e->dispositivos,
                i + 1 < ctx->n_entradas ? "," : "");
    }
    fputs("  ],\n", salida);

    fputs("  \"resumen\": {", salida);
    for (int c = 0; c < CAT_TOTAL; c++) {
        json_texto(salida, categoria_nombre(r->modo, (enum Categoria)c));
        fprintf(salida, ": %zu%s", r->por_categoria[c], c + 1 < CAT_TOTAL ? ", " : "");
    }
    fputs("},\n", salida);

    fputs("  \"hallazgos\": [\n", salida);
    for (size_t i = 0; i < r->cantidad; i++) {
        const struct Hallazgo *h = &r->hallazgos[i];
        char detalle[INFORME_MAX_DETALLE];
        informe_describir(r, h, detalle);
        fputs("    {", salida);
        json_par(salida, "estado", categoria_nombre(r->modo, h->categoria), true);
        json_dispositivo(salida, "referencia", h->referencia);
        fputs(", ", salida);
        json_dispositivo(salida, "observado", h->observado);
        fputs(", ", salida);
        json_par(salida, "detalle", detalle, false);
        fprintf(salida, "}%s\n", i + 1 < r->cantidad ? "," : "");
    }
    fputs("  ],\n", salida);

    fputs("  \"alertas\": [\n", salida);
    for (size_t i = 0; i < r->alertas.cantidad; i++) {
        const struct Alerta *a = &r->alertas.items[i];
        fputs("    {", salida);
        json_par(salida, "tipo", alerta_nombre(a->tipo), true);
        json_par(salida, "severidad", severidad_nombre(a->severidad), true);
        json_par(salida, "ip", a->ip, true);
        json_par(salida, "mac", a->mac, true);
        json_par(salida, "mensaje", a->mensaje, false);
        fprintf(salida, "}%s\n", i + 1 < r->alertas.cantidad ? "," : "");
    }
    fputs("  ]\n}\n", salida);
}

static bool termina_en(const char *texto, const char *sufijo)
{
    size_t lt = strlen(texto);
    size_t ls = strlen(sufijo);
    return lt >= ls && texto_igual_sin_mayusculas(texto + lt - ls, sufijo);
}

bool informe_exportar(const char *ruta, const struct ContextoInforme *ctx,
                      const struct Resultado *r)
{
    FILE *salida = csv_crear_archivo_nuevo(ruta);
    if (salida == NULL) {
        return false;
    }

    if (termina_en(ruta, ".json")) {
        exportar_json(salida, ctx, r);
    } else {
        exportar_csv(salida, ctx, r);
    }

    bool ok = !ferror(salida);
    if (fclose(salida) != 0) {
        ok = false;
    }
    if (!ok) {
        registro_mensaje("Error al escribir el informe '%s'\n", ruta);
    } else {
        printf("\nInforme guardado en: %s\n", ruta);
    }
    return ok;
}
