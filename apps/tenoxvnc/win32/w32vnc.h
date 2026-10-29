/*
 * w32vnc.h - what the win32/ files share with each other.
 */

#include "w32res.h"

#define WM_RFBSOCKET	(WM_USER + 1)	/* WSAAsyncSelect on the server socket */

extern HINSTANCE w32Inst;
extern HWND w32Wnd;
extern Bool w32QuietExit;	/* set when quitting is what was asked for */

/* w32xt.c */
extern void W32SocketEvent(WPARAM wParam, LPARAM lParam);

/* w32desk.c */
extern void W32CreateWindow(void);
extern void W32ShowWindow(void);
extern void W32SetGeometry(const char *spec);

/* w32color.c */
extern HPALETTE w32Palette;
extern void W32ColorTable(RGBQUAD *table);
extern void W32SelectPalette(HDC dc);

/* w32curs.c */
extern void W32CursorInit(void);
extern HCURSOR W32Cursor(void);

/* w32key.c */
extern void W32KeyEvent(UINT msg, WPARAM wParam, LPARAM lParam);
extern void W32ReleaseKeys(void);

/* w32misc.c */
extern void W32DrawClipboard(void);
extern void W32ChangeCbChain(WPARAM wParam, LPARAM lParam);
extern void W32Activated(Bool active);
