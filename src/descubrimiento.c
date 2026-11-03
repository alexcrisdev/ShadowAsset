#include "descubrimiento.h"
#include "registro.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32

#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>

static bool mac_es_unicast(const unsigned char *mac)
{
    bool todo_cero = true;
    for (int i = 0; i < 6; i++) {
        if (mac[i] != 0) {
            todo_cero = false;
        }
    }
    /* El bit menos significativo del primer byte marca multicast/broadcast. */
    return !todo_cero && (mac[0] & 0x01) == 0;
}

static bool estado_util(NL_NEIGHBOR_STATE estado)
{
    return estado == NlnsReachable || estado == NlnsStale || estado == NlnsDelay ||
           estado == NlnsProbe || estado == NlnsPermanent;
}

static void ip_a_texto(const IN_ADDR *direccion, char destino[MAX_IP])
{
    const unsigned char *b = (const unsigned char *)direccion;
    snprintf(destino, MAX_IP, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
}

bool descubrir_vecinos(const struct Subred *alcance, struct ListaDispositivos *lista)
{
    lista_iniciar(lista);

    PMIB_IPNET_TABLE2 tabla = NULL;
    DWORD codigo = GetIpNetTable2(AF_INET, &tabla);
    if (codigo != NO_ERROR) {
        registro_mensaje("Error: GetIpNetTable2 fallo (codigo %lu)\n", (unsigned long)codigo);
        return false;
    }

    bool ok = true;
    for (ULONG i = 0; i < tabla->NumEntries && ok; i++) {
        const MIB_IPNET_ROW2 *fila = &tabla->Table[i];
        if (fila->Address.si_family != AF_INET || fila->PhysicalAddressLength != 6 ||
            !estado_util(fila->State) || !mac_es_unicast(fila->PhysicalAddress)) {
            continue;
        }

        const unsigned char *ip = (const unsigned char *)&fila->Address.Ipv4.sin_addr;
        /* Excluir broadcast limitado y multicast IPv4 (224.0.0.0/4). */
        if ((ip[0] == 255 && ip[1] == 255 && ip[2] == 255 && ip[3] == 255) ||
            (ip[0] >= 224 && ip[0] <= 239)) {
            continue;
        }

        const unsigned char *mac = fila->PhysicalAddress;
        char ip_texto[MAX_IP];
        char mac_texto[MAX_MAC];
        ip_a_texto(&fila->Address.Ipv4.sin_addr, ip_texto);
        snprintf(mac_texto, sizeof(mac_texto), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

        struct Dispositivo d = {0};
        if (!validar_ip(ip_texto, d.ip) || !validar_mac(mac_texto, d.mac)) {
            continue;
        }
        if (alcance != NULL && !subred_contiene(alcance, d.ip)) {
            continue; /* fuera del alcance autorizado */
        }
        ok = lista_agregar(lista, &d);
    }
    FreeMibTable(tabla);

    if (!ok) {
        registro_mensaje("Error: memoria insuficiente\n");
        lista_liberar(lista);
        return false;
    }

    /* Una misma MAC puede responder por varias IP (p. ej. un router):
     * conservamos solo la primera para mantener la MAC como clave. */
    if (lista->cantidad > 1) {
        lista_ordenar_por_mac(lista);
        size_t escritura = 1;
        for (size_t i = 1; i < lista->cantidad; i++) {
            struct Dispositivo *anterior = &lista->items[escritura - 1];
            if (strcmp(lista->items[i].mac, anterior->mac) != 0) {
                lista->items[escritura++] = lista->items[i];
            } else if (strcmp(lista->items[i].ip, anterior->ip) != 0) {
                /* Misma IP vista en dos interfaces no merece aviso. */
                registro_mensaje("Aviso: MAC %s con varias IP; se conserva %s, se omite %s\n",
                        anterior->mac, anterior->ip, lista->items[i].ip);
            }
        }
        lista->cantidad = escritura;
    }
    return true;
}

bool descubrir_puerta_enlace(char ip[MAX_IP])
{
    PMIB_IPFORWARD_TABLE2 tabla = NULL;
    if (GetIpForwardTable2(AF_INET, &tabla) != NO_ERROR) {
        return false;
    }

    bool encontrada = false;
    ULONG mejor_metrica = 0;
    for (ULONG i = 0; i < tabla->NumEntries; i++) {
        const MIB_IPFORWARD_ROW2 *ruta = &tabla->Table[i];
        if (ruta->DestinationPrefix.PrefixLength != 0 ||
            ruta->NextHop.si_family != AF_INET ||
            ruta->NextHop.Ipv4.sin_addr.s_addr == 0) {
            continue;
        }
        if (!encontrada || ruta->Metric < mejor_metrica) {
            ip_a_texto(&ruta->NextHop.Ipv4.sin_addr, ip);
            mejor_metrica = ruta->Metric;
            encontrada = true;
        }
    }
    FreeMibTable(tabla);
    return encontrada;
}

#else

bool descubrir_vecinos(const struct Subred *alcance, struct ListaDispositivos *lista)
{
    (void)alcance;
    lista_iniciar(lista);
    registro_mensaje("Error: el descubrimiento solo esta disponible en Windows por ahora.\n");
    return false;
}

bool descubrir_puerta_enlace(char ip[MAX_IP])
{
    (void)ip;
    return false;
}

#endif
