// SPDX-License-Identifier: MIT

#include <windows.h>
#include <shellapi.h>
#include <strsafe.h>

#include "Resource.h"

#define TRAY_CALLBACK_MESSAGE (WM_APP + 1)
#define TRAY_ICON_ID          1
#define MENU_ID_ENABLED       1
#define MENU_ID_EXIT          2
#define MENU_ID_ABOUT         3
#define MENU_ID_NONE          0

// Kept out of Clicklet.rc: one definition for the About text, the band measurement and the launch.
#define CLICKLET_REPOSITORY_URL L"https://github.com/tabakman/clicklet"

#define INI_FILE_NAME     L"Clicklet.ini"
#define INI_SECTION       L"Mapping"

// Returned only for an absent key, so an empty "Button5=" is told apart from no line at all.
#define INI_ABSENT L"\x01"

#define DEFAULT_KEYSTROKE L"ctrl+enter"

#define KEYSTROKE_TEXT_MAX 32
#define KEY_DISPLAY_MAX    16
#define MENU_LINE_MAX      48

// 4 modifiers down, key down, key up, 4 modifiers up; AddKeyInput does not bounds-check.
#define KEYSTROKE_INPUT_MAX 10

#define INI_VALUE_MAX 96

static HINSTANCE g_instance;
static HWND  g_window;
static HWND  g_aboutDialog;
static HHOOK g_hook;
static UINT  g_taskbarCreatedMessage;
static HICON g_iconEnabled;
static HICON g_iconDisabled;
static BOOL  g_enabled      = TRUE;
static WCHAR g_iniPath[MAX_PATH];

// g_mappings[0] is XBUTTON1 and g_mappings[1] is XBUTTON2; MouseHookProc indexes them that way.
static struct Mapping
{
    int          number;
    const WCHAR *iniKey;
    INPUT        keystroke[KEYSTROKE_INPUT_MAX];
    UINT         keystrokeCount;
    WCHAR        keystrokeText[KEYSTROKE_TEXT_MAX];
    BOOL         downSwallowed;   // per button: one shared flag would let an up through orphaned
}
g_mappings[] =
{
    { 4, L"Button4" },
    { 5, L"Button5" },
};

// No LR_SHARED: a shared load ignores the requested size. These handles are ours to DestroyIcon.
static HICON LoadTrayIcon(HINSTANCE instance, int resourceId)
{
    return (HICON)LoadImageW(instance, MAKEINTRESOURCEW(resourceId), IMAGE_ICON,
                             GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                             LR_DEFAULTCOLOR);
}

static void TrayMessage(DWORD message)
{
    NOTIFYICONDATAW data;
    ZeroMemory(&data, sizeof(data));
    data.cbSize = sizeof(data);
    data.hWnd   = g_window;
    data.uID    = TRAY_ICON_ID;

    if (message != NIM_DELETE)
    {
        data.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        data.uCallbackMessage = TRAY_CALLBACK_MESSAGE;
        data.hIcon            = g_enabled ? g_iconEnabled : g_iconDisabled;

        StringCchCopyW(data.szTip, ARRAYSIZE(data.szTip),
                       g_enabled ? L"Clicklet" : L"Clicklet (Disabled)");
    }

    Shell_NotifyIconW(message, &data);
}

// Runs for every mouse event system-wide: no parsing or allocation, send the prebuilt input only.
static LRESULT CALLBACK MouseHookProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION && (wParam == WM_XBUTTONDOWN || wParam == WM_XBUTTONUP))
    {
        const MSLLHOOKSTRUCT *mouse = (const MSLLHOOKSTRUCT *)lParam;

        WORD xbutton = HIWORD(mouse->mouseData);
        struct Mapping *mapping = (xbutton == XBUTTON1) ? &g_mappings[0]
                                : (xbutton == XBUTTON2) ? &g_mappings[1]
                                : NULL;

        if (mapping != NULL && mapping->keystrokeCount != 0 &&
            (mouse->flags & LLMHF_INJECTED) == 0)
        {
            if (wParam == WM_XBUTTONDOWN)
            {
                // Not redundant: a hook that failed to uninstall must still pass input through.
                if (g_enabled)
                {
                    mapping->downSwallowed = TRUE;
                    SendInput(mapping->keystrokeCount, mapping->keystroke, sizeof(INPUT));
                    return 1;
                }
            }
            else if (mapping->downSwallowed)
            {
                mapping->downSwallowed = FALSE;
                return 1;
            }
        }
    }

    return CallNextHookEx(NULL, code, wParam, lParam);
}

// Message-loop thread only: the system runs a WH_MOUSE_LL hook on the thread that installed it.
static BOOL InstallHook(void)
{
    if (g_hook == NULL)
        g_hook = SetWindowsHookExW(WH_MOUSE_LL, MouseHookProc, NULL, 0);
    return g_hook != NULL;
}

static void UninstallHook(void)
{
    // Keep the handle if unhooking failed, or InstallHook would stack a second hook on it.
    if (g_hook != NULL && UnhookWindowsHookEx(g_hook))
        g_hook = NULL;

    // Clear every flag: unobserved, a stale TRUE swallows a real up and leaves the button down.
    for (int i = 0; i < (int)ARRAYSIZE(g_mappings); ++i)
        g_mappings[i].downSwallowed = FALSE;
}

static void SetEnabled(BOOL enabled)
{
    if (enabled && !InstallHook())
        enabled = FALSE;
    if (!enabled)
        UninstallHook();

    g_enabled = enabled;
    TrayMessage(NIM_MODIFY);
}

#define CLICKLET_LINK_COLOUR RGB(0, 140, 252)

// Dialog units of IDD_ABOUT. Keep the band this tall: a text-high target was hard to click.
#define CLICKLET_LINK_BAND_TOP_DLU      77
#define CLICKLET_LINK_BAND_BOTTOM_DLU   100
#define CLICKLET_LINK_BAND_PADDING_DLU  8

// Computed once in WM_INITDIALOG so each mouse move on the hook thread costs only a PtInRect.
static RECT g_aboutLinkBand;

static void ComputeAboutLinkBand(HWND dialog)
{
    RECT band = { 0, CLICKLET_LINK_BAND_TOP_DLU,
                  CLICKLET_LINK_BAND_PADDING_DLU, CLICKLET_LINK_BAND_BOTTOM_DLU };
    RECT client;
    if (!MapDialogRect(dialog, &band) || !GetClientRect(dialog, &client))
    {
        SetRectEmpty(&g_aboutLinkBand);
        return;
    }
    LONG padding = band.right;  // x 0 maps to 0, so the mapped right edge is the padding width

    band.left  = client.left;
    band.right = client.right;

    HWND link = GetDlgItem(dialog, IDC_ABOUT_URL);
    RECT linkRect;
    POINT linkTopLeft = { 0, 0 };
    if (link != NULL && GetClientRect(link, &linkRect) && ClientToScreen(link, &linkTopLeft) &&
        ScreenToClient(dialog, &linkTopLeft))
    {
        HDC dc = GetDC(link);
        if (dc != NULL)
        {
            HFONT   font     = (HFONT)SendMessageW(link, WM_GETFONT, 0, 0);
            HGDIOBJ previous = (font != NULL) ? SelectObject(dc, font) : NULL;
            RECT    text     = { 0, 0, 0, 0 };
            int     height   = DrawTextW(dc, CLICKLET_REPOSITORY_URL, -1, &text,
                                         DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
            if (previous != NULL)
                SelectObject(dc, previous);
            ReleaseDC(link, dc);

            LONG textWidth = text.right - text.left;
            LONG linkWidth = linkRect.right - linkRect.left;
            if (height != 0 && textWidth > 0)
            {
                LONG textLeft = linkTopLeft.x + (linkWidth - textWidth) / 2;
                band.left  = max(client.left, textLeft - padding);
                band.right = min(client.right, textLeft + textWidth + padding);
            }
        }
    }

    g_aboutLinkBand = band;
}

// The one hit test for both the hand cursor and the click, so the two can never disagree.
static BOOL PointIsOnAboutLink(POINT client)
{
    return PtInRect(&g_aboutLinkBand, client);
}

// Off the hook thread: blocking it past LowLevelHooksTimeout gets the hook silently removed.
static DWORD WINAPI OpenRepositoryThread(LPVOID unused)
{
    UNREFERENCED_PARAMETER(unused);

    SHELLEXECUTEINFOW execute;
    ZeroMemory(&execute, sizeof(execute));
    execute.cbSize = sizeof(execute);
    execute.fMask  = SEE_MASK_NOASYNC;  // required: this thread has no message loop
    execute.hwnd   = NULL;              // not the dialog: it may be destroyed while this runs
    execute.lpVerb = L"open";
    execute.lpFile = CLICKLET_REPOSITORY_URL;
    execute.nShow  = SW_SHOWNORMAL;
    ShellExecuteExW(&execute);
    return 0;
}

static void OpenRepository(void)
{
    HANDLE thread = CreateThread(NULL, 0, OpenRepositoryThread, NULL, 0, NULL);
    if (thread != NULL)
        CloseHandle(thread);
}

static INT_PTR CALLBACK AboutDialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
    {
        WCHAR version[64];
        StringCchPrintfW(version, ARRAYSIZE(version), L"Clicklet %d.%d.%d",
                         CLICKLET_VERSION_MAJOR, CLICKLET_VERSION_MINOR, CLICKLET_VERSION_PATCH);
        SetDlgItemTextW(dialog, IDC_ABOUT_VERSION, version);
        SetDlgItemTextW(dialog, IDC_ABOUT_URL, CLICKLET_REPOSITORY_URL);
        ComputeAboutLinkBand(dialog);

        // LR_SHARED, unlike the tray icons: the system owns this bitmap, so it is never freed.
        HANDLE logo = LoadImageW(g_instance, MAKEINTRESOURCEW(IDB_ABOUT_LOGO), IMAGE_BITMAP,
                                 0, 0, LR_SHARED | LR_DEFAULTCOLOR);
        if (logo != NULL)
        {
            SendDlgItemMessageW(dialog, IDC_ABOUT_LOGO, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)logo);
        }

        // Focus the dialog itself: there is no control to focus, and SetFocus(NULL) kills Esc.
        SetFocus(dialog);
        return FALSE;
    }

    case WM_CTLCOLORSTATIC:
        if ((HWND)lParam == GetDlgItem(dialog, IDC_ABOUT_URL))
        {
            SetTextColor((HDC)wParam, CLICKLET_LINK_COLOUR);
            SetBkMode((HDC)wParam, TRANSPARENT);
            return (INT_PTR)GetSysColorBrush(COLOR_3DFACE);
        }
        return FALSE;

    // A dialog procedure returns this message's result through DWLP_MSGRESULT.
    case WM_SETCURSOR:
        if ((HWND)wParam == dialog && LOWORD(lParam) == HTCLIENT)
        {
            POINT cursor;
            if (GetCursorPos(&cursor) && ScreenToClient(dialog, &cursor) &&
                PointIsOnAboutLink(cursor))
            {
                SetCursor(LoadCursorW(NULL, IDC_HAND));
                SetWindowLongPtrW(dialog, DWLP_MSGRESULT, TRUE);
                return TRUE;
            }
        }
        return FALSE;

    // On press, so a small drag cannot cancel it. No WM_LBUTTONDBLCLK: a double-click opens once.
    case WM_LBUTTONDOWN:
    {
        POINT click = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
        if (PointIsOnAboutLink(click))
        {
            OpenRepository();
            return TRUE;
        }
        return FALSE;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        // Esc sends IDCANCEL and Enter IDOK even though no such buttons exist.
        case IDOK:
        case IDCANCEL:
            DestroyWindow(dialog);  // modeless: EndDialog would not destroy it
            return TRUE;

        default:
            return FALSE;
        }

    case WM_CLOSE:
        DestroyWindow(dialog);
        return TRUE;

    case WM_DESTROY:
        g_aboutDialog = NULL;
        return FALSE;

    default:
        break;
    }

    return FALSE;
}

static void ShowAboutDialog(void)
{
    if (g_aboutDialog != NULL)
    {
        SetForegroundWindow(g_aboutDialog);
        return;
    }

    // Modeless: a modal DialogBox would disable its owner, the hidden tray window.
    g_aboutDialog = CreateDialogParamW(g_instance, MAKEINTRESOURCEW(IDD_ABOUT), g_window,
                                       AboutDialogProc, 0);

    if (g_aboutDialog != NULL)
        SetForegroundWindow(g_aboutDialog);
}

static BOOL g_trayMenuOpen;           // no nested menu from a notification inside TrackPopupMenu
static BOOL g_trayUpEndsDoubleClick;  // DOWN, UP, DBLCLK, UP: the second UP must not reopen

static void ShowTrayMenu(void)
{
    if (g_trayMenuOpen)
        return;

    HMENU menu = CreatePopupMenu();
    if (menu == NULL)
        return;

    for (int i = 0; i < (int)ARRAYSIZE(g_mappings); ++i)
    {
        if (g_mappings[i].keystrokeCount == 0)
            continue;
        WCHAR line[MENU_LINE_MAX];
        // Keep the arrow as \u2192: this file must stay pure ASCII for the compiler's code page.
        StringCchPrintfW(line, ARRAYSIZE(line), L"Mouse %d \u2192 %s",
                         g_mappings[i].number, g_mappings[i].keystrokeText);
        AppendMenuW(menu, MF_STRING | MF_GRAYED, MENU_ID_NONE, line);
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);

    AppendMenuW(menu, MF_STRING | (g_enabled ? MF_CHECKED : MF_UNCHECKED),
                MENU_ID_ENABLED, L"Enabled");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, MENU_ID_ABOUT, L"About");
    AppendMenuW(menu, MF_STRING, MENU_ID_EXIT, L"Exit");

    POINT cursor;
    GetCursorPos(&cursor);
    SetForegroundWindow(g_window);
    g_trayMenuOpen = TRUE;
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, cursor.x, cursor.y, 0, g_window, NULL);
    g_trayMenuOpen = FALSE;
    PostMessageW(g_window, WM_NULL, 0, 0);  // lets the menu dismiss properly
    DestroyMenu(menu);
}

static void OnTrayNotification(UINT mouseMessage)
{
    switch (mouseMessage)
    {
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
        g_trayUpEndsDoubleClick = FALSE;
        break;

    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDBLCLK:
        g_trayUpEndsDoubleClick = TRUE;
        break;

    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
        if (g_trayUpEndsDoubleClick)
            g_trayUpEndsDoubleClick = FALSE;
        else
            ShowTrayMenu();
        break;

    default:
        break;
    }
}

static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case TRAY_CALLBACK_MESSAGE:
        OnTrayNotification(LOWORD(lParam));
        return 0;

    case WM_COMMAND:
        if (LOWORD(wParam) == MENU_ID_ENABLED)
            SetEnabled(!g_enabled);
        else if (LOWORD(wParam) == MENU_ID_ABOUT)
            ShowAboutDialog();
        else if (LOWORD(wParam) == MENU_ID_EXIT)
            DestroyWindow(window);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }

    if (g_taskbarCreatedMessage != 0 && message == g_taskbarCreatedMessage)
    {
        TrayMessage(NIM_ADD);
        return 0;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

// Array order is press order; release is the reverse, whatever order the ini wrote them in.
static const struct
{
    const WCHAR *display;
    WORD         vk;
    DWORD        flags;
}
kModifiers[] =
{
    { L"Ctrl",  VK_CONTROL, 0 },
    { L"Alt",   VK_MENU,    0 },
    { L"Shift", VK_SHIFT,   0 },
    { L"Win",   VK_LWIN,    KEYEVENTF_EXTENDEDKEY },
};

static const struct
{
    const WCHAR *name;
    int          modifier;
}
kModifierNames[] =
{
    { L"ctrl",    0 }, { L"control", 0 },
    { L"alt",     1 },
    { L"shift",   2 },
    { L"win",     3 }, { L"windows", 3 },
};

// KEYEVENTF_EXTENDEDKEY on E0 keys, or apps reading scan codes see the numeric keypad key.
static const struct
{
    const WCHAR *name;
    const WCHAR *display;
    WORD         vk;
    DWORD        flags;
}
kNamedKeys[] =
{
    { L"enter",     L"Enter",     VK_RETURN, 0 },
    { L"return",    L"Enter",     VK_RETURN, 0 },
    { L"tab",       L"Tab",       VK_TAB,    0 },
    { L"escape",    L"Esc",       VK_ESCAPE, 0 },
    { L"esc",       L"Esc",       VK_ESCAPE, 0 },
    { L"space",     L"Space",     VK_SPACE,  0 },
    { L"backspace", L"Backspace", VK_BACK,   0 },
    { L"delete",    L"Delete",    VK_DELETE, KEYEVENTF_EXTENDEDKEY },
    { L"del",       L"Delete",    VK_DELETE, KEYEVENTF_EXTENDEDKEY },
    { L"insert",    L"Insert",    VK_INSERT, KEYEVENTF_EXTENDEDKEY },
    { L"ins",       L"Insert",    VK_INSERT, KEYEVENTF_EXTENDEDKEY },
    { L"home",      L"Home",      VK_HOME,   KEYEVENTF_EXTENDEDKEY },
    { L"end",       L"End",       VK_END,    KEYEVENTF_EXTENDEDKEY },
    { L"pageup",    L"PageUp",    VK_PRIOR,  KEYEVENTF_EXTENDEDKEY },
    { L"pgup",      L"PageUp",    VK_PRIOR,  KEYEVENTF_EXTENDEDKEY },
    { L"pagedown",  L"PageDown",  VK_NEXT,   KEYEVENTF_EXTENDEDKEY },
    { L"pgdn",      L"PageDown",  VK_NEXT,   KEYEVENTF_EXTENDEDKEY },
    { L"up",        L"Up",        VK_UP,     KEYEVENTF_EXTENDEDKEY },
    { L"down",      L"Down",      VK_DOWN,   KEYEVENTF_EXTENDEDKEY },
    { L"left",      L"Left",      VK_LEFT,   KEYEVENTF_EXTENDEDKEY },
    { L"right",     L"Right",     VK_RIGHT,  KEYEVENTF_EXTENDEDKEY },
};

// Ordinal, not lstrcmpiW: a locale-aware compare breaks "i"/"I" under some user locales.
static BOOL TokenEquals(const WCHAR *token, int length, const WCHAR *name)
{
    return CompareStringOrdinal(token, length, name, -1, TRUE) == CSTR_EQUAL;
}

static int FindModifier(const WCHAR *token, int length)
{
    for (int i = 0; i < (int)ARRAYSIZE(kModifierNames); ++i)
        if (TokenEquals(token, length, kModifierNames[i].name))
            return kModifierNames[i].modifier;
    return -1;
}

static BOOL ResolveMainKey(const WCHAR *token, int length, WORD *vk, DWORD *flags,
                           WCHAR *display)
{
    for (int i = 0; i < (int)ARRAYSIZE(kNamedKeys); ++i)
    {
        if (TokenEquals(token, length, kNamedKeys[i].name))
        {
            *vk    = kNamedKeys[i].vk;
            *flags = kNamedKeys[i].flags;
            StringCchCopyW(display, KEY_DISPLAY_MAX, kNamedKeys[i].display);
            return TRUE;
        }
    }

    if (length == 1)
    {
        WCHAR c = token[0];
        if (c >= L'a' && c <= L'z')
            c = (WCHAR)(c - L'a' + L'A');
        if ((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9'))
        {
            *vk        = (WORD)c;
            *flags     = 0;
            display[0] = c;
            display[1] = L'\0';
            return TRUE;
        }
        return FALSE;
    }

    if ((token[0] == L'f' || token[0] == L'F') && length >= 2 && length <= 3)
    {
        int number = 0;
        for (int i = 1; i < length; ++i)
        {
            if (token[i] < L'0' || token[i] > L'9')
                return FALSE;
            number = number * 10 + (token[i] - L'0');
        }
        if (number >= 1 && number <= 24)
        {
            *vk    = (WORD)(VK_F1 + number - 1);
            *flags = 0;
            StringCchPrintfW(display, KEY_DISPLAY_MAX, L"F%d", number);
            return TRUE;
        }
    }

    return FALSE;
}

static void AddKeyInput(struct Mapping *mapping, WORD vk, DWORD flags, BOOL up)
{
    INPUT *input = &mapping->keystroke[mapping->keystrokeCount++];
    ZeroMemory(input, sizeof(*input));
    input->type     = INPUT_KEYBOARD;
    input->ki.wVk   = vk;
    input->ki.wScan = (WORD)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    input->ki.dwFlags = flags | (up ? KEYEVENTF_KEYUP : 0u);
}

static BOOL ParseKeystroke(struct Mapping *mapping, const WCHAR *text)
{
    int   modifiers = 0;
    WORD  mainVk    = 0;
    DWORD mainFlags = 0;
    WCHAR mainDisplay[KEY_DISPLAY_MAX];

    const WCHAR *p = text;
    for (;;)
    {
        const WCHAR *start = p;
        while (*p != L'\0' && *p != L'+')
            ++p;

        const WCHAR *end = p;
        while (start < end && (*start == L' ' || *start == L'\t'))
            ++start;
        while (end > start && (end[-1] == L' ' || end[-1] == L'\t'))
            --end;

        int length = (int)(end - start);
        if (length == 0)
            return FALSE;

        int modifier = FindModifier(start, length);
        if (modifier >= 0)
        {
            if (mainVk != 0 || (modifiers & (1 << modifier)) != 0)
                return FALSE;
            modifiers |= 1 << modifier;
        }
        else
        {
            if (mainVk != 0)
                return FALSE;
            if (!ResolveMainKey(start, length, &mainVk, &mainFlags, mainDisplay))
                return FALSE;
        }

        if (*p == L'\0')
            break;
        ++p;
    }

    if (mainVk == 0)
        return FALSE;

    mapping->keystrokeCount = 0;
    for (int i = 0; i < (int)ARRAYSIZE(kModifiers); ++i)
        if ((modifiers & (1 << i)) != 0)
            AddKeyInput(mapping, kModifiers[i].vk, kModifiers[i].flags, FALSE);
    AddKeyInput(mapping, mainVk, mainFlags, FALSE);
    AddKeyInput(mapping, mainVk, mainFlags, TRUE);
    for (int i = (int)ARRAYSIZE(kModifiers) - 1; i >= 0; --i)
        if ((modifiers & (1 << i)) != 0)
            AddKeyInput(mapping, kModifiers[i].vk, kModifiers[i].flags, TRUE);

    WCHAR *shown = mapping->keystrokeText;
    size_t shownSize = ARRAYSIZE(mapping->keystrokeText);
    shown[0] = L'\0';
    for (int i = 0; i < (int)ARRAYSIZE(kModifiers); ++i)
    {
        if ((modifiers & (1 << i)) != 0)
        {
            StringCchCatW(shown, shownSize, kModifiers[i].display);
            StringCchCatW(shown, shownSize, L"+");
        }
    }
    StringCchCatW(shown, shownSize, mainDisplay);
    return TRUE;
}

// Must be absolute: given a bare file name the profile API searches the Windows directory.
static BOOL BuildIniPath(void)
{
    DWORD length = GetModuleFileNameW(NULL, g_iniPath, ARRAYSIZE(g_iniPath));

    if (length == 0 || length >= ARRAYSIZE(g_iniPath))
        return FALSE;

    while (length > 0 && g_iniPath[length - 1] != L'\\' && g_iniPath[length - 1] != L'/')
        --length;
    if (length == 0)
        return FALSE;

    g_iniPath[length] = L'\0';
    return SUCCEEDED(StringCchCatW(g_iniPath, ARRAYSIZE(g_iniPath), INI_FILE_NAME));
}

static BOOL ReadSetting(const WCHAR *key, WCHAR *value, DWORD size)
{
    GetPrivateProfileStringW(INI_SECTION, key, INI_ABSENT, value, size, g_iniPath);
    if (TokenEquals(value, -1, INI_ABSENT))
        return FALSE;

    WCHAR *start = value;
    while (*start == L' ' || *start == L'\t')
        ++start;
    WCHAR *end = start + lstrlenW(start);
    while (end > start && (end[-1] == L' ' || end[-1] == L'\t'))
        --end;
    *end = L'\0';
    if (start != value)
        MoveMemory(value, start, (size_t)(end - start + 1) * sizeof(WCHAR));
    return TRUE;
}

static BOOL CheckForUnknownSettings(WCHAR *problem, size_t problemSize)
{
    WCHAR names[512];
    DWORD length = GetPrivateProfileStringW(INI_SECTION, NULL, L"", names,
                                            ARRAYSIZE(names), g_iniPath);
    // size - 2 is the documented truncation result when listing key names.
    if (length >= ARRAYSIZE(names) - 2)
    {
        StringCchPrintfW(problem, problemSize,
                         INI_FILE_NAME L": the [%s] section is too long to read.",
                         INI_SECTION);
        return FALSE;
    }

    for (const WCHAR *name = names; *name != L'\0'; name += lstrlenW(name) + 1)
    {
        BOOL known = FALSE;
        for (int i = 0; i < (int)ARRAYSIZE(g_mappings); ++i)
            if (TokenEquals(name, -1, g_mappings[i].iniKey))
                known = TRUE;
        if (!known)
        {
            StringCchPrintfW(problem, problemSize,
                             INI_FILE_NAME L": \"%s\" is not a valid setting.", name);
            return FALSE;
        }
    }
    return TRUE;
}

// The profile API reads an absent and an empty file alike; only "not found" may mean absent.
static BOOL IniFileExists(void)
{
    if (GetFileAttributesW(g_iniPath) != INVALID_FILE_ATTRIBUTES)
        return TRUE;
    DWORD error = GetLastError();
    return error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND;
}

// Defaults apply only when the file is absent; a present file maps exactly the lines it has.
static BOOL LoadConfiguration(WCHAR *problem, size_t problemSize)
{
    if (!IniFileExists())
    {
        if (ParseKeystroke(&g_mappings[1], DEFAULT_KEYSTROKE))
            return TRUE;
        StringCchCopyW(problem, problemSize, L"Clicklet's default keystroke is invalid.");
        return FALSE;
    }

    if (!CheckForUnknownSettings(problem, problemSize))
        return FALSE;

    WCHAR value[INI_VALUE_MAX];
    BOOL  mappedAny = FALSE;

    for (int i = 0; i < (int)ARRAYSIZE(g_mappings); ++i)
    {
        if (!ReadSetting(g_mappings[i].iniKey, value, ARRAYSIZE(value)))
            continue;

        if (!ParseKeystroke(&g_mappings[i], value))
        {
            StringCchPrintfW(problem, problemSize,
                             INI_FILE_NAME L": \"%s\" is not a valid keystroke for %s.",
                             value, g_mappings[i].iniKey);
            return FALSE;
        }
        mappedAny = TRUE;
    }

    if (!mappedAny)
    {
        StringCchCopyW(problem, problemSize,
                       INI_FILE_NAME L": no Button4 or Button5 under [" INI_SECTION L"].");
        return FALSE;
    }
    return TRUE;
}

static void ShowStartupRefusal(const WCHAR *text)
{
    MessageBoxW(NULL, text, L"Clicklet", MB_OK | MB_ICONWARNING);
}

static void ReportBadConfiguration(const WCHAR *problem)
{
    WCHAR text[512];
    StringCchPrintfW(text, ARRAYSIZE(text),
                     L"%s\n\nSee README.md, or delete the file to use the defaults.", problem);
    ShowStartupRefusal(text);
}

static void ReportCommandLineArgument(void)
{
    ShowStartupRefusal(L"Clicklet doesn't take arguments. Use " INI_FILE_NAME
                       L" beside Clicklet.exe.\n\n"
                       L"Remove the argument from the shortcut.");
}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE previousInstance,
                      LPWSTR commandLine, int showCommand)
{
    UNREFERENCED_PARAMETER(previousInstance);
    UNREFERENCED_PARAMETER(showCommand);

    g_instance = instance;

    if (!BuildIniPath())
    {
        ShowStartupRefusal(L"Clicklet did not start, because it could not work out its own "
                           L"folder and therefore could not look for Clicklet.ini.");
        return 2;
    }

    const WCHAR *argument = commandLine;
    while (*argument == L' ' || *argument == L'\t')
        ++argument;
    if (*argument != L'\0')
    {
        ReportCommandLineArgument();
        return 2;
    }

    WCHAR problem[256];
    if (!LoadConfiguration(problem, ARRAYSIZE(problem)))
    {
        ReportBadConfiguration(problem);
        return 2;
    }

    HANDLE instanceLock = CreateMutexW(NULL, TRUE, L"Local\\Clicklet.SingleInstance");
    if (instanceLock == NULL)
        return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS)
        return 0;

    g_iconEnabled           = LoadTrayIcon(instance, IDI_TRAY_ENABLED);
    g_iconDisabled          = LoadTrayIcon(instance, IDI_TRAY_DISABLED);
    g_taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW windowClass;
    ZeroMemory(&windowClass, sizeof(windowClass));
    windowClass.cbSize        = sizeof(windowClass);
    windowClass.lpfnWndProc   = WindowProc;
    windowClass.hInstance     = instance;
    windowClass.lpszClassName = L"ClickletWindow";
    if (RegisterClassExW(&windowClass) == 0)
        return 1;

    // Top-level, not HWND_MESSAGE: message-only windows miss the TaskbarCreated broadcast.
    g_window = CreateWindowExW(WS_EX_TOOLWINDOW, windowClass.lpszClassName, L"Clicklet",
                               WS_POPUP, 0, 0, 0, 0, NULL, NULL, instance, NULL);
    if (g_window == NULL)
        return 1;

    if (!InstallHook())
    {
        DestroyWindow(g_window);
        return 1;
    }

    TrayMessage(NIM_ADD);

    MSG message;
    while (GetMessageW(&message, NULL, 0, 0) > 0)
    {
        // A modeless dialog gets Esc and Enter only through this call.
        if (g_aboutDialog != NULL && IsDialogMessageW(g_aboutDialog, &message))
            continue;

        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    TrayMessage(NIM_DELETE);
    UninstallHook();
    if (g_iconEnabled != NULL)
        DestroyIcon(g_iconEnabled);
    if (g_iconDisabled != NULL)
        DestroyIcon(g_iconDisabled);
    CloseHandle(instanceLock);
    return 0;
}
