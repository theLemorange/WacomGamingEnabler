#include <windows.h>
#include <string>
#include <shellapi.h> // Required for System Tray Icons
#include <tchar.h>    // Required for the TEXT() macro
#include <CommCtrl.h> // Required for Trackbar (Slider) control
#include <dwmapi.h>   // Required for DWM functions
#include "resource.h"

#pragma comment(lib, "ComCtl32.lib") // Link against the Common Controls library
#pragma comment(lib, "Dwmapi.lib")   // Link against the DWM library

// Wacom Wintab API headers
#include "WintabUtils.h"
#define PACKETDATA (PK_X | PK_Y | PK_Z)
#define PACKETMODE 0
#include "pktdef.h"

// --- Global Variables ---
HINSTANCE      g_hInst = NULL;
const TCHAR* g_szSettingsClass = TEXT("WACOM_SETTINGS_WINDOW");
HCTX           g_tabCtx = NULL;
HWND           g_hSettingsDlg = NULL; // Handle to the settings window (now our main window)
HWINEVENTHOOK  g_hWinEventHook = NULL; // Handle to the system event hook
HBRUSH         g_hbrBackground = NULL;     // Brush for the dark background
HBRUSH         g_hbrEditBackground = NULL; // Brush for the dark edit box background

// --- User-configurable Settings ---
int            g_iProximityThreshold = 800;
float          g_fMouseSpeedMultiplier = 1.0f;

// --- Defines for Controls, Menu, and Timers ---
#define WM_TRAYICON         (WM_USER + 1)
#define ID_TRAY_ICON        101
#define ID_EXIT_MENU        102
#define ID_SHOW_WINDOW_MENU 103
#define REASSERT_TIMER_ID   201 // One-shot timer for delayed reassertion

#define IDC_HEIGHT_SLIDER   1001
#define IDC_HEIGHT_LABEL    1002
#define IDC_SPEED_SLIDER    1003
#define IDC_SPEED_EDIT      1004
#define IDC_EXIT_BUTTON     1005

// --- Forward Declarations ---
ATOM             MyRegisterClass(HINSTANCE hInstance);
BOOL             InitInstance(HINSTANCE, int);
LRESULT CALLBACK SettingsDlgProc(HWND, UINT, WPARAM, LPARAM);
VOID CALLBACK    WinEventProc(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD);
HCTX             InitWintabAPI(HWND hWnd);
void             Cleanup();
void             ProcessPenPacket(const PACKET& packet);
void             CreateTrayIcon(HWND hWnd);
void             ShowContextMenu(HWND hWnd);

/// @brief Main entry point for the application.
int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR    lpCmdLine,
    _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    InitCommonControls();
    MyRegisterClass(hInstance);

    if (!InitInstance(hInstance, nCmdShow))
    {
        return FALSE;
    }

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        if (g_hSettingsDlg == NULL || !IsDialogMessage(g_hSettingsDlg, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    Cleanup();
    return (int)msg.wParam;
}

/// @brief Registers the window class for the settings dialog.
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEX settings_wcex = { 0 };
    settings_wcex.cbSize = sizeof(WNDCLASSEX);
    settings_wcex.lpfnWndProc = SettingsDlgProc;
    settings_wcex.hInstance = hInstance;
    settings_wcex.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    // Create a dark brush for the window background
    g_hbrBackground = CreateSolidBrush(RGB(45, 45, 48));
    settings_wcex.hbrBackground = g_hbrBackground;
    settings_wcex.lpszClassName = g_szSettingsClass;
    return RegisterClassEx(&settings_wcex);
}

/// @brief Creates and shows the main application window (the settings dialog).
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    g_hInst = hInstance;

    // Adjusted window size to be bigger
    g_hSettingsDlg = CreateWindowEx(
        WS_EX_PALETTEWINDOW,
        g_szSettingsClass,
        TEXT("WacomGamingEnabler"),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT,
        410, 280, // <-- Changed from 360, 250
        NULL,
        NULL,
        g_hInst,
        NULL);

    if (!g_hSettingsDlg)
    {
        return FALSE;
    }

    // Enable dark mode for the title bar
    BOOL value = TRUE;
    ::DwmSetWindowAttribute(g_hSettingsDlg, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value));

    ShowWindow(g_hSettingsDlg, nCmdShow);
    UpdateWindow(g_hSettingsDlg);

    return TRUE;
}

/// @brief Processes messages for the settings window, now the main window.
LRESULT CALLBACK SettingsDlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    static HWND hHeightSlider, hHeightValue, hSpeedSlider, hSpeedEdit, hInfoLabel, hExitButton;
    static HWND hGroupHeight, hGroupSpeed;
    static HFONT hFont;
    static bool bIsUpdatingControls = false; // Re-entrancy guard

    switch (message)
    {
    case WM_CREATE:
    {
        CreateTrayIcon(hWnd);
        g_tabCtx = InitWintabAPI(hWnd);
        if (g_tabCtx)
        {
            g_hWinEventHook = SetWinEventHook(
                EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
                NULL, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);
        }
        else
        {
            MessageBox(hWnd, TEXT("Could not initialize Wintab."), TEXT("Error"), MB_OK | MB_ICONERROR);
            DestroyWindow(hWnd);
        }

        hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        // --- All controls below are repositioned/resized for the new window size ---

        hInfoLabel = CreateWindow(TEXT("STATIC"), TEXT("This window will close to the system tray."), WS_CHILD | WS_VISIBLE | SS_CENTER, 10, 10, 375, 20, hWnd, NULL, g_hInst, NULL);
        hGroupHeight = CreateWindow(TEXT("BUTTON"), TEXT("Height / Proximity"), WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 10, 40, 375, 65, hWnd, NULL, g_hInst, NULL);
        hHeightSlider = CreateWindow(TRACKBAR_CLASS, NULL, WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS, 20, 60, 295, 30, hWnd, (HMENU)IDC_HEIGHT_SLIDER, g_hInst, NULL);
        SendMessage(hHeightSlider, TBM_SETRANGE, (WPARAM)TRUE, (LPARAM)MAKELONG(10, 1024));
        SendMessage(hHeightSlider, TBM_SETPOS, (WPARAM)TRUE, (LPARAM)g_iProximityThreshold);

        TCHAR szHeight[10];
        _stprintf_s(szHeight, TEXT("%d"), g_iProximityThreshold);
        hHeightValue = CreateWindow(TEXT("STATIC"), szHeight, WS_CHILD | WS_VISIBLE | SS_LEFT, 325, 65, 40, 20, hWnd, (HMENU)IDC_HEIGHT_LABEL, g_hInst, NULL);

        hGroupSpeed = CreateWindow(TEXT("BUTTON"), TEXT("Speed Multiplier"), WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 10, 115, 375, 65, hWnd, NULL, g_hInst, NULL);
        hSpeedSlider = CreateWindow(TRACKBAR_CLASS, NULL, WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS, 20, 135, 265, 30, hWnd, (HMENU)IDC_SPEED_SLIDER, g_hInst, NULL);
        SendMessage(hSpeedSlider, TBM_SETRANGE, (WPARAM)TRUE, (LPARAM)MAKELONG(1, 100));

        TCHAR szSpeed[10];
        _stprintf_s(szSpeed, TEXT("%.2f"), g_fMouseSpeedMultiplier);
        hSpeedEdit = CreateWindow(TEXT("EDIT"), szSpeed, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 295, 140, 80, 20, hWnd, (HMENU)IDC_SPEED_EDIT, g_hInst, NULL);

        // Exit button y-coordinate adjusted for more padding
        hExitButton = CreateWindow(TEXT("BUTTON"), TEXT("Exit Application"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW, 280, 200, 100, 25, hWnd, (HMENU)IDC_EXIT_BUTTON, g_hInst, NULL);

        SendMessage(hInfoLabel, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(hGroupHeight, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(hHeightSlider, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(hHeightValue, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(hGroupSpeed, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(hSpeedSlider, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(hSpeedEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(hExitButton, WM_SETFONT, (WPARAM)hFont, TRUE);

        {
            TCHAR szInitialSpeed[10];
            GetWindowText(hSpeedEdit, szInitialSpeed, 10);
            g_fMouseSpeedMultiplier = _ttof(szInitialSpeed);
            int iFinalPos = (int)(g_fMouseSpeedMultiplier * 10.0f);
            SendMessage(hSpeedSlider, TBM_SETPOS, TRUE, (LPARAM)iFinalPos);
        }
    }
    break;

    case WM_CTLCOLORSTATIC:
    {
        HDC hdcStatic = (HDC)wParam;
        SetTextColor(hdcStatic, RGB(220, 220, 220)); // Lighter text
        SetBkMode(hdcStatic, TRANSPARENT); // Make background transparent
        return (INT_PTR)g_hbrBackground;
    }

    case WM_CTLCOLOREDIT:
    {
        if (g_hbrEditBackground == NULL)
        {
            g_hbrEditBackground = CreateSolidBrush(RGB(60, 60, 63)); // Darker edit box background
        }
        HDC hdcEdit = (HDC)wParam;
        SetTextColor(hdcEdit, RGB(220, 220, 220)); // Lighter text
        SetBkColor(hdcEdit, RGB(60, 60, 63));       // Dark background
        return (INT_PTR)g_hbrEditBackground;
    }
    case WM_DRAWITEM:
    {
        LPDRAWITEMSTRUCT lpdis = (LPDRAWITEMSTRUCT)lParam;
        if (lpdis->CtlID == IDC_EXIT_BUTTON) {
            // Custom draw the exit button
            HDC hdc = lpdis->hDC;
            RECT rc = lpdis->rcItem;
            UINT state = lpdis->itemState;
            TCHAR buttonText[256];
            GetWindowText(lpdis->hwndItem, buttonText, 256);

            // Draw button background
            HBRUSH hBrush;
            if (state & ODS_SELECTED) {
                hBrush = CreateSolidBrush(RGB(80, 80, 80));
            }
            else {
                hBrush = CreateSolidBrush(RGB(60, 60, 60));
            }
            FillRect(hdc, &rc, hBrush);
            DeleteObject(hBrush);

            // Draw button text
            SetTextColor(hdc, RGB(220, 220, 220));
            SetBkMode(hdc, TRANSPARENT);
            DrawText(hdc, buttonText, -1, &rc, DT_CENTER | DT_SINGLELINE | DT_VCENTER);

            return TRUE;
        }
    }
    break;
    case WM_TIMER:
        if (wParam == REASSERT_TIMER_ID)
        {
            KillTimer(hWnd, REASSERT_TIMER_ID);
            if (g_tabCtx) { gpWTOverlap(g_tabCtx, TRUE); }
        }
        break;

    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP) { ShowContextMenu(hWnd); }
        else if (lParam == WM_LBUTTONDBLCLK) { ShowWindow(hWnd, SW_SHOW); SetForegroundWindow(hWnd); }
        break;

    case WT_PACKET:
    {
        PACKET packet;
        if (gpWTPacket((HCTX)lParam, (UINT)wParam, &packet)) { ProcessPenPacket(packet); }
    }
    break;

    case WM_NOTIFY:
    {
        LPNMHDR pnmh = (LPNMHDR)lParam;
        if (pnmh->code == NM_CUSTOMDRAW)
        {
            LPNMCUSTOMDRAW pnmcd = (LPNMCUSTOMDRAW)lParam;
            if (pnmcd->hdr.hwndFrom == hHeightSlider || pnmcd->hdr.hwndFrom == hSpeedSlider)
            {
                if (pnmcd->dwDrawStage == CDDS_PREPAINT)
                {
                    return CDRF_NOTIFYITEMDRAW;
                }
                else if (pnmcd->dwDrawStage == CDDS_ITEMPREPAINT)
                {
                    if (pnmcd->dwItemSpec == TBCD_CHANNEL)
                    {
                        // Draw the channel
                        HBRUSH hBrush = CreateSolidBrush(RGB(60, 60, 63));
                        FillRect(pnmcd->hdc, &pnmcd->rc, hBrush);
                        DeleteObject(hBrush);
                        return CDRF_SKIPDEFAULT;
                    }
                    else if (pnmcd->dwItemSpec == TBCD_THUMB)
                    {
                        // Draw the thumb
                        HBRUSH hBrush = CreateSolidBrush(RGB(150, 150, 150));
                        FillRect(pnmcd->hdc, &pnmcd->rc, hBrush);
                        DeleteObject(hBrush);
                        return CDRF_SKIPDEFAULT;
                    }
                }
            }
        }
    }
    break;

    case WM_HSCROLL:
    {
        if (bIsUpdatingControls) break;
        bIsUpdatingControls = true;

        if ((HWND)lParam == hHeightSlider)
        {
            g_iProximityThreshold = SendMessage(hHeightSlider, TBM_GETPOS, 0, 0);
            TCHAR szHeight[10];
            _stprintf_s(szHeight, TEXT("%d"), g_iProximityThreshold);
            SetWindowText(hHeightValue, szHeight);
        }
        else if ((HWND)lParam == hSpeedSlider)
        {
            int speedPos = SendMessage(hSpeedSlider, TBM_GETPOS, 0, 0);
            g_fMouseSpeedMultiplier = (float)speedPos / 10.0f;
            TCHAR szSpeed[10];
            _stprintf_s(szSpeed, TEXT("%.2f"), g_fMouseSpeedMultiplier);
            SetWindowText(hSpeedEdit, szSpeed);
        }
        bIsUpdatingControls = false;
    }
    break;

    case WM_COMMAND:
    {
        if (HIWORD(wParam) == 0)
        {
            switch (LOWORD(wParam))
            {
            case ID_EXIT_MENU: DestroyWindow(hWnd); break;
            case ID_SHOW_WINDOW_MENU: ShowWindow(hWnd, SW_SHOW); SetForegroundWindow(hWnd); break;
            }
        }
        if (HIWORD(wParam) == BN_CLICKED && LOWORD(wParam) == IDC_EXIT_BUTTON) { DestroyWindow(hWnd); }

        if (HIWORD(wParam) == EN_CHANGE && LOWORD(wParam) == IDC_SPEED_EDIT)
        {
            if (bIsUpdatingControls) break;
            bIsUpdatingControls = true;

            TCHAR szSpeed[10];
            GetWindowText(hSpeedEdit, szSpeed, 10);
            g_fMouseSpeedMultiplier = _ttof(szSpeed);

            int speedPos = (int)(g_fMouseSpeedMultiplier * 10.0f);
            SendMessage(hSpeedSlider, TBM_SETPOS, TRUE, (LPARAM)speedPos);

            bIsUpdatingControls = false;
        }
    }
    break;

    case WM_CLOSE:
        ShowWindow(hWnd, SW_HIDE);
        break;

    case WM_DESTROY:
    {
        if (g_hWinEventHook) { UnhookWinEvent(g_hWinEventHook); }
        NOTIFYICONDATA nid = { 0 };
        nid.cbSize = sizeof(NOTIFYICONDATA);
        nid.hWnd = hWnd;
        nid.uID = ID_TRAY_ICON;
        Shell_NotifyIcon(NIM_DELETE, &nid);
        PostQuitMessage(0);
    }
    break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

/// @brief Callback function for the system event hook. Called when focus changes.
VOID CALLBACK WinEventProc(
    HWINEVENTHOOK hWinEventHook,
    DWORD         event,
    HWND          hwnd,
    LONG          idObject,
    LONG          idChild,
    DWORD         dwEventThread,
    DWORD         dwmsEventTime)
{
    if (event == EVENT_SYSTEM_FOREGROUND)
    {
        SetTimer(g_hSettingsDlg, REASSERT_TIMER_ID, 100, NULL);
    }
}

/// @brief Processes a single Wintab packet.
void ProcessPenPacket(const PACKET& packet)
{
    static bool bHasInitialPoint = false;
    static POINT lastPoint;

    POINT currentPoint = { packet.pkX, packet.pkY };

    // If the pen is lifted out of proximity, reset our state.
    if (packet.pkZ > g_iProximityThreshold)
    {
        bHasInitialPoint = false;
        return;
    }

    // If we don't have a starting point yet, capture this one and exit.
    // This prevents the mouse from jumping on the first packet.
    if (!bHasInitialPoint)
    {
        lastPoint = currentPoint;
        bHasInitialPoint = true;
        return;
    }

    long dx = static_cast<long>((currentPoint.x - lastPoint.x) * g_fMouseSpeedMultiplier);
    long dy = static_cast<long>((currentPoint.y - lastPoint.y) * g_fMouseSpeedMultiplier);

    if (dx != 0 || dy != 0)
    {
        INPUT input = { 0 };
        input.type = INPUT_MOUSE;
        input.mi.dx = dx;
        input.mi.dy = -dy;
        input.mi.dwFlags = MOUSEEVENTF_MOVE;
        SendInput(1, &input, sizeof(INPUT));
    }

    lastPoint = currentPoint;
}

/// @brief Loads the Wintab DLL and prepares for message-based events.
HCTX InitWintabAPI(HWND hWnd)
{
    if (!LoadWintab()) { return NULL; }
    if (!gpWTInfoA(0, 0, NULL)) { return NULL; }

    LOGCONTEXTA logContext = { 0 };
    gpWTInfoA(WTI_DEFSYSCTX, 0, &logContext);

    wsprintfA(logContext.lcName, "Background Message Capture");
    logContext.lcOptions = CXO_SYSTEM | CXO_MESSAGES;
    logContext.lcPktData = PACKETDATA;
    logContext.lcPktMode = PACKETMODE;
    logContext.lcMoveMask = PACKETDATA;
    logContext.lcBtnUpMask = logContext.lcBtnDnMask;

    return gpWTOpenA(hWnd, &logContext, TRUE);
}

/// @brief Adds an icon to the system notification area (tray).
void CreateTrayIcon(HWND hWnd)
{
    NOTIFYICONDATA nid = { 0 };
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hWnd;
    nid.uID = ID_TRAY_ICON;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;

    nid.hIcon = (HICON)LoadImage(g_hInst,
        MAKEINTRESOURCE(IDI_ICON1),
        IMAGE_ICON,
        16, 16,
        LR_DEFAULTCOLOR);

    // --- ADD THIS CHECK ---
    if (nid.hIcon == NULL)
    {
        MessageBox(hWnd, TEXT("Failed to load tray icon!"), TEXT("Resource Error"), MB_OK | MB_ICONERROR);
    }

    _tcscpy_s(nid.szTip, _countof(nid.szTip), TEXT("WacomGamingEnabler"));
    Shell_NotifyIcon(NIM_ADD, &nid);
}

/// @brief Shows a context menu for the tray icon.
void ShowContextMenu(HWND hWnd)
{
    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    if (hMenu)
    {
        InsertMenu(hMenu, 0, MF_BYPOSITION, ID_SHOW_WINDOW_MENU, TEXT("Show Window"));
        InsertMenu(hMenu, 1, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
        InsertMenu(hMenu, 2, MF_BYPOSITION, ID_EXIT_MENU, TEXT("Exit"));

        SetForegroundWindow(hWnd);
        TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hWnd, NULL);
        PostMessage(hWnd, WM_NULL, 0, 0);
        DestroyMenu(hMenu);
    }
}

/// @brief Closes the Wintab context and unloads the library.
void Cleanup(void)
{
    // Clean up GDI brushes
    if (g_hbrBackground) { DeleteObject(g_hbrBackground); }
    if (g_hbrEditBackground) { DeleteObject(g_hbrEditBackground); }

    if (g_tabCtx)
    {
        gpWTClose(g_tabCtx);
        g_tabCtx = NULL;
    }
    UnloadWintab();
}