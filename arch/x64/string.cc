/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */


#include <string.h>
#include <stdint.h>
#include "cpuid.hh"
#include "debug.hh"
#include "exceptions.hh"
#include "prio.hh"

extern "C"
void *memcpy_base(void *__restrict dest, const void *__restrict src, size_t n);
extern "C"
void *memset_base(void *__restrict dest, int c, size_t n);

static inline __always_inline void
repmovsq(void *__restrict &dest, const void *__restrict &src, size_t &n)
{
    asm volatile
       ("1: \n\t"
        "rep movsq\n\t"
        ".pushsection .memcpy_decode, \"ax\" \n\t"
        ".quad 1b, 8\n\t"
        ".popsection\n"
            : "+D"(dest), "+S"(src), "+c"(n) : : "memory");
}

static inline __always_inline void
repmovsb(void *__restrict dest, const void *src, size_t n)
{
    asm volatile
       ("1: \n\t"
        "rep movsb\n\t"
        ".pushsection .memcpy_decode, \"ax\" \n\t"
        ".quad 1b, 1\n\t"
        ".popsection\n"
            : "+D"(dest), "+S"(src), "+c"(n) : : "memory");
}

extern "C"
void *memcpy_repmov_old(void *__restrict dest, const void *__restrict src, size_t n)
{
    auto ret = dest;
    auto nw = n / 8;
    auto nb = n & 7;
    repmovsq(dest, src, nw);
    repmovsb(dest, src, nb);
    return ret;
}


extern "C"
void *memcpy_repmov(void *dest, const void *src, size_t n)
{
    auto ret = dest;
    repmovsb(dest, src, n);
    return ret;
}

extern "C"
void *(*resolve_memcpy())(void *__restrict dest, const void *__restrict src, size_t n)
{
    if (processor::features().repmovsb) {
        return memcpy_repmov;
    }
    return memcpy_repmov_old;
}

void *memcpy(void *__restrict dest, const void *__restrict src, size_t n)
    __attribute__((ifunc("resolve_memcpy")));

extern memcpy_decoder memcpy_decode_start[], memcpy_decode_end[];

static void sort_memcpy_decoder() __attribute__((constructor(init_prio::sort)));

static void sort_memcpy_decoder()
{
    std::sort(memcpy_decode_start, memcpy_decode_end);
}

void memcpy_decoder::memcpy_fixup(exception_frame *ef, size_t fixup)
{
    int direction = ef->rflags & (1 << 10);
    if (!direction) {
        ef->rdi += fixup;
        ef->rsi += fixup;
    } else {
        ef->rsi -= fixup;
        ef->rdi -= fixup;
    }
    ef->rcx -= fixup / _word_size;
}

unsigned char *memcpy_decoder::dest(exception_frame *ef)
{
    return reinterpret_cast<unsigned char *>(ef->rdi);
}

unsigned char *memcpy_decoder::src(exception_frame *ef)
{
    return reinterpret_cast<unsigned char *>(ef->rsi);
}

ulong memcpy_decoder::size(exception_frame *ef)
{
    return ef->rcx;
}

memcpy_decoder::memcpy_decoder(ulong pc, ulong word_size)
    : _pc(pc), _word_size(word_size)
{
}

memcpy_decoder *memcpy_find_decoder(exception_frame *ef)
{
    memcpy_decoder v{ef->rip, 0};
    auto dec = std::lower_bound(memcpy_decode_start, memcpy_decode_end, v);
    if (dec != memcpy_decode_end && ((*dec) == ef->rip)) {
        return &*dec;
    }
    return nullptr;
}

extern "C"
void *memset_repstos_old(void *__restrict dest, int c, size_t n)
{
    auto ret = dest;
    auto nw = n / 8;
    auto nb = n & 7;
    auto cw = (uint8_t)c * 0x0101010101010101ull;
    asm volatile("rep stosq" : "+D"(dest), "+c"(nw) : "a"(cw) : "memory");
    asm volatile("rep stosb" : "+D"(dest), "+c"(nb) : "a"(cw) : "memory");
    return ret;
}

extern "C"
void *memset_repstosb(void *__restrict dest, int c, size_t n)
{
    auto ret = dest;
    asm volatile("rep stosb" : "+D"(dest), "+c"(n) : "a"(c) : "memory");
    return ret;
}

extern "C"
void *(*resolve_memset())(void *__restrict dest, int c, size_t n)
{
    if (processor::features().repmovsb) {
        return memset_repstosb;
    }
    return memset_repstos_old;
}

void *memset(void *__restrict dest, int c, size_t n)
    __attribute__((ifunc("resolve_memset")));


