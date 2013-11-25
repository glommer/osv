#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <osv/string.h>

#define WT size_t
#define WS (sizeof(WT))

void *memmove(void *dest, const void *src, size_t n)
{
	char *d = dest;
	const char *s = src;

	if (d==s) return d;
	if (d < s || s+n <= d || d+n <= s) return memcpy(d, s, n);

	memcpy_backwards(dest, src, n);
	return dest;
}
