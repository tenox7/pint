#ifndef NTPERF_NUMBERS_H
#define NTPERF_NUMBERS_H
/* Memory values in KiB, and their signed differences (Mem Delta): NT 3.x is C89/32-bit. */
typedef unsigned int NP_SIZE;
typedef int NP_SSIZE;
#ifndef NTPERF_BACKEND
/* Avoid wsprintf's architecture-dependent 64-bit format support and any CRT. */
static void formatSize(char *out, NP_SIZE value)
{
    char reverse[24];
    int n=0;
    do { reverse[n++]=(char)('0'+value%10); value/=10; } while (value);
    while (n) *out++=reverse[--n];
    *out=0;
}
#endif
#endif
