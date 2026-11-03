#include "analisis.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- Lista de alertas ---------- */

static bool agregar_alerta(struct ListaAlertas *a, enum TipoAlerta tipo,
                           enum Severidad severidad, const struct Dispositivo *d,
                           const char *mensaje)
{
    if (a->cantidad == a->capacidad) {
        size_t nueva = a->capacidad == 0 ? 8 : a->capacidad * 2;
        struct Alerta *items = realloc(a->items, nueva * sizeof(*items));
        if (items == NULL) {
            return false;
        }
        a->items = items;
        a->capacidad = nueva;
    }
    struct Alerta *alerta = &a->items[a->cantidad++];
    alerta->tipo = tipo;
    alerta->severidad = severidad;
    memcpy(alerta->ip, d->ip, MAX_IP);
    memcpy(alerta->mac, d->mac, MAX_MAC);
    snprintf(alerta->mensaje, MAX_MENSAJE, "%s", mensaje);
    return true;
}

static int comparar_alertas(const void *a, const void *b)
{
    const struct Alerta *x = a;
    const struct Alerta *y = b;
    if (x->severidad != y->severidad) {
        return x->severidad > y->severidad ? -1 : 1; /* más grave primero */
    }
    if (x->tipo != y->tipo) {
        return x->tipo < y->tipo ? -1 : 1;
    }
    int c = strcmp(x->ip, y->ip);
    return c != 0 ? c : strcmp(x->mac, y->mac);
}

static void ordenar_alertas(struct ListaAlertas *a)
{
    if (a->cantidad > 1) {
        qsort(a->items, a->cantidad, sizeof(*a->items), comparar_alertas);
    }
}

void alertas_liberar(struct ListaAlertas *alertas)
{
    free(alertas->items);
    alertas->items = NULL;
    alertas->cantidad = 0;
    alertas->capacidad = 0;
}

/* ---------- Índices auxiliares (punteros ordenados) ---------- */

static int comparar_ptr_ip(const void *a, const void *b)
{
    const struct Dispositivo *x = *(const struct Dispositivo *const *)a;
    const struct Dispositivo *y = *(const struct Dispositivo *const *)b;
    uint32_t nx = 0;
    uint32_t ny = 0;
    ip_a_numero(x->ip, &nx);
    ip_a_numero(y->ip, &ny);
    if (nx != ny) {
        return nx < ny ? -1 : 1;
    }
    return strcmp(x->mac, y->mac);
}

static const struct Dispositivo **indice_por_ip(const struct ListaDispositivos *lista)
{
    size_t n = lista->cantidad > 0 ? lista->cantidad : 1;
    const struct Dispositivo **indice = malloc(n * sizeof(*indice));
    if (indice == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < lista->cantidad; i++) {
        indice[i] = &lista->items[i];
    }
    if (lista->cantidad > 1) {
        qsort(indice, lista->cantidad, sizeof(*indice), comparar_ptr_ip);
    }
    return indice;
}

/* Posición del primer elemento del índice con esa IP, o n si no existe. */
static size_t buscar_ip(const struct Dispositivo **indice, size_t n, const char *ip)
{
    uint32_t buscada = 0;
    ip_a_numero(ip, &buscada);
    size_t bajo = 0;
    size_t alto = n;
    while (bajo < alto) {
        size_t medio = bajo + (alto - bajo) / 2;
        uint32_t valor = 0;
        ip_a_numero(indice[medio]->ip, &valor);
        if (valor < buscada) {
            bajo = medio + 1;
        } else {
            alto = medio;
        }
    }
    if (bajo < n && strcmp(indice[bajo]->ip, ip) == 0) {
        return bajo;
    }
    return n;
}

/* ---------- Alertas de una sola medición ---------- */

static bool alertas_ip_duplicada(const struct ListaDispositivos *lista,
                                 struct ListaAlertas *alertas)
{
    const struct Dispositivo **indice = indice_por_ip(lista);
    if (indice == NULL) {
        return false;
    }
    bool ok = true;
    for (size_t i = 1; i < lista->cantidad && ok; i++) {
        if (strcmp(indice[i - 1]->ip, indice[i]->ip) == 0) {
            char mensaje[MAX_MENSAJE];
            snprintf(mensaje, sizeof(mensaje),
                     "La IP %s aparece con dos MAC (%s y %s): conflicto de IP o posible "
                     "suplantacion ARP. Verificar cual es legitima.",
                     indice[i]->ip, indice[i - 1]->mac, indice[i]->mac);
            ok = agregar_alerta(alertas, ALERTA_IP_DUPLICADA, SEV_ALTA, indice[i], mensaje);
        }
    }
    free(indice);
    return ok;
}

static bool alerta_mac_local(const struct Dispositivo *d, struct ListaAlertas *alertas)
{
    if (!mac_es_local(d->mac)) {
        return true;
    }
    return agregar_alerta(alertas, ALERTA_MAC_LOCAL, SEV_INFO, d,
                          "MAC administrada localmente: suele ser aleatoria (privacidad en "
                          "moviles) o de una maquina virtual; puede cambiar con el tiempo.");
}

bool alertas_de_lista(const struct ListaDispositivos *lista, struct ListaAlertas *alertas)
{
    memset(alertas, 0, sizeof(*alertas));
    bool ok = alertas_ip_duplicada(lista, alertas);
    for (size_t i = 0; i < lista->cantidad && ok; i++) {
        ok = alerta_mac_local(&lista->items[i], alertas);
    }
    if (!ok) {
        alertas_liberar(alertas);
        return false;
    }
    ordenar_alertas(alertas);
    return true;
}

/* ---------- Comparación de dos listas ---------- */

static bool atributos_iguales(const struct Dispositivo *a, const struct Dispositivo *b)
{
    return strcmp(a->ip, b->ip) == 0 && texto_igual_sin_mayusculas(a->hostname, b->hostname);
}

static void agregar_hallazgo(struct Resultado *r, enum Categoria categoria,
                             const struct Dispositivo *referencia,
                             const struct Dispositivo *observado)
{
    struct Hallazgo *h = &r->hallazgos[r->cantidad++];
    h->categoria = categoria;
    h->referencia = referencia;
    h->observado = observado;
    r->por_categoria[categoria]++;
}

static int comparar_hallazgos(const void *a, const void *b)
{
    const struct Hallazgo *ha = a;
    const struct Hallazgo *hb = b;
    if (ha->categoria != hb->categoria) {
        return ha->categoria < hb->categoria ? -1 : 1;
    }
    const struct Dispositivo *da = ha->observado ? ha->observado : ha->referencia;
    const struct Dispositivo *db = hb->observado ? hb->observado : hb->referencia;
    return strcmp(da->mac, db->mac);
}

/* Una IP que en la referencia tenía la MAC X ahora la usa la MAC Y. */
static bool alertas_ip_reasignada(const struct Resultado *r,
                                  const struct ListaDispositivos *referencia,
                                  const struct ListaDispositivos *observado,
                                  const char *ip_critica, struct ListaAlertas *alertas)
{
    const struct Dispositivo **indice = indice_por_ip(referencia);
    if (indice == NULL) {
        return false;
    }
    bool ok = true;
    for (size_t i = 0; i < observado->cantidad && ok; i++) {
        const struct Dispositivo *obs = &observado->items[i];
        size_t n = referencia->cantidad;
        size_t pos = buscar_ip(indice, n, obs->ip);
        if (pos == n) {
            continue;
        }
        /* Si alguna fila con esa IP tiene esta misma MAC, no hay alerta. */
        bool coincide = false;
        for (size_t k = pos; k < n && strcmp(indice[k]->ip, obs->ip) == 0; k++) {
            if (strcmp(indice[k]->mac, obs->mac) == 0) {
                coincide = true;
            }
        }
        if (coincide) {
            continue;
        }
        const struct Dispositivo *ref = indice[pos];
        bool critica = ip_critica != NULL && strcmp(ip_critica, obs->ip) == 0;
        char mensaje[MAX_MENSAJE];
        snprintf(mensaje, sizeof(mensaje),
                 "%sLa IP %s %s la MAC %s y ahora responde con %s: cambio de hardware, "
                 "DHCP o posible suplantacion ARP.",
                 critica ? "PUERTA DE ENLACE. " : "", obs->ip,
                 r->modo == MODO_CAMBIOS ? "tenia" : "esta asignada en el inventario a",
                 ref->mac, obs->mac);
        ok = agregar_alerta(alertas, ALERTA_IP_REASIGNADA, critica ? SEV_ALTA : SEV_MEDIA,
                            obs, mensaje);
    }
    free(indice);
    return ok;
}

/* Un equipo "nuevo" con el mismo nombre que uno "no observado". */
static bool alertas_mismo_equipo(const struct Resultado *r, struct ListaAlertas *alertas)
{
    for (size_t i = 0; i < r->cantidad; i++) {
        const struct Hallazgo *nuevo = &r->hallazgos[i];
        if (nuevo->categoria != CAT_SOLO_OBSERVADO || nuevo->observado->hostname[0] == '\0') {
            continue;
        }
        for (size_t j = 0; j < r->cantidad; j++) {
            const struct Hallazgo *ausente = &r->hallazgos[j];
            if (ausente->categoria == CAT_SOLO_REFERENCIA &&
                texto_igual_sin_mayusculas(ausente->referencia->hostname,
                                           nuevo->observado->hostname)) {
                char mensaje[MAX_MENSAJE];
                snprintf(mensaje, sizeof(mensaje),
                         "'%s' aparece con la MAC %s y antes con %s: probablemente el mismo "
                         "equipo con MAC aleatoria o tarjeta cambiada. Confirmar.",
                         nuevo->observado->hostname, nuevo->observado->mac,
                         ausente->referencia->mac);
                if (!agregar_alerta(alertas, ALERTA_POSIBLE_MISMO_EQUIPO, SEV_BAJA,
                                    nuevo->observado, mensaje)) {
                    return false;
                }
                break;
            }
        }
    }
    return true;
}

bool analizar(enum Modo modo,
              const struct ListaDispositivos *referencia,
              const struct ListaDispositivos *observado,
              const char *ip_critica,
              struct Resultado *resultado)
{
    memset(resultado, 0, sizeof(*resultado));
    resultado->modo = modo;

    size_t maximo = referencia->cantidad + observado->cantidad;
    resultado->hallazgos = calloc(maximo > 0 ? maximo : 1, sizeof(*resultado->hallazgos));
    if (resultado->hallazgos == NULL) {
        return false;
    }

    /* Recorrido en paralelo de dos listas ordenadas por MAC: O(n + m). */
    size_t i = 0;
    size_t j = 0;
    while (i < referencia->cantidad || j < observado->cantidad) {
        const struct Dispositivo *ref = i < referencia->cantidad ? &referencia->items[i] : NULL;
        const struct Dispositivo *obs = j < observado->cantidad ? &observado->items[j] : NULL;
        int orden = (ref == NULL) ? 1 : (obs == NULL) ? -1 : strcmp(ref->mac, obs->mac);

        if (orden < 0) {
            agregar_hallazgo(resultado, CAT_SOLO_REFERENCIA, ref, NULL);
            i++;
        } else if (orden > 0) {
            agregar_hallazgo(resultado, CAT_SOLO_OBSERVADO, NULL, obs);
            j++;
        } else {
            agregar_hallazgo(resultado, atributos_iguales(ref, obs) ? CAT_IGUAL : CAT_CAMBIO,
                             ref, obs);
            i++;
            j++;
        }
    }

    if (resultado->cantidad > 1) {
        qsort(resultado->hallazgos, resultado->cantidad,
              sizeof(*resultado->hallazgos), comparar_hallazgos);
    }

    struct ListaAlertas *alertas = &resultado->alertas;
    bool ok = alertas_ip_duplicada(observado, alertas) &&
              alertas_ip_reasignada(resultado, referencia, observado, ip_critica, alertas) &&
              alertas_mismo_equipo(resultado, alertas);
    /* MAC aleatoria: solo interesa en equipos que no reconocemos. */
    for (size_t k = 0; k < resultado->cantidad && ok; k++) {
        if (resultado->hallazgos[k].categoria == CAT_SOLO_OBSERVADO) {
            ok = alerta_mac_local(resultado->hallazgos[k].observado, alertas);
        }
    }
    if (!ok) {
        resultado_liberar(resultado);
        return false;
    }
    ordenar_alertas(alertas);
    return true;
}

bool alertas_requieren_revision(const struct ListaAlertas *alertas)
{
    for (size_t i = 0; i < alertas->cantidad; i++) {
        if (alertas->items[i].severidad >= SEV_MEDIA) {
            return true;
        }
    }
    return false;
}

bool resultado_requiere_revision(const struct Resultado *r)
{
    return r->cantidad != r->por_categoria[CAT_IGUAL] ||
           alertas_requieren_revision(&r->alertas);
}

void resultado_liberar(struct Resultado *resultado)
{
    free(resultado->hallazgos);
    alertas_liberar(&resultado->alertas);
    memset(resultado, 0, sizeof(*resultado));
}

const char *categoria_nombre(enum Modo modo, enum Categoria categoria)
{
    static const char *const cambios[CAT_TOTAL] = {
        "NUEVO", "NO_OBSERVADO", "CAMBIO", "PERSISTENTE"};
    static const char *const cumplimiento[CAT_TOTAL] = {
        "NO_INVENTARIADO", "NO_OBSERVADO", "DISCREPANCIA", "CONFORME"};
    return modo == MODO_CAMBIOS ? cambios[categoria] : cumplimiento[categoria];
}

const char *severidad_nombre(enum Severidad severidad)
{
    static const char *const nombres[] = {"INFO", "BAJA", "MEDIA", "ALTA"};
    return nombres[severidad];
}

const char *alerta_nombre(enum TipoAlerta tipo)
{
    static const char *const nombres[] = {
        "IP_DUPLICADA", "IP_REASIGNADA", "POSIBLE_MISMO_EQUIPO", "MAC_ALEATORIA"};
    return nombres[tipo];
}
