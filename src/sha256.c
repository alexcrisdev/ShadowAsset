#include "sha256.h"

#include <string.h>

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
    0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
    0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
    0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
    0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
    0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
    0xc67178f2};

static uint32_t rotr(uint32_t x, unsigned n)
{
    return (x >> n) | (x << (32u - n));
}

static void procesar_bloque(struct Sha256 *ctx, const unsigned char b[64])
{
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = (uint32_t)b[i * 4] << 24 | (uint32_t)b[i * 4 + 1] << 16 |
               (uint32_t)b[i * 4 + 2] << 8 | (uint32_t)b[i * 4 + 3];
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = ctx->estado[0], bb = ctx->estado[1], c = ctx->estado[2], d = ctx->estado[3];
    uint32_t e = ctx->estado[4], f = ctx->estado[5], g = ctx->estado[6], h = ctx->estado[7];

    for (int i = 0; i < 64; i++) {
        uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + S1 + ch + K[i] + w[i];
        uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t maj = (a & bb) ^ (a & c) ^ (bb & c);
        uint32_t t2 = S0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = bb;
        bb = a;
        a = t1 + t2;
    }

    ctx->estado[0] += a;
    ctx->estado[1] += bb;
    ctx->estado[2] += c;
    ctx->estado[3] += d;
    ctx->estado[4] += e;
    ctx->estado[5] += f;
    ctx->estado[6] += g;
    ctx->estado[7] += h;
}

void sha256_iniciar(struct Sha256 *ctx)
{
    static const uint32_t inicial[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memcpy(ctx->estado, inicial, sizeof(inicial));
    ctx->bits = 0;
    ctx->usado = 0;
}

void sha256_actualizar(struct Sha256 *ctx, const void *datos, size_t largo)
{
    const unsigned char *p = datos;
    ctx->bits += (uint64_t)largo * 8u;
    while (largo > 0) {
        size_t copiar = 64 - ctx->usado;
        if (copiar > largo) {
            copiar = largo;
        }
        memcpy(ctx->bloque + ctx->usado, p, copiar);
        ctx->usado += copiar;
        p += copiar;
        largo -= copiar;
        if (ctx->usado == 64) {
            procesar_bloque(ctx, ctx->bloque);
            ctx->usado = 0;
        }
    }
}

void sha256_finalizar(struct Sha256 *ctx, char hex[SHA256_HEX])
{
    uint64_t bits = ctx->bits;
    unsigned char relleno = 0x80;
    sha256_actualizar(ctx, &relleno, 1);
    unsigned char cero = 0;
    while (ctx->usado != 56) {
        sha256_actualizar(ctx, &cero, 1);
    }
    unsigned char largo[8];
    for (int i = 0; i < 8; i++) {
        largo[i] = (unsigned char)(bits >> (56 - 8 * i));
    }
    sha256_actualizar(ctx, largo, 8);

    static const char digitos[] = "0123456789abcdef";
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 4; j++) {
            unsigned byte = (ctx->estado[i] >> (24 - 8 * j)) & 0xFFu;
            hex[i * 8 + j * 2] = digitos[byte >> 4];
            hex[i * 8 + j * 2 + 1] = digitos[byte & 0x0Fu];
        }
    }
    hex[64] = '\0';
}

void sha256_texto(const void *datos, size_t largo, char hex[SHA256_HEX])
{
    struct Sha256 ctx;
    sha256_iniciar(&ctx);
    sha256_actualizar(&ctx, datos, largo);
    sha256_finalizar(&ctx, hex);
}
