/* Regression coverage for the ordinary OpenSSL sparse-array C API.
 * Build against an unmodified OpenSSL libcrypto and its matching headers.
 * Callback values are compared with owned objects before dereferencing. */
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/crypto.h>
#include "crypto/sparse_array.h"

#define CAPACITY 1024
struct entry { ossl_uintmax_t index; unsigned marker; unsigned seen; };
struct check {
    struct entry entries[CAPACITY];
    size_t count, calls;
    unsigned errors;
};
static struct check *active;

static void inspect(ossl_uintmax_t index, void *value, void *argument)
{
    size_t i;
    struct check *c = active;
    if (argument != c) {
        fprintf(stderr, "FAIL callback arg %p, expected %p\n", argument, (void *)c);
        c->errors++;
        return;
    }
    c->calls++;
    for (i = 0; i < c->count; ++i) {
        if (c->entries[i].index == index) {
            if (value != &c->entries[i]) {
                fprintf(stderr, "FAIL index=%ju value=%p expected=%p\n",
                        (uintmax_t)index, value, (void *)&c->entries[i]);
                c->errors++;
            }
            if (c->entries[i].seen++) {
                fprintf(stderr, "FAIL duplicate index=%ju\n", (uintmax_t)index);
                c->errors++;
            }
            return;
        }
    }
    fprintf(stderr, "FAIL unexpected index=%ju value=%p\n", (uintmax_t)index, value);
    c->errors++;
}
static void inspect_without_arg(ossl_uintmax_t index, void *value)
{
    inspect(index, value, active);
}
static int verify(const char *name, const ossl_uintmax_t *indices, size_t count)
{
    OPENSSL_SA *sa = ossl_sa_new();
    struct check c;
    size_t i;
    int mode;
    memset(&c, 0, sizeof(c));
    if (sa == NULL || count > CAPACITY) return 0;
    c.count = count;
    active = &c;
    for (i = 0; i < count; ++i) {
        c.entries[i].index = indices[i];
        c.entries[i].marker = (unsigned)i + 1;
        if (!ossl_sa_set(sa, indices[i], &c.entries[i])) {
            fprintf(stderr, "FAIL %s insertion %zu\n", name, i);
            c.errors++;
            break;
        }
    }
    if (ossl_sa_num(sa) != count) c.errors++;
    for (i = 0; i < count; ++i)
        if (ossl_sa_get(sa, indices[i]) != &c.entries[i]) c.errors++;
    for (mode = 0; mode < 2; ++mode) {
        c.calls = 0;
        for (i = 0; i < count; ++i) c.entries[i].seen = 0;
        if (mode == 0) ossl_sa_doall_arg(sa, inspect, &c);
        else ossl_sa_doall(sa, inspect_without_arg);
        if (c.calls != count) c.errors++;
        for (i = 0; i < count; ++i)
            if (c.entries[i].seen != 1) c.errors++;
    }
    ossl_sa_free(sa);
    printf("%s: %s (%zu objects, %u errors)\n", name,
           c.errors ? "FAIL" : "PASS", count, c.errors);
    return c.errors == 0;
}
int main(void)
{
    ossl_uintmax_t indices[CAPACITY] = {0};
    size_t i;
    unsigned level;
    int pass = 1;
    setbuf(stdout, NULL);
    printf("OpenSSL %s; pointer=%zu index=%zu\n", OpenSSL_version(0),
           sizeof(void *), sizeof(ossl_uintmax_t));
    pass &= verify("empty", indices, 0);
    indices[0] = 10;
    pass &= verify("single-leaf-ten", indices, 1);
    for (i = 0; i < 16; ++i) indices[i] = i;
    pass &= verify("one-level-dense", indices, 16);
    for (i = 0; i < 256; ++i) indices[i] = i;
    pass &= verify("two-level-dense", indices, 256);
    for (i = 0; i < CAPACITY; ++i) indices[i] = i;
    pass &= verify("three-level-dense", indices, CAPACITY);
    for (level = 1; level < sizeof(ossl_uintmax_t) * CHAR_BIT / 4; ++level) {
        char name[48];
        ossl_uintmax_t split = (ossl_uintmax_t)1 << (4 * level);
        indices[0] = 0;
        indices[1] = 10;
        indices[2] = split - 1;
        indices[3] = split;
        indices[4] = split + 1;
        indices[5] = split + 10;
        snprintf(name, sizeof(name), "boundary-level-%u", level);
        pass &= verify(name, indices, 6);
    }
    for (i = 0; i < 512; ++i) indices[i] = (ossl_uintmax_t)-1 - i;
    pass &= verify("maximum-index-dense", indices, 512);
    puts(pass ? "ALL INVARIANTS PASS" : "INVARIANTS FAILED");
    return pass ? 0 : 1;
}
