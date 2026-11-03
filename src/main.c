/* ShadowAsset - Network Asset Discovery, Change Detection & Asset Compliance
 *
 * Códigos de salida (útiles en tareas programadas y scripts):
 *   0  sin novedades
 *   1  error de uso, de datos o de E/S
 *   2  análisis correcto, pero hay hallazgos o alertas que revisar
 */

#include <stdio.h>
#include <string.h>

#include "analisis.h"
#include "csv.h"
#include "descubrimiento.h"
#include "dispositivo.h"
#include "historial.h"
#include "informe.h"

enum { SALIDA_OK = 0, SALIDA_ERROR = 1, SALIDA_REVISAR = 2 };

#define MAX_POSICIONALES 3

struct Opciones {
    const char *posicionales[MAX_POSICIONALES];
    int n_posicionales;
    const char *informe;  /* --report */
    char separador;       /* --sep */
    bool hay_subred;      /* --subnet */
    struct Subred subred;
    char gateway[MAX_IP]; /* --gateway */
};

static void mostrar_uso(void)
{
    fprintf(stderr,
            "ShadowAsset v%s - auditoria de activos de red\n\n"
            "Uso:\n"
            "  shadowasset show     <archivo.csv>\n"
            "  shadowasset discover <salida.csv> [--subnet CIDR]\n"
            "  shadowasset monitor  <carpeta>    [--subnet CIDR] [--gateway IP] [--report F]\n"
            "  shadowasset compare  <anterior.csv> <actual.csv>     [opciones de informe]\n"
            "  shadowasset audit    <observado.csv> <inventario.csv> [opciones de informe]\n"
            "  shadowasset version\n\n"
            "Opciones de informe:\n"
            "  --report F      guarda el resultado en F (.json o .csv); nunca sobrescribe\n"
            "  --sep ;         separador del CSV (',' por defecto; ';' para Excel en espanol)\n"
            "  --gateway IP    IP critica: si cambia de MAC, la alerta es ALTA\n"
            "  --subnet CIDR   limita el descubrimiento a una subred, p. ej. 192.168.1.0/24\n\n"
            "Codigos de salida: 0 sin novedades, 1 error, 2 hay algo que revisar.\n",
            SHADOWASSET_VERSION);
}

static bool archivo_existe(const char *ruta)
{
    FILE *f = fopen(ruta, "rb");
    if (f != NULL) {
        fclose(f);
        return true;
    }
    return false;
}

static bool leer_opciones(int argc, char *argv[], struct Opciones *op)
{
    memset(op, 0, sizeof(*op));
    op->separador = ',';

    for (int i = 2; i < argc; i++) {
        const char *arg = argv[i];
        if (strncmp(arg, "--", 2) != 0) {
            if (op->n_posicionales == MAX_POSICIONALES) {
                fprintf(stderr, "Error: demasiados argumentos\n");
                return false;
            }
            op->posicionales[op->n_posicionales++] = arg;
            continue;
        }
        if (i + 1 >= argc) {
            fprintf(stderr, "Error: falta el valor de %s\n", arg);
            return false;
        }
        const char *valor = argv[++i];

        if (strcmp(arg, "--report") == 0) {
            op->informe = valor;
        } else if (strcmp(arg, "--sep") == 0) {
            if (strcmp(valor, ",") != 0 && strcmp(valor, ";") != 0) {
                fprintf(stderr, "Error: --sep admite ',' o ';'\n");
                return false;
            }
            op->separador = valor[0];
        } else if (strcmp(arg, "--subnet") == 0) {
            if (!subred_parsear(valor, &op->subred)) {
                fprintf(stderr, "Error: subred no valida '%s' (ejemplo: 192.168.1.0/24)\n",
                        valor);
                return false;
            }
            op->hay_subred = true;
        } else if (strcmp(arg, "--gateway") == 0) {
            if (!validar_ip(valor, op->gateway)) {
                fprintf(stderr, "Error: IP de --gateway no valida\n");
                return false;
            }
        } else {
            fprintf(stderr, "Error: opcion desconocida '%s'\n", arg);
            return false;
        }
    }

    /* Comprobarlo antes de analizar evita trabajo inútil. */
    if (op->informe != NULL && archivo_existe(op->informe)) {
        fprintf(stderr, "Error: '%s' ya existe; elija otro nombre\n", op->informe);
        return false;
    }
    return true;
}

/* ---------- Comandos ---------- */

static int comando_show(const char *ruta)
{
    struct ListaDispositivos lista;
    char sha[SHA256_HEX];
    if (!csv_cargar(ruta, &lista, sha)) {
        return SALIDA_ERROR;
    }
    printf("%s  (sha256 %s)\n\n", ruta, sha);
    informe_mostrar_lista(&lista);

    int codigo = SALIDA_OK;
    struct ListaAlertas alertas;
    if (!alertas_de_lista(&lista, &alertas)) {
        fprintf(stderr, "Error: memoria insuficiente\n");
        codigo = SALIDA_ERROR;
    } else {
        informe_mostrar_alertas(&alertas);
        codigo = alertas_requieren_revision(&alertas) ? SALIDA_REVISAR : SALIDA_OK;
        alertas_liberar(&alertas);
    }
    lista_liberar(&lista);
    return codigo;
}

/* Carga dos archivos, los compara y muestra/exporta el resultado. */
static int analizar_archivos(enum Modo modo, struct ContextoInforme *ctx,
                             const struct Opciones *op)
{
    struct Entrada *ref = &ctx->entradas[0];
    struct Entrada *obs = &ctx->entradas[1];
    struct ListaDispositivos referencia;
    struct ListaDispositivos observado;

    if (!csv_cargar(ref->ruta, &referencia, ref->sha256)) {
        return SALIDA_ERROR;
    }
    if (!csv_cargar(obs->ruta, &observado, obs->sha256)) {
        lista_liberar(&referencia);
        return SALIDA_ERROR;
    }
    ref->dispositivos = referencia.cantidad;
    obs->dispositivos = observado.cantidad;

    int codigo;
    struct Resultado resultado;
    if (!analizar(modo, &referencia, &observado, ctx->ip_critica, &resultado)) {
        fprintf(stderr, "Error: memoria insuficiente\n");
        codigo = SALIDA_ERROR;
    } else {
        printf("%s\n\n", modo == MODO_CAMBIOS ? "== Deteccion de cambios =="
                                              : "== Cumplimiento frente al inventario ==");
        informe_mostrar_entradas(ctx);
        informe_mostrar_resultado(&resultado);
        codigo = resultado_requiere_revision(&resultado) ? SALIDA_REVISAR : SALIDA_OK;
        if (op->informe != NULL && !informe_exportar(op->informe, ctx, &resultado)) {
            codigo = SALIDA_ERROR;
        }
        resultado_liberar(&resultado);
    }

    lista_liberar(&observado);
    lista_liberar(&referencia);
    return codigo;
}

static void preparar_contexto(struct ContextoInforme *ctx, const char *comando,
                              const struct Opciones *op)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->comando = comando;
    ctx->separador = op->separador;
    ctx->ip_critica = op->gateway[0] != '\0' ? op->gateway : NULL;
    ctx->n_entradas = 2;
}

static int comando_compare(const struct Opciones *op)
{
    struct ContextoInforme ctx;
    preparar_contexto(&ctx, "compare", op);
    ctx.entradas[0].rol = "anterior";
    ctx.entradas[0].ruta = op->posicionales[0];
    ctx.entradas[1].rol = "actual";
    ctx.entradas[1].ruta = op->posicionales[1];
    return analizar_archivos(MODO_CAMBIOS, &ctx, op);
}

static int comando_audit(const struct Opciones *op)
{
    struct ContextoInforme ctx;
    preparar_contexto(&ctx, "audit", op);
    /* audit <observado> <inventario>: la referencia es el inventario. */
    ctx.entradas[0].rol = "inventario";
    ctx.entradas[0].ruta = op->posicionales[1];
    ctx.entradas[1].rol = "observado";
    ctx.entradas[1].ruta = op->posicionales[0];
    return analizar_archivos(MODO_CUMPLIMIENTO, &ctx, op);
}

static void mostrar_alcance(const struct Opciones *op)
{
    if (op->hay_subred) {
        uint32_t r = op->subred.red;
        printf("Alcance: %u.%u.%u.%u/%d\n", (unsigned)(r >> 24), (unsigned)(r >> 16) & 255u,
               (unsigned)(r >> 8) & 255u, (unsigned)r & 255u, op->subred.prefijo);
    } else {
        printf("Alcance: todas las interfaces (use --subnet para limitarlo)\n");
    }
}

/* Lee la caché ARP, la muestra y la guarda en "ruta". */
static int descubrir_y_guardar(const struct Opciones *op, const char *ruta)
{
    struct ListaDispositivos lista;
    if (!descubrir_vecinos(op->hay_subred ? &op->subred : NULL, &lista)) {
        return SALIDA_ERROR;
    }
    printf("Vecinos IPv4 en la cache ARP local (lectura pasiva, sin enviar paquetes)\n");
    mostrar_alcance(op);
    printf("\n");
    informe_mostrar_lista(&lista);

    int codigo = SALIDA_OK;
    struct ListaAlertas alertas;
    if (alertas_de_lista(&lista, &alertas)) {
        informe_mostrar_alertas(&alertas);
        if (alertas_requieren_revision(&alertas)) {
            codigo = SALIDA_REVISAR;
        }
        alertas_liberar(&alertas);
    }

    if (!csv_guardar_instantanea(ruta, &lista)) {
        codigo = SALIDA_ERROR;
    } else {
        printf("\nInstantanea guardada en: %s\n", ruta);
    }
    lista_liberar(&lista);
    return codigo;
}

static int comando_discover(const struct Opciones *op)
{
    int codigo = descubrir_y_guardar(op, op->posicionales[0]);
    printf("\nLimitaciones: solo aparecen equipos con los que este PC se comunico\n"
           "recientemente; la ausencia de un equipo no significa que este apagado.\n");
    return codigo;
}

/* discover con historial automático + compare con la instantánea anterior. */
static int comando_monitor(const struct Opciones *op_original)
{
    struct Opciones op = *op_original;
    const char *carpeta = op.posicionales[0];

    char anterior[HISTORIAL_MAX_RUTA];
    char nueva[HISTORIAL_MAX_RUTA];
    bool hay_anterior = historial_ultima(carpeta, anterior);
    if (!historial_ruta_nueva(carpeta, nueva)) {
        fprintf(stderr, "Error: ruta de carpeta demasiado larga\n");
        return SALIDA_ERROR;
    }
    if (archivo_existe(nueva)) {
        fprintf(stderr, "Error: ya existe una instantanea de este segundo; espere y repita\n");
        return SALIDA_ERROR;
    }

    if (op.gateway[0] == '\0') {
        if (descubrir_puerta_enlace(op.gateway)) {
            printf("Puerta de enlace detectada: %s (vigilada)\n", op.gateway);
        }
    }

    int codigo = descubrir_y_guardar(&op, nueva);
    if (codigo == SALIDA_ERROR) {
        return codigo;
    }

    if (!hay_anterior) {
        printf("\nPrimera instantanea en '%s': queda como linea base.\n"
               "Vuelva a ejecutar 'monitor' mas tarde para detectar cambios.\n", carpeta);
        return codigo;
    }

    printf("\n");
    struct ContextoInforme ctx;
    preparar_contexto(&ctx, "monitor", &op);
    ctx.entradas[0].rol = "anterior";
    ctx.entradas[0].ruta = anterior;
    ctx.entradas[1].rol = "actual";
    ctx.entradas[1].ruta = nueva;
    int codigo_cambios = analizar_archivos(MODO_CAMBIOS, &ctx, &op);
    return codigo_cambios != SALIDA_OK ? codigo_cambios : codigo;
}

/* ---------- Punto de entrada ---------- */

struct Comando {
    const char *nombre;
    int posicionales;
    int (*ejecutar)(const struct Opciones *op);
};

int main(int argc, char *argv[])
{
    if (argc < 2) {
        mostrar_uso();
        return SALIDA_ERROR;
    }

    const char *nombre = argv[1];
    if (strcmp(nombre, "version") == 0 || strcmp(nombre, "--version") == 0) {
        printf("ShadowAsset %s\n", SHADOWASSET_VERSION);
        return SALIDA_OK;
    }
    if (strcmp(nombre, "help") == 0 || strcmp(nombre, "--help") == 0 ||
        strcmp(nombre, "-h") == 0) {
        mostrar_uso();
        return SALIDA_OK;
    }

    /* Compatibilidad con v0.1: "shadowasset archivo.csv". */
    if (argc == 2 && strstr(nombre, ".csv") != NULL) {
        return comando_show(nombre);
    }

    static const struct Comando comandos[] = {
        {"compare", 2, comando_compare},
        {"audit", 2, comando_audit},
        {"discover", 1, comando_discover},
        {"monitor", 1, comando_monitor},
    };

    struct Opciones op;
    if (strcmp(nombre, "show") == 0) {
        if (argc != 3) {
            mostrar_uso();
            return SALIDA_ERROR;
        }
        return comando_show(argv[2]);
    }

    for (size_t i = 0; i < sizeof(comandos) / sizeof(comandos[0]); i++) {
        if (strcmp(nombre, comandos[i].nombre) != 0) {
            continue;
        }
        if (!leer_opciones(argc, argv, &op)) {
            return SALIDA_ERROR;
        }
        if (op.n_posicionales != comandos[i].posicionales) {
            mostrar_uso();
            return SALIDA_ERROR;
        }
        return comandos[i].ejecutar(&op);
    }

    fprintf(stderr, "Comando desconocido: '%s'\n\n", nombre);
    mostrar_uso();
    return SALIDA_ERROR;
}
