/*
 * stdint.h for the Windows NT 3.1 SDK compilers (cl386 8.00 and the MIPS
 * and Alpha compilers of 1993), which have none. Only BearSSL uses it.
 *
 * These compilers have no 64-bit integer type, so uint64_t is a pair of
 * 32-bit halves. BearSSL only counts with it (bytes hashed, TLS record
 * sequence numbers), through the br_u64_*() helpers in src/inner.h, and
 * SHA-384/512 work on the halves. Any other arithmetic on it is a compile
 * error, so no 64-bit use can slip through unnoticed.
 */

#ifndef NT31_STDINT_H__
#define NT31_STDINT_H__

typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef unsigned int uintptr_t;     /* NT pointers are 32-bit, also on Alpha */

typedef struct {
	uint32_t hi, lo;
} uint64_t;

#endif
