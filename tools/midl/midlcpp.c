/*
 * midlcpp - the C preprocessor MIDL runs on an interface (MIDL_CPP), the cl -E of
 * the target as for NT. MIDL runs on the 32-bit ARM Wine, the compiler on the arm64
 * one: this runs tools/midl/midlcpp there by Wine's Unix spawn, which waits for it,
 * and copies what it wrote to the standard output MIDL redirected.
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>

typedef int (__cdecl *UNIX_SPAWNVP)(char *const argv[], int wait);

int main(int argc, char **argv)
{
    char root[MAX_PATH], script[MAX_PATH], out[64], dos[80], buf[4096], *a[256], *p;
    UNIX_SPAWNVP spawn;
    FILE *f;
    size_t n;
    int i, status;

    spawn = (UNIX_SPAWNVP)GetProcAddress(GetModuleHandleA("ntdll.dll"), "__wine_unix_spawnvp");
    if (spawn == NULL || argc > 250 || !GetModuleFileNameA(NULL, root, sizeof(root)))
        return 1;

    for (i = 0; i < 3 && (p = strrchr(root, '\\')) != NULL; i++)
        *p = 0;
    for (p = root; *p; p++)
        if (*p == '\\')
            *p = '/';
    sprintf(script, "%s/tools/midl/midlcpp", root + 2);
    sprintf(out, "/tmp/midlcpp.%lu", GetCurrentProcessId());
    a[0] = "/bin/sh";
    a[1] = script;
    a[2] = out;
    for (i = 1; i < argc; i++)
        a[i + 2] = argv[i];
    a[argc + 2] = NULL;
    status = spawn(a, TRUE);

    sprintf(dos, "Z:%s", out);
    if ((f = fopen(dos, "rb")) == NULL)
        return status ? status : 1;
    _setmode(_fileno(stdout), _O_BINARY);
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        fwrite(buf, 1, n, stdout);
    fclose(f);
    DeleteFileA(dos);
    return status;
}
