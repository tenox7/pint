/*
 * w32key.c - Windows keys to X keysyms.  Keys that type nothing go through
 * a table; the rest through ToUnicode with the keyboard layout in use, so
 * AltGr and dead keys come out the way the layout means them.  A release
 * sends whatever its press did.
 */

#include <windows.h>
#include "vncviewer.h"
#include "w32vnc.h"

#define XK_BackSpace	0xff08
#define XK_Tab		0xff09
#define XK_Clear	0xff0b
#define XK_Return	0xff0d
#define XK_Pause	0xff13
#define XK_Scroll_Lock	0xff14
#define XK_Escape	0xff1b
#define XK_Home		0xff50
#define XK_Left		0xff51
#define XK_Up		0xff52
#define XK_Right	0xff53
#define XK_Down		0xff54
#define XK_Page_Up	0xff55
#define XK_Page_Down	0xff56
#define XK_End		0xff57
#define XK_Select	0xff60
#define XK_Print	0xff61
#define XK_Execute	0xff62
#define XK_Insert	0xff63
#define XK_Menu		0xff67
#define XK_Help		0xff6a
#define XK_Break	0xff6b
#define XK_Num_Lock	0xff7f
#define XK_KP_Enter	0xff8d
#define XK_KP_Multiply	0xffaa
#define XK_KP_Add	0xffab
#define XK_KP_Separator	0xffac
#define XK_KP_Subtract	0xffad
#define XK_KP_Decimal	0xffae
#define XK_KP_Divide	0xffaf
#define XK_KP_0		0xffb0
#define XK_F1		0xffbe
#define XK_Shift_L	0xffe1
#define XK_Shift_R	0xffe2
#define XK_Control_L	0xffe3
#define XK_Control_R	0xffe4
#define XK_Caps_Lock	0xffe5
#define XK_Alt_L	0xffe9
#define XK_Alt_R	0xffea
#define XK_Super_L	0xffeb
#define XK_Super_R	0xffec
#define XK_Delete	0xffff
#define XK_EuroSign	0x20ac

#ifndef VK_LWIN
#define VK_LWIN		0x5b
#define VK_RWIN		0x5c
#define VK_APPS		0x5d
#endif

static const struct {
  UINT vk;
  CARD32 sym;
} keyTable[] = {
  { VK_BACK, XK_BackSpace },	{ VK_TAB, XK_Tab },
  { VK_CLEAR, XK_Clear },	{ VK_PAUSE, XK_Pause },
  { VK_CAPITAL, XK_Caps_Lock },	{ VK_ESCAPE, XK_Escape },
  { VK_PRIOR, XK_Page_Up },	{ VK_NEXT, XK_Page_Down },
  { VK_END, XK_End },		{ VK_HOME, XK_Home },
  { VK_LEFT, XK_Left },		{ VK_UP, XK_Up },
  { VK_RIGHT, XK_Right },	{ VK_DOWN, XK_Down },
  { VK_SELECT, XK_Select },	{ VK_EXECUTE, XK_Execute },
  { VK_SNAPSHOT, XK_Print },	{ VK_INSERT, XK_Insert },
  { VK_DELETE, XK_Delete },	{ VK_HELP, XK_Help },
  { VK_MULTIPLY, XK_KP_Multiply }, { VK_ADD, XK_KP_Add },
  { VK_SEPARATOR, XK_KP_Separator }, { VK_SUBTRACT, XK_KP_Subtract },
  { VK_DECIMAL, XK_KP_Decimal }, { VK_DIVIDE, XK_KP_Divide },
  { VK_NUMLOCK, XK_Num_Lock },	{ VK_SCROLL, XK_Scroll_Lock },
  { VK_CANCEL, XK_Break },	{ VK_LWIN, XK_Super_L },
  { VK_RWIN, XK_Super_R },	{ VK_APPS, XK_Menu }
};

/* what each key sent when it went down, by scan code and extended bit */
#define MAX_SYMS 2
static struct {
  CARD32 sym[MAX_SYMS];
  int n;
  Bool altGr;			/* sent with Ctrl and Alt let go around it */
} keyDown[512];


static CARD32
SpecialKey(UINT vk, LPARAM lParam)
{
  Bool extended = (lParam & 0x01000000) != 0;
  UINT scan = (lParam >> 16) & 0xff;
  int i;

  switch (vk) {
  case VK_SHIFT:   return scan == 0x36 ? XK_Shift_R : XK_Shift_L;
  case VK_CONTROL: return extended ? XK_Control_R : XK_Control_L;
  case VK_MENU:    return extended ? XK_Alt_R : XK_Alt_L;
  case VK_RETURN:  return extended ? XK_KP_Enter : XK_Return;
  }

  if (vk >= VK_F1 && vk <= VK_F24)
    return XK_F1 + (vk - VK_F1);
  if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9)
    return XK_KP_0 + (vk - VK_NUMPAD0);

  for (i = 0; i < (int)XtNumber(keyTable); i++)
    if (keyTable[i].vk == vk)
      return keyTable[i].sym;

  return 0;
}

static CARD32
CharToKeysym(WCHAR c)
{
  if ((c >= 0x20 && c < 0x7f) || (c >= 0xa0 && c <= 0xff))
    return c;			/* Latin-1 is its own keysym */
  if (c == 0x20ac)
    return XK_EuroSign;
  if (c < 0x20 || (c >= 0x7f && c < 0xa0))
    return 0;
  return 0x01000000 | c;	/* the X keysym for any other Unicode */
}

static Bool
IsCtrlOrAlt(CARD32 sym)
{
  return sym == XK_Control_L || sym == XK_Control_R ||
	 sym == XK_Alt_L || sym == XK_Alt_R;
}

/* Ctrl and Alt as the server has them, up or back down around an AltGr key */
static void
SendCtrlAlt(Bool down)
{
  int k, i;

  for (k = 0; k < 512; k++)
    for (i = 0; i < keyDown[k].n; i++)
      if (IsCtrlOrAlt(keyDown[k].sym[i]))
	SendKeyEvent(keyDown[k].sym[i], down);
}

static void
KeyUp(int key)
{
  int i;

  if (keyDown[key].n == 0)
    return;

  if (keyDown[key].altGr)
    SendCtrlAlt(False);
  for (i = keyDown[key].n - 1; i >= 0; i--)
    SendKeyEvent(keyDown[key].sym[i], False);
  if (keyDown[key].altGr)
    SendCtrlAlt(True);

  keyDown[key].n = 0;
  keyDown[key].altGr = False;
}

/* the last character goes down and stays there, the ones before it type */
static void
SendChars(int key, WCHAR *chars, int n, Bool altGr)
{
  CARD32 sym;
  int i;

  if (altGr)
    SendCtrlAlt(False);

  for (i = 0; i < n; i++) {
    if ((sym = CharToKeysym(chars[i])) == 0)
      continue;
    SendKeyEvent(sym, True);
    if (i < n - 1) {
      SendKeyEvent(sym, False);
    } else {
      keyDown[key].sym[0] = sym;
      keyDown[key].n = 1;
      keyDown[key].altGr = altGr;
    }
  }

  if (altGr)
    SendCtrlAlt(True);
}

static void
CharKey(int key, UINT vk, LPARAM lParam)
{
  UINT scan = (lParam >> 16) & 0xff;
  BYTE state[256];
  WCHAR chars[4];
  int n;

  GetKeyboardState(state);

  /* Ctrl and Alt together is AltGr on many layouts, and @ on a German
     keyboard is not Ctrl-Alt-Q to the server */
  if ((state[VK_CONTROL] & 0x80) && (state[VK_MENU] & 0x80)) {
    n = ToUnicode(vk, scan, state, chars, 4, 0);
    if (n < 0)
      return;			/* a dead key; the next key composes */
    if (n > 0 && CharToKeysym(chars[n - 1])) {
      SendChars(key, chars, n, True);
      return;
    }
  }

  /* Ctrl-C is a C with Control held, not a control character */
  state[VK_CONTROL] = state[VK_LCONTROL] = state[VK_RCONTROL] = 0;

  n = ToUnicode(vk, scan, state, chars, 4, 0);
  if (n > 0)
    SendChars(key, chars, n, False);
}

void
W32KeyEvent(UINT msg, WPARAM wParam, LPARAM lParam)
{
  int key = (int)((lParam >> 16) & 0x1ff);
  CARD32 sym;
  int i;

  if (appData.viewOnly)
    return;

  if (msg == WM_KEYUP || msg == WM_SYSKEYUP) {
    KeyUp(key);
    return;
  }

  /* auto repeat: the same again, without asking the layout twice */
  if ((lParam & 0x40000000) && keyDown[key].n) {
    if (keyDown[key].altGr)
      SendCtrlAlt(False);
    for (i = 0; i < keyDown[key].n; i++)
      SendKeyEvent(keyDown[key].sym[i], True);
    if (keyDown[key].altGr)
      SendCtrlAlt(True);
    return;
  }

  KeyUp(key);

  if ((sym = SpecialKey((UINT)wParam, lParam)) != 0) {
    keyDown[key].sym[0] = sym;
    keyDown[key].n = 1;
    SendKeyEvent(sym, True);
    return;
  }

  CharKey(key, (UINT)wParam, lParam);
}

/* the window lost the keyboard: let go of everything the server holds */
void
W32ReleaseKeys(void)
{
  int k;

  for (k = 0; k < 512; k++)
    if (!IsCtrlOrAlt(keyDown[k].sym[0]))
      KeyUp(k);
  for (k = 0; k < 512; k++)
    KeyUp(k);
}
