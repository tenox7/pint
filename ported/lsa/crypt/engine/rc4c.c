/*
** RC4, the stream cipher of the LSA crypt engine (rc4.h), whose source is not
** in the tree: as the engine objects define it. rc4 takes a buffer of one
** byte or more.
*/

#include <nt.h>
#include "rc4.h"

void rc4_key(struct RC4_KEYSTRUCT *pks, int keylen, PUCHAR pbKey)
{
    unsigned char j, k, t;
    int i;

    for (i = 0; i < 256; i++)
        pks->S[i] = (unsigned char)i;
    pks->i = 0;
    pks->j = 0;
    for (i = 0, j = 0, k = 0; i < 256; i++) {
        j = (j + pks->S[i] + pbKey[k]) % 256;
        t = pks->S[i];
        pks->S[i] = pks->S[j];
        pks->S[j] = t;
        k = (k + 1) % keylen;
    }
}

void rc4(struct RC4_KEYSTRUCT *pks, int cbBuffer, PUCHAR pbBuffer)
{
    unsigned char i = pks->i, j = pks->j, t;

    do {
        i = (i + 1) % 256;
        j = (j + pks->S[i]) % 256;
        t = pks->S[i];
        pks->S[i] = pks->S[j];
        pks->S[j] = t;
        *pbBuffer++ ^= pks->S[(pks->S[i] + t) % 256];
    } while (--cbBuffer);
    pks->i = i;
    pks->j = j;
}
