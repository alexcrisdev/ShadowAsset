#ifndef SHADOWASSET_SHA256_H
#define SHADOWASSET_SHA256_H

#include <stddef.h>
#include <stdint.h>

#define SHA256_HEX 65 /* 64 dígitos hexadecimales + '\0' */

/* Implementación de SHA-256 (FIPS 180-4). Se usa para registrar la huella
 * de cada archivo analizado y poder verificar después que no cambió. */
struct Sha256 {
    uint32_t estado[8];
    uint64_t bits;
    unsigned char bloque[64];
    size_t usado;
};

void sha256_iniciar(struct Sha256 *ctx);
void sha256_actualizar(struct Sha256 *ctx, const void *datos, size_t largo);
void sha256_finalizar(struct Sha256 *ctx, char hex[SHA256_HEX]);

void sha256_texto(const void *datos, size_t largo, char hex[SHA256_HEX]);

#endif
