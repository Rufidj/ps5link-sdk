#ifndef PS5LINK_COMMON_BUF_H
#define PS5LINK_COMMON_BUF_H

/* Small growable-byte-buffer and string-table helpers shared by mkelf_test.c
 * and dynwriter.c. Factored out once both needed the same thing. */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint8_t *data; size_t len, cap; } Buf;

static inline void buf_init(Buf *b) { b->data = NULL; b->len = 0; b->cap = 0; }
static inline void buf_free(Buf *b) { free(b->data); b->data = NULL; b->len = b->cap = 0; }

static inline void buf_push(Buf *b, const void *p, size_t n) {
    if (b->len + n > b->cap) {
        size_t newcap = b->cap == 0 ? 256 : b->cap * 2;
        while (newcap < b->len + n) newcap *= 2;
        b->data = realloc(b->data, newcap);
        b->cap = newcap;
    }
    memcpy(b->data + b->len, p, n);
    b->len += n;
}
static inline void buf_u8(Buf *b, uint8_t v) { buf_push(b, &v, 1); }
static inline void buf_u32(Buf *b, uint32_t v) { buf_push(b, &v, 4); }
static inline void buf_u64(Buf *b, uint64_t v) { buf_push(b, &v, 8); }
static inline void buf_pad_to(Buf *b, size_t total) { while (b->len < total) buf_u8(b, 0); }

/* .dynstr-shaped string table: index 0 is always the empty string. */
typedef struct { Buf bytes; } StrTab;
static inline void strtab_init(StrTab *s) { buf_init(&s->bytes); buf_u8(&s->bytes, 0); }
static inline uint32_t strtab_add(StrTab *s, const char *str) {
    uint32_t off = (uint32_t)s->bytes.len;
    buf_push(&s->bytes, str, strlen(str) + 1);
    return off;
}

static inline uint64_t align_up(uint64_t v, uint64_t a) {
    return a <= 1 ? v : (v + a - 1) / a * a;
}

/* Standard SysV elf_hash / PJW hash (DynamicWriter.cs's ElfHash). */
static inline uint32_t elf_hash(const char *name) {
    uint32_t h = 0;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
        h = (h << 4) + *p;
        uint32_t carry = h & 0xF0000000u;
        if (carry != 0) h ^= carry >> 24;
        h &= ~carry;
    }
    return h;
}

#endif
