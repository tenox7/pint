/*
 * w32dlg.c - the connection, password and settings dialogs, as Windows
 * dialog boxes.  The Win32 side of dialogs.c: the same fields, the same
 * rules about when each one shows.
 */

#include <windows.h>
#include "vncviewer.h"
#include "w32vnc.h"

#ifndef BST_CHECKED
#define BST_CHECKED	1	/* NT 4.0 headers; NT 3.5 returns the same */
#endif

#define HOST_LEN	255
#define PASS_LEN	63

enum { DLG_CONNECT, DLG_PASSWORD, DLG_SETTINGS };

static int dlgMode;
static const char *dlgMessage;
static Bool dlgInUse = False;

static char dlgHost[HOST_LEN + 1];
static char dlgPass[PASS_LEN + 1];
Bool connectDialogUsed = False;

/* the radio buttons, in the order of their ids */
static const int encVals[] = {
  -1, rfbEncodingTight, rfbEncodingZRLE, rfbEncodingHextile, rfbEncodingRaw
};


static void
EnableQuality(HWND dlg)
{
  BOOL on = IsDlgButtonChecked(dlg, IDC_JPEG) == BST_CHECKED;

  EnableWindow(GetDlgItem(dlg, IDC_QUALITY_LABEL), on);
  EnableWindow(GetDlgItem(dlg, IDC_QUALITY), on);
}

static void
Center(HWND dlg)
{
  HWND owner = GetWindow(dlg, GW_OWNER);
  RECT r, o;

  GetWindowRect(dlg, &r);
  if (owner && IsWindowVisible(owner) && !IsIconic(owner))
    GetWindowRect(owner, &o);
  else
    SetRect(&o, 0, 0, GetSystemMetrics(SM_CXSCREEN),
	    GetSystemMetrics(SM_CYSCREEN));

  SetWindowPos(dlg, NULL,
	       o.left + (o.right - o.left - (r.right - r.left)) / 2,
	       o.top + (o.bottom - o.top - (r.bottom - r.top)) / 2,
	       0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

static void
InitOptions(HWND dlg)
{
  char num[4];
  int i;

  CheckDlgButton(dlg, IDC_SHARED, appData.shareDesktop);
  CheckDlgButton(dlg, IDC_VIEWONLY, appData.viewOnly);
  CheckDlgButton(dlg, IDC_CONTINUOUS, appData.useContinuousUpdates);
  CheckDlgButton(dlg, IDC_RESIZE, appData.useRemoteResize);

  for (i = 0; i < (int)XtNumber(encVals); i++)
    if (encVals[i] == appData.preferredEncoding)
      CheckRadioButton(dlg, IDC_ENC_AUTO, IDC_ENC_RAW, IDC_ENC_AUTO + i);

  CheckDlgButton(dlg, IDC_JPEG, appData.enableJPEG);
  for (i = 0; i <= 9; i++) {
    sprintf(num, "%d", i);
    SendDlgItemMessage(dlg, IDC_QUALITY, CB_ADDSTRING, 0, (LPARAM)num);
  }
  SendDlgItemMessage(dlg, IDC_QUALITY, CB_SETCURSEL,
		     (appData.qualityLevel >= 0 && appData.qualityLevel <= 9)
		     ? appData.qualityLevel : 5, 0);
  EnableQuality(dlg);

  /* the first entry asks the server for nothing, as -1 does */
  SendDlgItemMessage(dlg, IDC_COMPRESS, CB_ADDSTRING, 0, (LPARAM)"Auto");
  for (i = 0; i <= 9; i++) {
    sprintf(num, "%d", i);
    SendDlgItemMessage(dlg, IDC_COMPRESS, CB_ADDSTRING, 0, (LPARAM)num);
  }
  SendDlgItemMessage(dlg, IDC_COMPRESS, CB_SETCURSEL,
		     (appData.compressLevel >= 0 && appData.compressLevel <= 9)
		     ? appData.compressLevel + 1 : 0, 0);

  CheckRadioButton(dlg, IDC_COLOR_FULL, IDC_COLOR_VERYLOW,
		   IDC_COLOR_FULL + appData.colorLevel);

  /* what a session we did not dial ourselves cannot change */
  if (dlgMode == DLG_SETTINGS) {
    EnableWindow(GetDlgItem(dlg, IDC_SHARED), !listenSpecified);
    for (i = IDC_COLOR_FULL; i <= IDC_COLOR_VERYLOW; i++)
      EnableWindow(GetDlgItem(dlg, i),
		   !listenSpecified && ColorLevelSettable());
  }
}

static void
ReadOptions(HWND dlg)
{
  int i;

  appData.shareDesktop = IsDlgButtonChecked(dlg, IDC_SHARED) == BST_CHECKED;
  appData.viewOnly = IsDlgButtonChecked(dlg, IDC_VIEWONLY) == BST_CHECKED;
  appData.useContinuousUpdates =
    IsDlgButtonChecked(dlg, IDC_CONTINUOUS) == BST_CHECKED;
  appData.useRemoteResize = IsDlgButtonChecked(dlg, IDC_RESIZE) == BST_CHECKED;

  for (i = 0; i < (int)XtNumber(encVals); i++)
    if (IsDlgButtonChecked(dlg, IDC_ENC_AUTO + i) == BST_CHECKED)
      appData.preferredEncoding = encVals[i];

  appData.enableJPEG = IsDlgButtonChecked(dlg, IDC_JPEG) == BST_CHECKED;
  appData.qualityLevel =
    (int)SendDlgItemMessage(dlg, IDC_QUALITY, CB_GETCURSEL, 0, 0);
  appData.compressLevel =
    (int)SendDlgItemMessage(dlg, IDC_COMPRESS, CB_GETCURSEL, 0, 0) - 1;

  for (i = IDC_COLOR_FULL; i <= IDC_COLOR_VERYLOW; i++)
    if (IsDlgButtonChecked(dlg, i) == BST_CHECKED)
      appData.colorLevel = i - IDC_COLOR_FULL;
}

static BOOL CALLBACK
DlgProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
  switch (msg) {

  case WM_INITDIALOG:
    if (dlgMode == DLG_CONNECT)
      SetWindowText(dlg, "TenoxVNC " TENOXVNC_VERSION " - Connect");
    SetDlgItemText(dlg, IDC_MESSAGE, dlgMessage ? dlgMessage : "");
    if (dlgMode == DLG_CONNECT)
      SetDlgItemText(dlg, IDC_SERVER, dlgHost);
    if (dlgMode != DLG_SETTINGS) {
      SendDlgItemMessage(dlg, IDC_PASSWORD, EM_LIMITTEXT, PASS_LEN, 0);
      SetDlgItemText(dlg, IDC_PASSWORD, dlgPass);
    }
    if (dlgMode != DLG_PASSWORD)
      InitOptions(dlg);
    Center(dlg);

    /* a message means the last try failed, nearly always the password */
    if (dlgMessage && dlgMode == DLG_CONNECT) {
      SetFocus(GetDlgItem(dlg, IDC_PASSWORD));
      return FALSE;
    }
    return TRUE;

  case WM_CTLCOLORSTATIC:
    if ((HWND)lParam == GetDlgItem(dlg, IDC_MESSAGE)) {
      HBRUSH brush = (HBRUSH)DefWindowProc(dlg, msg, wParam, lParam);

      SetTextColor((HDC)wParam, RGB(192, 0, 0));
      return (BOOL)brush;
    }
    return FALSE;

  case WM_COMMAND:
    switch (LOWORD(wParam)) {
    case IDOK:
      if (dlgMode == DLG_CONNECT)
	GetDlgItemText(dlg, IDC_SERVER, dlgHost, sizeof(dlgHost));
      if (dlgMode != DLG_SETTINGS)
	GetDlgItemText(dlg, IDC_PASSWORD, dlgPass, sizeof(dlgPass));
      if (dlgMode != DLG_PASSWORD)
	ReadOptions(dlg);
      EndDialog(dlg, IDOK);
      return TRUE;
    case IDCANCEL:
      EndDialog(dlg, IDCANCEL);
      return TRUE;
    case IDC_JPEG:
      EnableQuality(dlg);
      return TRUE;
    }
    break;
  }

  return FALSE;
}

static int
DlgRun(int mode, const char *message)
{
  static const int templates[] = { IDD_CONNECT, IDD_PASSWORD, IDD_SETTINGS };
  HWND owner = (w32Wnd && IsWindowVisible(w32Wnd)) ? w32Wnd : NULL;
  int res;

  if (dlgInUse)
    return IDCANCEL;

  dlgInUse = True;
  dlgMode = mode;
  dlgMessage = message;

  if (mode == DLG_CONNECT && dlgHost[0] == '\0' && vncServerHost[0] != '\0') {
    if (vncServerPort >= SERVER_PORT_OFFSET &&
	vncServerPort < SERVER_PORT_OFFSET + 100)
      sprintf(dlgHost, "%.240s:%d", vncServerHost,
	      vncServerPort - SERVER_PORT_OFFSET);
    else
      sprintf(dlgHost, "%.240s::%d", vncServerHost, vncServerPort);
  }

  res = DialogBox(w32Inst, MAKEINTRESOURCE(templates[mode]), owner, DlgProc);

  dlgInUse = False;
  return res == IDOK ? IDOK : IDCANCEL;
}

char *
DoConnectDialog(const char *message)
{
  char *name;

  for (;;) {
    if (DlgRun(DLG_CONNECT, message) != IDOK) {
      w32QuietExit = True;
      Cleanup();
      exit(1);
    }

    if (dlgHost[0] != '\0')
      break;

    message = "Enter the VNC server to connect to.";
  }

  connectDialogUsed = True;
  name = malloc(strlen(dlgHost) + 1);
  if (name)
    strcpy(name, dlgHost);
  return name;
}

void
AskForServer(const char *message)
{
  for (;;) {
    char *name = DoConnectDialog(message);
    Bool ok = name && SetServerName(name);

    free(name);
    if (ok)
      return;

    message = connErrorMsg;
  }
}

char *
DoPasswordDialog()
{
  if (connectDialogUsed)
    return dlgPass;

  if (DlgRun(DLG_PASSWORD, NULL) != IDOK) {
    w32QuietExit = True;
    Cleanup();
    exit(1);
  }

  return dlgPass;
}

void
ForgetPassword(void)
{
  memset(dlgPass, 0, sizeof(dlgPass));
  ForgetSessionPassword();
}

Bool
DoSettingsDialog(void)
{
  return DlgRun(DLG_SETTINGS, NULL) == IDOK;
}

void
CancelDialog(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
}
