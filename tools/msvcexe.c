/*
 * msvcexe - cl and link (obj/host/CL.EXE, LINK.EXE) for the NT build tools on the 32-bit
 * ARM Wine that run the compiler and librarian of their machine by name, as NT's build
 * ran them (GUIDLIB: cl /P, cl /c, link /lib): runs tools/msvcexe with the name of this
 * program, the tool of Microsoft C on the arm64 Wine, by Wine's Unix spawn, which waits
 * for it.
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef int (__cdecl *UNIX_SPAWNVP)(char *const argv[], int wait);

int main(int argc, char **argv)
{
    char root[MAX_PATH], script[MAX_PATH], tool[MAX_PATH], *a[256], *p;
    UNIX_SPAWNVP spawn;
    int i;

    spawn = (UNIX_SPAWNVP)GetProcAddress(GetModuleHandleA("ntdll.dll"), "__wine_unix_spawnvp");
    if (spawn == NULL || argc > 250 || !GetModuleFileNameA(NULL, root, sizeof(root)))
        return 1;

    strcpy(tool, strrchr(root, '\\') + 1);
    if ((p = strrchr(tool, '.')) != NULL)
        *p = 0;
    _strlwr(tool);
    for (i = 0; i < 3 && (p = strrchr(root, '\\')) != NULL; i++)
        *p = 0;
    for (p = root; *p; p++)
        if (*p == '\\')
            *p = '/';
    sprintf(script, "%s/tools/msvcexe", root + 2);
    a[0] = "/bin/sh";
    a[1] = script;
    a[2] = tool;
    for (i = 1; i < argc; i++)
        a[i + 2] = argv[i];
    a[argc + 2] = NULL;
    return spawn(a, TRUE);
}
