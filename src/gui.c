/* ShadowAsset - interfaz gráfica para Windows (API Win32 nativa).
 *
 * Reutiliza los mismos módulos que la versión de consola; aquí solo vive la
 * presentación. La interfaz usa texto UTF-16 (funciones W de Windows) y
 * convierte las rutas a la página de códigos ANSI que usa fopen().
 *
 * También acepta argumentos para abrir algo directamente:
 *   shadowasset-gui.exe discover
 *   shadowasset-gui.exe show <archivo.csv>
 *   shadowasset-gui.exe compare <anterior.csv> <actual.csv>
 *   shadowasset-gui.exe audit <observado.csv> <inventario.csv>
 */

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "analisis.h"
#include "csv.h"
#include "descubrimiento.h"
#include "dispositivo.h"
#include "historial.h"
#include "informe.h"
#include "registro.h"

/* ---------- Identificadores y estado ---------- */

enum {
    ID_ABRIR = 101,
    ID_DESCUBRIR,
    ID_COMPARAR,
    ID_AUDITAR,
    ID_MONITOR,
    ID_SUBRED,
    ID_GATEWAY,
    ID_PUNTO_Y_COMA,
    ID_GUARDAR,
    ID_EXPORTAR,
    ID_LISTA,
    ID_ALERTAS,
    ID_ESTADO
};

enum Vista { VISTA_VACIA, VISTA_LISTA, VISTA_RESULTADO };

#define ANCHO_(x) L##x
#define ANCHO(x) ANCHO_(x)
#define SHADOWASSET_VERSION_W ANCHO(SHADOWASSET_VERSION)

#define SIN_COLOR ((LPARAM)-1)
#define MAX_TEXTO 1024

static struct {
    HINSTANCE instancia;
    HWND ventana;
    HWND botones[5];
    HWND etiqueta_subred, subred, etiqueta_gateway, gateway, punto_y_coma;
    HWND titulo_lista, lista, titulo_alertas, alertas;
    HWND guardar, exportar, estado;
    HFONT fuente, fuente_negrita;
    int dpi;

    enum Vista vista;
    bool descubierta; /* la lista actual viene de la red y se puede guardar */
    struct ListaDispositivos referencia;
    struct ListaDispositivos observado;
    struct Resultado resultado;
    struct ListaAlertas alertas_lista;
    struct ContextoInforme ctx;
    char rutas[2][MAX_PATH];
    char ip_critica[MAX_IP];

    char mensajes[4096];
    size_t usado;
} app;

/* ---------- Utilidades ---------- */

static int px(int valor)
{
    return MulDiv(valor, app.dpi, 96);
}

static void a_ancho(const char *texto, wchar_t *destino, int capacidad)
{
    if (MultiByteToWideChar(CP_ACP, 0, texto, -1, destino, capacidad) == 0) {
        destino[0] = L'\0';
    }
}

static void recoger_mensaje(const char *mensaje, void *contexto)
{
    (void)contexto;
    size_t largo = strlen(mensaje);
    size_t libre = sizeof(app.mensajes) - app.usado - 1;
    if (largo > libre) {
        largo = libre;
    }
    memcpy(app.mensajes + app.usado, mensaje, largo);
    app.usado += largo;
    app.mensajes[app.usado] = '\0';
}

static void limpiar_mensajes(void)
{
    app.usado = 0;
    app.mensajes[0] = '\0';
}

static void mostrar_error(const wchar_t *titulo, const wchar_t *por_defecto)
{
    wchar_t texto[4096];
    if (app.usado > 0) {
        a_ancho(app.mensajes, texto, (int)(sizeof(texto) / sizeof(texto[0])));
    } else {
        wcsncpy(texto, por_defecto, sizeof(texto) / sizeof(texto[0]) - 1);
        texto[sizeof(texto) / sizeof(texto[0]) - 1] = L'\0';
    }
    MessageBoxW(app.ventana, texto, titulo, MB_OK | MB_ICONERROR);
    limpiar_mensajes();
}

/* Avisos que no impiden la acción (p. ej. una MAC con varias IP). */
static void mostrar_avisos(void)
{
    if (app.usado > 0) {
        wchar_t texto[4096];
        a_ancho(app.mensajes, texto, (int)(sizeof(texto) / sizeof(texto[0])));
        MessageBoxW(app.ventana, texto, L"Avisos", MB_OK | MB_ICONINFORMATION);
    }
    limpiar_mensajes();
}

static void estado(const wchar_t *texto)
{
    SendMessageW(app.estado, SB_SETTEXTW, 0, (LPARAM)texto);
}

static void cursor_espera(bool activo)
{
    SetCursor(LoadCursorW(NULL, activo ? IDC_WAIT : IDC_ARROW));
}

/* Convierte la ruta elegida a ANSI (lo que entiende fopen). Rechaza rutas
 * con caracteres que no existen en la página de códigos del sistema. */
static bool ruta_a_ansi(const wchar_t *ancha, char destino[MAX_PATH])
{
    BOOL perdida = FALSE;
    int n = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, ancha, -1, destino, MAX_PATH,
                                NULL, &perdida);
    if (n == 0 || perdida) {
        MessageBoxW(app.ventana,
                    L"La ruta contiene caracteres que esta versión no admite.\n"
                    L"Mueva el archivo a una carpeta con un nombre más simple.",
                    L"Ruta no compatible", MB_OK | MB_ICONWARNING);
        return false;
    }
    return true;
}

static bool elegir_archivo(const wchar_t *titulo, bool guardar, const wchar_t *filtro,
                           const wchar_t *extension, char destino[MAX_PATH])
{
    wchar_t ruta[MAX_PATH] = L"";
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = app.ventana;
    ofn.lpstrFilter = filtro;
    ofn.lpstrFile = ruta;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = titulo;
    ofn.lpstrDefExt = extension;
    ofn.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST |
                (guardar ? 0u : (DWORD)OFN_FILEMUSTEXIST);

    BOOL ok = guardar ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    return ok && ruta_a_ansi(ruta, destino);
}

static bool elegir_carpeta(const wchar_t *titulo, char destino[MAX_PATH])
{
    BROWSEINFOW info;
    memset(&info, 0, sizeof(info));
    info.hwndOwner = app.ventana;
    info.lpszTitle = titulo;
    info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    PIDLIST_ABSOLUTE elegido = SHBrowseForFolderW(&info);
    if (elegido == NULL) {
        return false;
    }
    wchar_t ruta[MAX_PATH];
    BOOL ok = SHGetPathFromIDListW(elegido, ruta);
    CoTaskMemFree(elegido);
    return ok && ruta_a_ansi(ruta, destino);
}

static const wchar_t FILTRO_CSV[] = L"Archivos CSV (*.csv)\0*.csv\0Todos los archivos (*.*)\0*.*\0";

/* Lee el campo "Subred". Vacío = sin filtro. */
static bool leer_subred(struct Subred *subred, bool *hay)
{
    wchar_t ancho[64];
    char texto[64];
    GetWindowTextW(app.subred, ancho, 64);
    WideCharToMultiByte(CP_ACP, 0, ancho, -1, texto, sizeof(texto), NULL, NULL);
    *hay = texto[0] != '\0';
    if (*hay && !subred_parsear(texto, subred)) {
        MessageBoxW(app.ventana, L"La subred no es válida. Ejemplo: 192.168.1.0/24\n"
                                 L"(Déjela vacía para no filtrar.)",
                    L"Subred", MB_OK | MB_ICONWARNING);
        return false;
    }
    return true;
}

/* Lee el campo "Gateway". Vacío = sin IP crítica. */
static bool leer_gateway(void)
{
    wchar_t ancho[32];
    char texto[32];
    GetWindowTextW(app.gateway, ancho, 32);
    WideCharToMultiByte(CP_ACP, 0, ancho, -1, texto, sizeof(texto), NULL, NULL);
    app.ip_critica[0] = '\0';
    if (texto[0] != '\0' && !validar_ip(texto, app.ip_critica)) {
        MessageBoxW(app.ventana, L"La IP de la puerta de enlace no es válida.\n"
                                 L"(Déjela vacía para no vigilar ninguna IP.)",
                    L"Gateway", MB_OK | MB_ICONWARNING);
        return false;
    }
    return true;
}

/* Si el campo Gateway está vacío, lo rellena con la puerta de enlace real. */
static void autocompletar_gateway(void)
{
    if (GetWindowTextLengthW(app.gateway) == 0) {
        char ip[MAX_IP];
        if (descubrir_puerta_enlace(ip)) {
            wchar_t ancho[MAX_IP];
            a_ancho(ip, ancho, MAX_IP);
            SetWindowTextW(app.gateway, ancho);
        }
    }
}

/* ---------- Datos ---------- */

static void liberar_datos(void)
{
    if (app.vista == VISTA_RESULTADO) {
        resultado_liberar(&app.resultado); /* antes que las listas: apunta a ellas */
    }
    alertas_liberar(&app.alertas_lista);
    lista_liberar(&app.referencia);
    lista_liberar(&app.observado);
    app.vista = VISTA_VACIA;
    app.descubierta = false;
}

/* ---------- Tablas (ListView) ---------- */

struct Columna {
    const wchar_t *titulo;
    int ancho;
};

static void configurar_columnas(HWND lv, const struct Columna *columnas, int n)
{
    ListView_DeleteAllItems(lv);
    while (ListView_DeleteColumn(lv, 0)) {
    }
    for (int i = 0; i < n; i++) {
        LVCOLUMNW col;
        memset(&col, 0, sizeof(col));
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        col.pszText = (LPWSTR)columnas[i].titulo;
        col.cx = px(columnas[i].ancho);
        col.iSubItem = i;
        ListView_InsertColumn(lv, i, &col);
    }
}

static void agregar_fila(HWND lv, LPARAM dato, const char *const textos[], int n)
{
    wchar_t ancho[MAX_TEXTO];
    LVITEMW item;
    memset(&item, 0, sizeof(item));
    item.mask = LVIF_TEXT | LVIF_PARAM;
    item.iItem = ListView_GetItemCount(lv);
    item.lParam = dato;
    a_ancho(textos[0], ancho, MAX_TEXTO);
    item.pszText = ancho;
    int indice = ListView_InsertItem(lv, &item);
    for (int c = 1; c < n; c++) {
        a_ancho(textos[c], ancho, MAX_TEXTO);
        ListView_SetItemText(lv, indice, c, ancho);
    }
}

static const char *nombre_visible(const char *hostname)
{
    return hostname[0] != '\0' ? hostname : "(desconocido)";
}

static void mostrar_dispositivos(const struct ListaDispositivos *lista, const wchar_t *titulo)
{
    static const struct Columna columnas[] = {
        {L"IP", 130}, {L"MAC", 150}, {L"Hostname", 220}};
    SendMessageW(app.lista, WM_SETREDRAW, FALSE, 0);
    configurar_columnas(app.lista, columnas, 3);
    for (size_t i = 0; i < lista->cantidad; i++) {
        const struct Dispositivo *d = &lista->items[i];
        const char *const textos[] = {d->ip, d->mac, nombre_visible(d->hostname)};
        agregar_fila(app.lista, SIN_COLOR, textos, 3);
    }
    SendMessageW(app.lista, WM_SETREDRAW, TRUE, 0);
    SetWindowTextW(app.titulo_lista, titulo);
}

static void mostrar_hallazgos(const struct Resultado *r)
{
    static const struct Columna columnas[] = {
        {L"Estado", 135}, {L"MAC", 150}, {L"IP", 120}, {L"Hostname", 160}, {L"Detalle", 420}};
    SendMessageW(app.lista, WM_SETREDRAW, FALSE, 0);
    configurar_columnas(app.lista, columnas, 5);
    for (size_t i = 0; i < r->cantidad; i++) {
        const struct Hallazgo *h = &r->hallazgos[i];
        const struct Dispositivo *d = h->observado ? h->observado : h->referencia;
        char detalle[INFORME_MAX_DETALLE];
        informe_describir(r, h, detalle);
        const char *const textos[] = {categoria_nombre(r->modo, h->categoria), d->mac, d->ip,
                                      nombre_visible(d->hostname), detalle};
        agregar_fila(app.lista, (LPARAM)h->categoria, textos, 5);
    }
    SendMessageW(app.lista, WM_SETREDRAW, TRUE, 0);
    SetWindowTextW(app.titulo_lista, r->modo == MODO_CAMBIOS
                                         ? L"Cambios entre instantáneas"
                                         : L"Cumplimiento frente al inventario");
}

static void mostrar_alertas(const struct ListaAlertas *alertas)
{
    static const struct Columna columnas[] = {
        {L"Severidad", 85}, {L"Tipo", 175}, {L"IP", 120}, {L"MAC", 150}, {L"Mensaje", 650}};
    SendMessageW(app.alertas, WM_SETREDRAW, FALSE, 0);
    configurar_columnas(app.alertas, columnas, 5);
    for (size_t i = 0; i < alertas->cantidad; i++) {
        const struct Alerta *a = &alertas->items[i];
        const char *const textos[] = {severidad_nombre(a->severidad), alerta_nombre(a->tipo),
                                      a->ip, a->mac, a->mensaje};
        agregar_fila(app.alertas, (LPARAM)a->severidad, textos, 5);
    }
    SendMessageW(app.alertas, WM_SETREDRAW, TRUE, 0);

    wchar_t titulo[128];
    swprintf(titulo, 128, L"Alertas (%lu)  —  doble clic para ver el mensaje completo",
             (unsigned long)alertas->cantidad);
    SetWindowTextW(app.titulo_alertas, titulo);
}

static void actualizar_botones(void)
{
    EnableWindow(app.guardar, app.vista == VISTA_LISTA && app.descubierta);
    EnableWindow(app.exportar, app.vista == VISTA_RESULTADO);
}

static size_t contar_severidad(const struct ListaAlertas *alertas, enum Severidad severidad)
{
    size_t n = 0;
    for (size_t i = 0; i < alertas->cantidad; i++) {
        if (alertas->items[i].severidad == severidad) {
            n++;
        }
    }
    return n;
}

static void resumen_alertas(const struct ListaAlertas *alertas, wchar_t *destino, size_t tam)
{
    swprintf(destino, tam, L"Alertas: %lu ALTA, %lu MEDIA, %lu BAJA, %lu INFO",
             (unsigned long)contar_severidad(alertas, SEV_ALTA),
             (unsigned long)contar_severidad(alertas, SEV_MEDIA),
             (unsigned long)contar_severidad(alertas, SEV_BAJA),
             (unsigned long)contar_severidad(alertas, SEV_INFO));
}

/* Muestra app.observado como lista simple (abrir CSV o descubrir). */
static void presentar_lista(const wchar_t *titulo, const wchar_t *origen)
{
    if (!alertas_de_lista(&app.observado, &app.alertas_lista)) {
        memset(&app.alertas_lista, 0, sizeof(app.alertas_lista));
    }
    app.vista = VISTA_LISTA;
    mostrar_dispositivos(&app.observado, titulo);
    mostrar_alertas(&app.alertas_lista);
    actualizar_botones();

    wchar_t alertas[160];
    wchar_t texto[512];
    resumen_alertas(&app.alertas_lista, alertas, 160);
    swprintf(texto, 512, L"%ls: %lu dispositivo(s)   |   %ls", origen,
             (unsigned long)app.observado.cantidad, alertas);
    estado(texto);
}

static void presentar_resultado(void)
{
    const struct Resultado *r = &app.resultado;
    app.vista = VISTA_RESULTADO;
    mostrar_hallazgos(r);
    mostrar_alertas(&r->alertas);
    actualizar_botones();

    wchar_t nombres[CAT_TOTAL][32];
    for (int c = 0; c < CAT_TOTAL; c++) {
        a_ancho(categoria_nombre(r->modo, (enum Categoria)c), nombres[c], 32);
    }
    wchar_t alertas[160];
    wchar_t texto[512];
    resumen_alertas(&r->alertas, alertas, 160);
    swprintf(texto, 512, L"%ls %lu · %ls %lu · %ls %lu · %ls %lu   |   %ls   |   %ls",
             nombres[0], (unsigned long)r->por_categoria[0], nombres[1],
             (unsigned long)r->por_categoria[1], nombres[2], (unsigned long)r->por_categoria[2],
             nombres[3], (unsigned long)r->por_categoria[3], alertas,
             resultado_requiere_revision(r) ? L"REQUIERE REVISIÓN" : L"Sin novedades");
    estado(texto);
}

/* ---------- Acciones ---------- */

static bool cargar_archivo(const char *ruta, struct ListaDispositivos *lista, char sha[SHA256_HEX])
{
    if (!csv_cargar(ruta, lista, sha)) {
        mostrar_error(L"No se pudo leer el archivo", L"Error al leer el CSV.");
        return false;
    }
    return true;
}

static void accion_abrir_csv(const char *ruta_dada)
{
    char ruta[MAX_PATH];
    if (ruta_dada != NULL) {
        snprintf(ruta, sizeof(ruta), "%s", ruta_dada);
    } else if (!elegir_archivo(L"Abrir CSV de dispositivos", false, FILTRO_CSV, L"csv", ruta)) {
        return;
    }

    cursor_espera(true);
    limpiar_mensajes();
    liberar_datos();
    char sha[SHA256_HEX];
    if (cargar_archivo(ruta, &app.observado, sha)) {
        wchar_t titulo[MAX_PATH + 32];
        wchar_t ancho[MAX_PATH];
        a_ancho(ruta, ancho, MAX_PATH);
        swprintf(titulo, MAX_PATH + 32, L"Dispositivos en %ls", ancho);
        presentar_lista(titulo, L"Archivo cargado");
    } else {
        presentar_lista(L"Dispositivos", L"Sin datos");
    }
    cursor_espera(false);
}

static void accion_descubrir(void)
{
    struct Subred subred;
    bool hay_subred;
    if (!leer_subred(&subred, &hay_subred)) {
        return;
    }
    cursor_espera(true);
    limpiar_mensajes();
    liberar_datos();
    autocompletar_gateway();

    if (!descubrir_vecinos(hay_subred ? &subred : NULL, &app.observado)) {
        mostrar_error(L"Descubrimiento", L"No se pudo leer la tabla de vecinos.");
        presentar_lista(L"Dispositivos", L"Sin datos");
    } else {
        app.descubierta = true;
        presentar_lista(L"Vecinos en la caché ARP (lectura pasiva, sin enviar paquetes)",
                        L"Descubiertos");
        mostrar_avisos();
    }
    cursor_espera(false);
}

static void accion_guardar_instantanea(void)
{
    char ruta[MAX_PATH];
    if (!elegir_archivo(L"Guardar instantánea", true, FILTRO_CSV, L"csv", ruta)) {
        return;
    }
    limpiar_mensajes();
    if (csv_guardar_instantanea(ruta, &app.observado)) {
        wchar_t texto[MAX_PATH + 64];
        wchar_t ancho[MAX_PATH];
        a_ancho(ruta, ancho, MAX_PATH);
        swprintf(texto, MAX_PATH + 64, L"Instantánea guardada en %ls", ancho);
        estado(texto);
    } else {
        mostrar_error(L"Guardar instantánea", L"No se pudo guardar.");
    }
}

/* Carga app.rutas[0] (referencia) y app.rutas[1] (observado) y los compara. */
static void analizar_rutas(enum Modo modo, const char *comando, const char *rol_ref,
                           const char *rol_obs)
{
    cursor_espera(true);
    limpiar_mensajes();
    liberar_datos();

    memset(&app.ctx, 0, sizeof(app.ctx));
    app.ctx.comando = comando;
    app.ctx.n_entradas = 2;
    app.ctx.ip_critica = app.ip_critica[0] != '\0' ? app.ip_critica : NULL;
    app.ctx.entradas[0].rol = rol_ref;
    app.ctx.entradas[0].ruta = app.rutas[0];
    app.ctx.entradas[1].rol = rol_obs;
    app.ctx.entradas[1].ruta = app.rutas[1];

    bool ok = cargar_archivo(app.rutas[0], &app.referencia, app.ctx.entradas[0].sha256) &&
              cargar_archivo(app.rutas[1], &app.observado, app.ctx.entradas[1].sha256);
    if (ok) {
        app.ctx.entradas[0].dispositivos = app.referencia.cantidad;
        app.ctx.entradas[1].dispositivos = app.observado.cantidad;
        if (analizar(modo, &app.referencia, &app.observado, app.ctx.ip_critica,
                     &app.resultado)) {
            presentar_resultado();
        } else {
            MessageBoxW(app.ventana, L"Memoria insuficiente.", L"Error", MB_OK | MB_ICONERROR);
            ok = false;
        }
    }
    if (!ok) {
        liberar_datos();
        presentar_lista(L"Dispositivos", L"Sin datos");
    }
    cursor_espera(false);
}

static void accion_comparar(const char *anterior, const char *actual)
{
    autocompletar_gateway();
    if (!leer_gateway()) {
        return;
    }
    if (anterior != NULL) {
        snprintf(app.rutas[0], MAX_PATH, "%s", anterior);
        snprintf(app.rutas[1], MAX_PATH, "%s", actual);
    } else if (!elegir_archivo(L"1 de 2: instantánea ANTERIOR", false, FILTRO_CSV, L"csv",
                               app.rutas[0]) ||
               !elegir_archivo(L"2 de 2: instantánea ACTUAL", false, FILTRO_CSV, L"csv",
                               app.rutas[1])) {
        return;
    }
    analizar_rutas(MODO_CAMBIOS, "compare", "anterior", "actual");
}

static void accion_auditar(const char *observado, const char *inventario)
{
    autocompletar_gateway();
    if (!leer_gateway()) {
        return;
    }
    /* rutas[0] = referencia (inventario), rutas[1] = observado */
    if (observado != NULL) {
        snprintf(app.rutas[1], MAX_PATH, "%s", observado);
        snprintf(app.rutas[0], MAX_PATH, "%s", inventario);
    } else if (!elegir_archivo(L"1 de 2: medición OBSERVADA (lo que hay en la red)", false,
                               FILTRO_CSV, L"csv", app.rutas[1]) ||
               !elegir_archivo(L"2 de 2: INVENTARIO aprobado por TI", false, FILTRO_CSV,
                               L"csv", app.rutas[0])) {
        return;
    }
    analizar_rutas(MODO_CUMPLIMIENTO, "audit", "inventario", "observado");
}

static void accion_monitor(void)
{
    struct Subred subred;
    bool hay_subred;
    char carpeta[MAX_PATH];
    if (!leer_subred(&subred, &hay_subred) || !leer_gateway() ||
        !elegir_carpeta(L"Carpeta del historial de instantáneas (p. ej. data\\historial)",
                        carpeta)) {
        return;
    }

    char anterior[HISTORIAL_MAX_RUTA];
    char nueva[HISTORIAL_MAX_RUTA];
    bool hay_anterior = historial_ultima(carpeta, anterior);
    if (!historial_ruta_nueva(carpeta, nueva) || strlen(anterior) >= MAX_PATH ||
        strlen(nueva) >= MAX_PATH) {
        MessageBoxW(app.ventana, L"La ruta de la carpeta es demasiado larga.", L"Monitor",
                    MB_OK | MB_ICONERROR);
        return;
    }

    cursor_espera(true);
    limpiar_mensajes();
    liberar_datos();
    autocompletar_gateway();
    if (!leer_gateway()) {
        cursor_espera(false);
        return;
    }

    struct ListaDispositivos medida;
    if (!descubrir_vecinos(hay_subred ? &subred : NULL, &medida)) {
        mostrar_error(L"Monitor", L"No se pudo leer la tabla de vecinos.");
        cursor_espera(false);
        return;
    }
    bool guardada = csv_guardar_instantanea(nueva, &medida);
    lista_liberar(&medida);
    if (!guardada) {
        mostrar_error(L"Monitor", L"No se pudo guardar la instantánea.");
        cursor_espera(false);
        return;
    }

    if (!hay_anterior) {
        char sha[SHA256_HEX];
        if (cargar_archivo(nueva, &app.observado, sha)) {
            app.descubierta = false; /* ya está guardada */
            presentar_lista(L"Primera instantánea (línea base)", L"Línea base creada");
        }
        cursor_espera(false);
        MessageBoxW(app.ventana,
                    L"Se creó la primera instantánea de esta carpeta (línea base).\n\n"
                    L"Vuelva a pulsar «Monitor» más tarde, con la misma carpeta, para ver "
                    L"qué cambió.",
                    L"Monitor", MB_OK | MB_ICONINFORMATION);
        return;
    }

    snprintf(app.rutas[0], MAX_PATH, "%s", anterior);
    snprintf(app.rutas[1], MAX_PATH, "%s", nueva);
    analizar_rutas(MODO_CAMBIOS, "monitor", "anterior", "actual");
}

static void accion_exportar(void)
{
    char ruta[MAX_PATH];
    if (!elegir_archivo(L"Exportar informe (.json para SIEM, .csv para Excel)", true,
                        L"Informe JSON (*.json)\0*.json\0CSV para Excel (*.csv)\0*.csv\0",
                        L"json", ruta)) {
        return;
    }
    app.ctx.separador =
        SendMessageW(app.punto_y_coma, BM_GETCHECK, 0, 0) == BST_CHECKED ? ';' : ',';
    limpiar_mensajes();
    if (informe_exportar(ruta, &app.ctx, &app.resultado)) {
        wchar_t texto[MAX_PATH + 64];
        wchar_t ancho[MAX_PATH];
        a_ancho(ruta, ancho, MAX_PATH);
        swprintf(texto, MAX_PATH + 64, L"Informe exportado a %ls", ancho);
        estado(texto);
    } else {
        mostrar_error(L"Exportar informe", L"No se pudo exportar el informe.");
    }
}

static void ver_alerta(int indice)
{
    const struct ListaAlertas *alertas =
        app.vista == VISTA_RESULTADO ? &app.resultado.alertas : &app.alertas_lista;
    if (indice < 0 || (size_t)indice >= alertas->cantidad) {
        return;
    }
    const struct Alerta *a = &alertas->items[indice];
    char texto[MAX_MENSAJE + 128];
    snprintf(texto, sizeof(texto), "[%s] %s\nIP: %s\nMAC: %s\n\n%s",
             severidad_nombre(a->severidad), alerta_nombre(a->tipo), a->ip, a->mac, a->mensaje);
    wchar_t ancho[MAX_MENSAJE + 128];
    a_ancho(texto, ancho, MAX_MENSAJE + 128);
    MessageBoxW(app.ventana, ancho, L"Detalle de la alerta",
                MB_OK | (a->severidad >= SEV_MEDIA ? MB_ICONWARNING : MB_ICONINFORMATION));
}

/* ---------- Colores ---------- */

static COLORREF color_alerta(LPARAM severidad)
{
    switch (severidad) {
    case SEV_ALTA: return RGB(255, 205, 205);
    case SEV_MEDIA: return RGB(255, 226, 186);
    case SEV_BAJA: return RGB(255, 247, 200);
    default: return CLR_NONE;
    }
}

static COLORREF color_hallazgo(LPARAM categoria)
{
    switch (categoria) {
    case CAT_SOLO_OBSERVADO: return RGB(255, 247, 200);
    case CAT_CAMBIO: return RGB(255, 226, 186);
    case CAT_SOLO_REFERENCIA: return RGB(232, 232, 232);
    default: return CLR_NONE;
    }
}

static LRESULT dibujar_fila(NMLVCUSTOMDRAW *cd, bool es_alerta)
{
    switch (cd->nmcd.dwDrawStage) {
    case CDDS_PREPAINT:
        return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT: {
        LPARAM dato = cd->nmcd.lItemlParam;
        COLORREF color = dato == SIN_COLOR ? CLR_NONE
                         : es_alerta       ? color_alerta(dato)
                                           : color_hallazgo(dato);
        if (color != CLR_NONE) {
            cd->clrTextBk = color;
            cd->clrText = RGB(0, 0, 0);
        }
        return CDRF_DODEFAULT;
    }
    default:
        return CDRF_DODEFAULT;
    }
}

/* ---------- Ventana ---------- */

static HWND crear(const wchar_t *clase, const wchar_t *texto, DWORD estilo, int id)
{
    HWND h = CreateWindowExW(0, clase, texto, WS_CHILD | WS_VISIBLE | estilo, 0, 0, 0, 0,
                             app.ventana, (HMENU)(INT_PTR)id, app.instancia, NULL);
    SendMessageW(h, WM_SETFONT, (WPARAM)app.fuente, TRUE);
    return h;
}

static HWND crear_tabla(int id)
{
    HWND lv = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                              WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT |
                                  LVS_SHOWSELALWAYS | LVS_SINGLESEL,
                              0, 0, 0, 0, app.ventana, (HMENU)(INT_PTR)id, app.instancia, NULL);
    ListView_SetExtendedListViewStyle(lv, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES |
                                              LVS_EX_DOUBLEBUFFER);
    SendMessageW(lv, WM_SETFONT, (WPARAM)app.fuente, TRUE);
    return lv;
}

static void crear_controles(void)
{
    NONCLIENTMETRICSW metricas;
    memset(&metricas, 0, sizeof(metricas));
    metricas.cbSize = sizeof(metricas);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metricas), &metricas, 0);
    app.fuente = CreateFontIndirectW(&metricas.lfMessageFont);
    metricas.lfMessageFont.lfWeight = FW_BOLD;
    app.fuente_negrita = CreateFontIndirectW(&metricas.lfMessageFont);

    static const wchar_t *const textos[5] = {L"Abrir CSV…", L"Descubrir red", L"Comparar…",
                                             L"Auditar…", L"Monitor…"};
    for (int i = 0; i < 5; i++) {
        app.botones[i] = crear(WC_BUTTONW, textos[i], WS_TABSTOP | BS_PUSHBUTTON, ID_ABRIR + i);
    }

    app.etiqueta_subred = crear(WC_STATICW, L"Subred (alcance):", SS_RIGHT | SS_CENTERIMAGE, 0);
    app.subred = CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, L"",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0,
                                 0, app.ventana, (HMENU)(INT_PTR)ID_SUBRED, app.instancia, NULL);
    SendMessageW(app.subred, WM_SETFONT, (WPARAM)app.fuente, TRUE);
    SendMessageW(app.subred, EM_SETCUEBANNER, TRUE, (LPARAM)L"todas (ej. 192.168.1.0/24)");

    app.etiqueta_gateway = crear(WC_STATICW, L"Gateway vigilado:", SS_RIGHT | SS_CENTERIMAGE, 0);
    app.gateway = CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, L"",
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0,
                                  0, app.ventana, (HMENU)(INT_PTR)ID_GATEWAY, app.instancia, NULL);
    SendMessageW(app.gateway, WM_SETFONT, (WPARAM)app.fuente, TRUE);
    SendMessageW(app.gateway, EM_SETCUEBANNER, TRUE, (LPARAM)L"automático");

    app.punto_y_coma = crear(WC_BUTTONW, L"CSV con «;» (Excel en español)",
                             WS_TABSTOP | BS_AUTOCHECKBOX, ID_PUNTO_Y_COMA);

    app.titulo_lista = crear(WC_STATICW, L"Elija una acción en la barra superior", 0, 0);
    SendMessageW(app.titulo_lista, WM_SETFONT, (WPARAM)app.fuente_negrita, TRUE);
    app.lista = crear_tabla(ID_LISTA);

    app.titulo_alertas = crear(WC_STATICW, L"Alertas", 0, 0);
    SendMessageW(app.titulo_alertas, WM_SETFONT, (WPARAM)app.fuente_negrita, TRUE);
    app.alertas = crear_tabla(ID_ALERTAS);

    app.guardar = crear(WC_BUTTONW, L"Guardar instantánea…", WS_TABSTOP | BS_PUSHBUTTON,
                        ID_GUARDAR);
    app.exportar = crear(WC_BUTTONW, L"Exportar informe…", WS_TABSTOP | BS_PUSHBUTTON,
                         ID_EXPORTAR);

    app.estado = CreateWindowExW(0, STATUSCLASSNAMEW, L"",
                                 WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0,
                                 app.ventana, (HMENU)(INT_PTR)ID_ESTADO, app.instancia, NULL);
    SendMessageW(app.estado, WM_SETFONT, (WPARAM)app.fuente, TRUE);

    actualizar_botones();
    estado(L"Todo es local y pasivo: ShadowAsset no envía paquetes ni necesita ser "
           L"administrador.");
}

static void distribuir(int ancho, int alto)
{
    SendMessageW(app.estado, WM_SIZE, 0, 0);
    RECT r;
    GetWindowRect(app.estado, &r);
    int alto_estado = r.bottom - r.top;

    int m = px(10);
    int bh = px(30);
    int sep = px(8);
    int bw = px(130);

    int x = m;
    for (int i = 0; i < 5; i++) {
        MoveWindow(app.botones[i], x, m, bw, bh, TRUE);
        x += bw + sep;
    }

    int y2 = m + bh + sep;
    int eh = px(24);
    x = m;
    MoveWindow(app.etiqueta_subred, x, y2, px(110), eh, TRUE);
    x += px(110) + sep;
    MoveWindow(app.subred, x, y2, px(190), eh, TRUE);
    x += px(190) + px(16);
    MoveWindow(app.etiqueta_gateway, x, y2, px(110), eh, TRUE);
    x += px(110) + sep;
    MoveWindow(app.gateway, x, y2, px(120), eh, TRUE);
    x += px(120) + px(16);
    MoveWindow(app.punto_y_coma, x, y2, px(250), eh, TRUE);

    int th = px(20);
    int y3 = y2 + eh + px(12);
    int abajo = alto - alto_estado - m - bh;    /* fila de botones inferiores */
    int disponible = abajo - sep - y3 - 2 * th - sep;
    if (disponible < px(120)) {
        disponible = px(120);
    }
    int alto_lista = disponible * 62 / 100;
    int alto_alertas = disponible - alto_lista;

    MoveWindow(app.titulo_lista, m, y3, ancho - 2 * m, th, TRUE);
    MoveWindow(app.lista, m, y3 + th, ancho - 2 * m, alto_lista, TRUE);
    int y4 = y3 + th + alto_lista + sep;
    MoveWindow(app.titulo_alertas, m, y4, ancho - 2 * m, th, TRUE);
    MoveWindow(app.alertas, m, y4 + th, ancho - 2 * m, alto_alertas, TRUE);

    int bw2 = px(170);
    MoveWindow(app.exportar, ancho - m - bw2, abajo, bw2, bh, TRUE);
    MoveWindow(app.guardar, ancho - m - 2 * bw2 - sep, abajo, bw2, bh, TRUE);
}

static LRESULT CALLBACK procedimiento(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        app.ventana = hwnd;
        crear_controles();
        return 0;

    case WM_SIZE:
        distribuir(LOWORD(lp), HIWORD(lp));
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO *mmi = (MINMAXINFO *)lp;
        mmi->ptMinTrackSize.x = px(860);
        mmi->ptMinTrackSize.y = px(480);
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_ABRIR: accion_abrir_csv(NULL); break;
        case ID_DESCUBRIR: accion_descubrir(); break;
        case ID_COMPARAR: accion_comparar(NULL, NULL); break;
        case ID_AUDITAR: accion_auditar(NULL, NULL); break;
        case ID_MONITOR: accion_monitor(); break;
        case ID_GUARDAR: accion_guardar_instantanea(); break;
        case ID_EXPORTAR: accion_exportar(); break;
        default: break;
        }
        return 0;

    case WM_NOTIFY: {
        NMHDR *h = (NMHDR *)lp;
        if (h->code == NM_CUSTOMDRAW && (h->idFrom == ID_LISTA || h->idFrom == ID_ALERTAS)) {
            return dibujar_fila((NMLVCUSTOMDRAW *)lp, h->idFrom == ID_ALERTAS);
        }
        if (h->code == NM_DBLCLK && h->idFrom == ID_ALERTAS) {
            ver_alerta(((NMITEMACTIVATE *)lp)->iItem);
        }
        return 0;
    }

    case WM_DESTROY:
        liberar_datos();
        DeleteObject(app.fuente);
        DeleteObject(app.fuente_negrita);
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

/* Abre lo que se pasó por la línea de comandos (si se pasó algo). */
static void procesar_argumentos(void)
{
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == NULL) {
        return;
    }
    char args[3][MAX_PATH];
    int n = argc - 1 < 3 ? argc - 1 : 3;
    bool ok = true;
    for (int i = 0; i < n && ok; i++) {
        ok = ruta_a_ansi(argv[i + 1], args[i]);
    }
    LocalFree(argv);
    if (!ok || n == 0) {
        return;
    }

    if (n == 1 && strcmp(args[0], "discover") == 0) {
        accion_descubrir();
    } else if (n == 2 && strcmp(args[0], "show") == 0) {
        accion_abrir_csv(args[1]);
    } else if (n == 3 && strcmp(args[0], "compare") == 0) {
        accion_comparar(args[1], args[2]);
    } else if (n == 3 && strcmp(args[0], "audit") == 0) {
        accion_auditar(args[1], args[2]);
    } else if (n == 1) {
        accion_abrir_csv(args[0]); /* arrastrar un CSV sobre el .exe */
    }
}

int WINAPI wWinMain(HINSTANCE instancia, HINSTANCE previa, PWSTR linea, int mostrar)
{
    (void)previa;
    (void)linea;
    app.instancia = instancia;
    registro_establecer(recoger_mensaje, NULL);

    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES |
                                                 ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    if (FAILED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) {
        return 1;
    }

    HDC pantalla = GetDC(NULL);
    app.dpi = GetDeviceCaps(pantalla, LOGPIXELSY);
    ReleaseDC(NULL, pantalla);

    WNDCLASSEXW clase;
    memset(&clase, 0, sizeof(clase));
    clase.cbSize = sizeof(clase);
    clase.lpfnWndProc = procedimiento;
    clase.hInstance = instancia;
    clase.hCursor = LoadCursorW(NULL, IDC_ARROW);
    clase.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    clase.lpszClassName = L"ShadowAssetVentana";
    clase.hIcon = LoadIconW(instancia, MAKEINTRESOURCEW(1));
    if (clase.hIcon == NULL) {
        clase.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    }
    clase.hIconSm = clase.hIcon;
    RegisterClassExW(&clase);

    HWND ventana = CreateWindowExW(
        0, clase.lpszClassName, L"ShadowAsset " SHADOWASSET_VERSION_W L" — Auditoría de activos de red",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, px(1100), px(700), NULL, NULL,
        instancia, NULL);
    if (ventana == NULL) {
        CoUninitialize();
        return 1;
    }
    ShowWindow(ventana, mostrar);
    UpdateWindow(ventana);
    procesar_argumentos();

    MSG mensaje;
    while (GetMessageW(&mensaje, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(ventana, &mensaje)) { /* permite moverse con Tab */
            TranslateMessage(&mensaje);
            DispatchMessageW(&mensaje);
        }
    }
    CoUninitialize();
    return (int)mensaje.wParam;
}
