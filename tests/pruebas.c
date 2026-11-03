/* Pruebas unitarias de ShadowAsset (sin dependencias externas).
 * Se compilan con todos los módulos de src/ salvo main.c.
 * Algunas pruebas provocan errores a propósito: los mensajes que aparecen
 * en stderr durante la ejecución son esperados. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/analisis.h"
#include "../src/csv.h"
#include "../src/dispositivo.h"
#include "../src/historial.h"
#include "../src/sha256.h"

static int total = 0;
static int fallos = 0;

#define COMPROBAR(cond)                                                          \
    do {                                                                         \
        total++;                                                                 \
        if (!(cond)) {                                                           \
            fallos++;                                                            \
            printf("  FALLO %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
        }                                                                        \
    } while (0)

#define COMPROBAR_TEXTO(a, b) COMPROBAR(strcmp((a), (b)) == 0)

/* ---------- Validación ---------- */

static void prueba_validar_ip(void)
{
    char ip[MAX_IP];
    COMPROBAR(validar_ip("192.168.1.10", ip));
    COMPROBAR_TEXTO(ip, "192.168.1.10");
    COMPROBAR(validar_ip("0.0.0.0", ip));
    COMPROBAR(validar_ip("255.255.255.255", ip));

    const char *invalidas[] = {"", "1.2.3", "1.2.3.4.5", "256.1.1.1", "01.2.3.4",
                               "1.2.3.4 ", " 1.2.3.4", "1..3.4", "a.b.c.d", "1.2.3.-4",
                               "1234.1.1.1", "=1+1", "1.2.3.4/24"};
    for (size_t i = 0; i < sizeof(invalidas) / sizeof(invalidas[0]); i++) {
        COMPROBAR(!validar_ip(invalidas[i], ip));
    }
}

static void prueba_validar_mac(void)
{
    char mac[MAX_MAC];
    COMPROBAR(validar_mac("aa-bb-cc-dd-ee-ff", mac));
    COMPROBAR_TEXTO(mac, "AA:BB:CC:DD:EE:FF");
    COMPROBAR(validar_mac("00:00:5e:00:53:01", mac));
    COMPROBAR_TEXTO(mac, "00:00:5E:00:53:01");

    COMPROBAR(!validar_mac("AA:BB-CC:DD:EE:FF", mac)); /* separadores mezclados */
    COMPROBAR(!validar_mac("AA:BB:CC:DD:EE", mac));
    COMPROBAR(!validar_mac("AA:BB:CC:DD:EE:FG", mac));
    COMPROBAR(!validar_mac("AABBCCDDEEFF", mac));
    COMPROBAR(!validar_mac("AA.BB.CC.DD.EE.FF", mac));
}

static void prueba_validar_hostname(void)
{
    char host[MAX_HOST];
    COMPROBAR(validar_hostname("PC-VENTAS_01.local", host));
    COMPROBAR(validar_hostname("", host));
    COMPROBAR(!validar_hostname("-cmd", host));
    COMPROBAR(!validar_hostname(".oculto", host));
    COMPROBAR(!validar_hostname("con espacio", host));
    COMPROBAR(!validar_hostname("=HYPERLINK(\"x\")", host));
    COMPROBAR(!validar_hostname("a;b", host));

    char largo[MAX_HOST + 1];
    memset(largo, 'a', MAX_HOST);
    largo[MAX_HOST] = '\0';
    COMPROBAR(!validar_hostname(largo, host));
    largo[MAX_HOST - 1] = '\0';
    COMPROBAR(validar_hostname(largo, host));
}

static void prueba_mac_local_y_subred(void)
{
    COMPROBAR(!mac_es_local("00:00:5E:00:53:01"));
    COMPROBAR(mac_es_local("02:00:00:00:00:01"));
    COMPROBAR(mac_es_local("DA:A1:19:00:00:01")); /* típica MAC aleatoria */
    COMPROBAR(!mac_es_local("08:33:ED:93:D8:C0"));

    struct Subred s;
    COMPROBAR(subred_parsear("192.168.1.0/24", &s));
    COMPROBAR(subred_contiene(&s, "192.168.1.200"));
    COMPROBAR(!subred_contiene(&s, "192.168.2.1"));
    COMPROBAR(subred_parsear("0.0.0.0/0", &s));
    COMPROBAR(subred_contiene(&s, "8.8.8.8"));
    COMPROBAR(subred_parsear("10.0.0.7/32", &s));
    COMPROBAR(subred_contiene(&s, "10.0.0.7"));
    COMPROBAR(!subred_contiene(&s, "10.0.0.8"));

    COMPROBAR(!subred_parsear("192.168.1.5/24", &s)); /* bits de host activos */
    COMPROBAR(!subred_parsear("192.168.1.0/33", &s));
    COMPROBAR(!subred_parsear("192.168.1.0", &s));
    COMPROBAR(!subred_parsear("/24", &s));
    COMPROBAR(!subred_parsear("192.168.1.0/", &s));
    COMPROBAR(!subred_parsear("192.168.1.0/2x", &s));
}

/* ---------- CSV ---------- */

static void prueba_dividir_linea(void)
{
    char *campos[8];
    char l1[] = "a, b ,c";
    COMPROBAR(csv_dividir_linea(l1, ',', campos, 8) == 3);
    COMPROBAR_TEXTO(campos[1], "b");

    char l2[] = "\"Ana; TI\";x;\"di \"\"hola\"\"\"";
    COMPROBAR(csv_dividir_linea(l2, ';', campos, 8) == 3);
    COMPROBAR_TEXTO(campos[0], "Ana; TI");
    COMPROBAR_TEXTO(campos[2], "di \"hola\"");

    char l3[] = "a,,";
    COMPROBAR(csv_dividir_linea(l3, ',', campos, 8) == 3);
    COMPROBAR_TEXTO(campos[2], "");

    char l4[] = "\"sin cerrar,b";
    COMPROBAR(csv_dividir_linea(l4, ',', campos, 8) == -1);
    char l5[] = "a\"b,c";
    COMPROBAR(csv_dividir_linea(l5, ',', campos, 8) == -1);
    char l6[] = "\"a\"x,c";
    COMPROBAR(csv_dividir_linea(l6, ',', campos, 8) == -1);
    char l7[] = "1,2,3";
    COMPROBAR(csv_dividir_linea(l7, ',', campos, 2) == -1); /* demasiados campos */
}

static bool parsear(const char *texto, struct ListaDispositivos *lista)
{
    size_t largo = strlen(texto);
    char *copia = malloc(largo + 1);
    if (copia == NULL) {
        return false;
    }
    memcpy(copia, texto, largo + 1);
    bool ok = csv_parsear_texto("prueba", copia, largo, lista);
    free(copia);
    return ok;
}

static void prueba_parsear_csv(void)
{
    struct ListaDispositivos l;

    COMPROBAR(parsear("ip,mac,hostname\n10.0.0.2,00:00:5E:00:53:02,B\n"
                      "10.0.0.1,00:00:5e:00:53:01,A\n", &l));
    COMPROBAR(l.cantidad == 2);
    COMPROBAR_TEXTO(l.items[0].mac, "00:00:5E:00:53:01"); /* ordenada y normalizada */
    COMPROBAR_TEXTO(l.items[0].hostname, "A");
    lista_liberar(&l);

    /* Exportado por Excel en español: BOM, ';', CRLF, columnas extra y en otro orden. */
    COMPROBAR(parsear("\xEF\xBB\xBFResponsable;MAC;IP\r\n\"Ana; TI\";00-00-5E-00-53-01;10.0.0.1\r\n"
                      "\r\n", &l));
    COMPROBAR(l.cantidad == 1);
    COMPROBAR_TEXTO(l.items[0].ip, "10.0.0.1");
    COMPROBAR_TEXTO(l.items[0].hostname, "");
    lista_liberar(&l);

    COMPROBAR(parsear("IP,MAC,HOSTNAME\n", &l)); /* solo cabecera: lista vacía válida */
    COMPROBAR(l.cantidad == 0);
    lista_liberar(&l);

    COMPROBAR(!parsear("", &l));
    COMPROBAR(!parsear("hostname\nPC\n", &l));                         /* faltan ip y mac */
    COMPROBAR(!parsear("ip,mac,ip\n", &l));                            /* columna repetida */
    COMPROBAR(!parsear("ip,mac\n10.0.0.1\n", &l));                     /* faltan campos */
    COMPROBAR(!parsear("ip,mac\n10.0.0.1,00:00:5E:00:53:01,x\n", &l)); /* sobran campos */
    COMPROBAR(!parsear("ip,mac\n10.0.0.300,00:00:5E:00:53:01\n", &l));
    COMPROBAR(!parsear("ip,mac,hostname\n10.0.0.1,00:00:5E:00:53:01,=cmd\n", &l));
    COMPROBAR(!parsear("ip,mac\n10.0.0.1,00:00:5E:00:53:01\n10.0.0.2,00-00-5e-00-53-01\n", &l));
    COMPROBAR(!parsear("\xFF\xFEi\0p\0", &l)); /* UTF-16 */

    /* Bytes nulos dentro del archivo. */
    char con_nulo[] = "ip,mac\n10.0.0.1,00:00:5E:00:53:01\n";
    con_nulo[3] = '\0';
    COMPROBAR(!csv_parsear_texto("prueba", con_nulo, sizeof(con_nulo) - 1, &l));

    /* Línea demasiado larga. */
    size_t n = CSV_MAX_LINEA + 64;
    char *larga = malloc(n + 32);
    if (larga != NULL) {
        strcpy(larga, "ip,mac,");
        memset(larga + 7, 'x', n);
        larga[7 + n] = '\0';
        COMPROBAR(!csv_parsear_texto("prueba", larga, strlen(larga), &l));
        free(larga);
    }
}

static void prueba_campo_seguro(void)
{
    const char *ruta = "tests/salida_prueba.tmp";
    remove(ruta);
    FILE *f = csv_crear_archivo_nuevo(ruta);
    COMPROBAR(f != NULL);
    if (f == NULL) {
        return;
    }
    csv_escribir_campo_seguro(f, "=1+1");
    fputc('|', f);
    csv_escribir_campo_seguro(f, "-2");
    fputc('|', f);
    csv_escribir_campo_seguro(f, "@SUM(A1)");
    fputc('|', f);
    csv_escribir_campo_seguro(f, "di \"hola\"");
    fputc('|', f);
    csv_escribir_campo_seguro(f, "normal");
    fclose(f);

    COMPROBAR(csv_crear_archivo_nuevo(ruta) == NULL); /* no sobrescribe */

    char leido[256] = "";
    f = fopen(ruta, "rb");
    if (f != NULL) {
        size_t n = fread(leido, 1, sizeof(leido) - 1, f);
        leido[n] = '\0';
        fclose(f);
    }
    remove(ruta);
    COMPROBAR_TEXTO(leido, "\"'=1+1\"|\"'-2\"|\"'@SUM(A1)\"|\"di \"\"hola\"\"\"|\"normal\"");
}

/* ---------- SHA-256 (vectores de FIPS 180-4) ---------- */

static void prueba_sha256(void)
{
    char hex[SHA256_HEX];
    sha256_texto("", 0, hex);
    COMPROBAR_TEXTO(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    sha256_texto("abc", 3, hex);
    COMPROBAR_TEXTO(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const char *dos_bloques = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    sha256_texto(dos_bloques, strlen(dos_bloques), hex);
    COMPROBAR_TEXTO(hex, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    /* Un millón de 'a', en trozos de distinto tamaño. */
    struct Sha256 ctx;
    char bloque[1001];
    memset(bloque, 'a', sizeof(bloque));
    sha256_iniciar(&ctx);
    for (int i = 0; i < 1000; i++) {
        sha256_actualizar(&ctx, bloque, i % 2 == 0 ? 999 : 1001);
    }
    sha256_finalizar(&ctx, hex);
    COMPROBAR_TEXTO(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

/* ---------- Análisis ---------- */

static size_t contar_alertas(const struct Resultado *r, enum TipoAlerta tipo,
                             enum Severidad *severidad)
{
    size_t n = 0;
    for (size_t i = 0; i < r->alertas.cantidad; i++) {
        if (r->alertas.items[i].tipo == tipo) {
            n++;
            if (severidad != NULL) {
                *severidad = r->alertas.items[i].severidad;
            }
        }
    }
    return n;
}

static void prueba_analisis_categorias(void)
{
    struct ListaDispositivos ant;
    struct ListaDispositivos act;
    COMPROBAR(parsear("ip,mac,hostname\n"
                      "10.0.0.1,00:00:5E:00:53:01,ROUTER\n"
                      "10.0.0.10,00:00:5E:00:53:10,PC-A\n"
                      "10.0.0.20,00:00:5E:00:53:20,PC-B\n"
                      "10.0.0.30,00:00:5E:00:53:30,IMPRESORA\n", &ant));
    COMPROBAR(parsear("ip,mac,hostname\n"
                      "10.0.0.1,00:00:5E:00:53:01,router\n" /* mayúsculas: igual */
                      "10.0.0.10,00:00:5E:00:53:10,PC-A2\n" /* cambia nombre */
                      "10.0.0.21,00:00:5E:00:53:20,PC-B\n"  /* cambia IP */
                      "10.0.0.40,00:00:5E:00:53:40,NUEVO\n", &act));

    struct Resultado r;
    COMPROBAR(analizar(MODO_CAMBIOS, &ant, &act, NULL, &r));
    COMPROBAR(r.cantidad == 5);
    COMPROBAR(r.por_categoria[CAT_SOLO_OBSERVADO] == 1);
    COMPROBAR(r.por_categoria[CAT_SOLO_REFERENCIA] == 1);
    COMPROBAR(r.por_categoria[CAT_CAMBIO] == 2);
    COMPROBAR(r.por_categoria[CAT_IGUAL] == 1);
    COMPROBAR(r.alertas.cantidad == 0);
    COMPROBAR(resultado_requiere_revision(&r));
    COMPROBAR_TEXTO(categoria_nombre(r.modo, r.hallazgos[0].categoria), "NUEVO");
    resultado_liberar(&r);

    /* Mismas listas: nada que revisar. */
    COMPROBAR(analizar(MODO_CUMPLIMIENTO, &ant, &ant, NULL, &r));
    COMPROBAR(r.por_categoria[CAT_IGUAL] == 4);
    COMPROBAR(!resultado_requiere_revision(&r));
    COMPROBAR_TEXTO(categoria_nombre(r.modo, CAT_IGUAL), "CONFORME");
    resultado_liberar(&r);

    /* Listas vacías. */
    struct ListaDispositivos vacia;
    lista_iniciar(&vacia);
    COMPROBAR(analizar(MODO_CAMBIOS, &vacia, &vacia, NULL, &r));
    COMPROBAR(r.cantidad == 0 && !resultado_requiere_revision(&r));
    resultado_liberar(&r);

    lista_liberar(&ant);
    lista_liberar(&act);
}

static void prueba_analisis_alertas(void)
{
    struct ListaDispositivos ant;
    struct ListaDispositivos act;
    COMPROBAR(parsear("ip,mac,hostname\n"
                      "10.0.0.1,00:00:5E:00:53:01,ROUTER\n"
                      "10.0.0.5,00:00:5E:00:53:05,SERVIDOR\n"
                      "10.0.0.9,00:00:5E:00:53:09,MOVIL\n", &ant));
    COMPROBAR(parsear("ip,mac,hostname\n"
                      "10.0.0.1,00:00:5E:00:53:66,ROUTER\n"   /* gateway con otra MAC */
                      "10.0.0.5,00:00:5E:00:53:05,SERVIDOR\n"
                      "10.0.0.5,00:00:5E:00:53:55,\n"         /* IP duplicada */
                      "10.0.0.77,DA:A1:19:00:00:09,MOVIL\n", &act)); /* MAC aleatoria */

    struct Resultado r;
    COMPROBAR(analizar(MODO_CAMBIOS, &ant, &act, "10.0.0.1", &r));
    enum Severidad sev = SEV_INFO;
    COMPROBAR(contar_alertas(&r, ALERTA_IP_DUPLICADA, &sev) == 1 && sev == SEV_ALTA);
    COMPROBAR(contar_alertas(&r, ALERTA_IP_REASIGNADA, NULL) == 2);
    COMPROBAR(contar_alertas(&r, ALERTA_POSIBLE_MISMO_EQUIPO, NULL) == 2); /* ROUTER y MOVIL */
    COMPROBAR(contar_alertas(&r, ALERTA_MAC_LOCAL, NULL) == 1);
    /* La más grave va primero y es la de la puerta de enlace o la IP duplicada. */
    COMPROBAR(r.alertas.items[0].severidad == SEV_ALTA);
    resultado_liberar(&r);

    /* Sin --gateway, el cambio de MAC del router es MEDIA, no ALTA. */
    COMPROBAR(analizar(MODO_CAMBIOS, &ant, &act, NULL, &r));
    size_t altas = 0;
    for (size_t i = 0; i < r.alertas.cantidad; i++) {
        if (r.alertas.items[i].tipo == ALERTA_IP_REASIGNADA &&
            r.alertas.items[i].severidad == SEV_ALTA) {
            altas++;
        }
    }
    COMPROBAR(altas == 0);
    resultado_liberar(&r);

    /* Inventario con la misma IP en dos filas: si una coincide, no hay alerta. */
    struct ListaDispositivos inv;
    struct ListaDispositivos obs;
    COMPROBAR(parsear("ip,mac\n10.0.0.5,00:00:5E:00:53:05\n10.0.0.5,00:00:5E:00:53:06\n", &inv));
    COMPROBAR(parsear("ip,mac\n10.0.0.5,00:00:5E:00:53:06\n", &obs));
    COMPROBAR(analizar(MODO_CUMPLIMIENTO, &inv, &obs, NULL, &r));
    COMPROBAR(contar_alertas(&r, ALERTA_IP_REASIGNADA, NULL) == 0);
    resultado_liberar(&r);

    struct ListaAlertas sueltas;
    COMPROBAR(alertas_de_lista(&act, &sueltas));
    COMPROBAR(sueltas.cantidad == 2); /* IP duplicada + MAC aleatoria */
    COMPROBAR(alertas_requieren_revision(&sueltas));
    alertas_liberar(&sueltas);

    lista_liberar(&inv);
    lista_liberar(&obs);
    lista_liberar(&ant);
    lista_liberar(&act);
}

static void prueba_historial(void)
{
    COMPROBAR(historial_es_nombre_valido("snapshot_20261102_214033.csv"));
    COMPROBAR(!historial_es_nombre_valido("snapshot_20261102-214033.csv"));
    COMPROBAR(!historial_es_nombre_valido("snapshot_2026110_214033.csv"));
    COMPROBAR(!historial_es_nombre_valido("otro_20261102_214033.csv"));
    COMPROBAR(!historial_es_nombre_valido("snapshot_20261102_214033.csv.bak"));

    char ruta[HISTORIAL_MAX_RUTA];
    COMPROBAR(historial_ruta_nueva("hist/", ruta));
    COMPROBAR(strncmp(ruta, "hist/snapshot_", 14) == 0);
    COMPROBAR(historial_es_nombre_valido(ruta + 5));
}

int main(void)
{
    struct {
        const char *nombre;
        void (*funcion)(void);
    } pruebas[] = {
        {"validar_ip", prueba_validar_ip},
        {"validar_mac", prueba_validar_mac},
        {"validar_hostname", prueba_validar_hostname},
        {"mac_local_y_subred", prueba_mac_local_y_subred},
        {"csv_dividir_linea", prueba_dividir_linea},
        {"csv_parsear", prueba_parsear_csv},
        {"csv_campo_seguro", prueba_campo_seguro},
        {"sha256", prueba_sha256},
        {"analisis_categorias", prueba_analisis_categorias},
        {"analisis_alertas", prueba_analisis_alertas},
        {"historial", prueba_historial},
    };

    for (size_t i = 0; i < sizeof(pruebas) / sizeof(pruebas[0]); i++) {
        int antes = fallos;
        pruebas[i].funcion();
        printf("[%s] %s\n", fallos == antes ? " OK " : "FALLO", pruebas[i].nombre);
        fflush(stdout);
    }

    printf("\n%d comprobaciones, %d fallo(s)\n", total, fallos);
    return fallos == 0 ? 0 : 1;
}
