#include <windows.h>
#include <string.h>

HHOOK g_hHook = NULL;
HWND g_hwnd = NULL;
NOTIFYICONDATA g_nid = {0};

BOOL g_enabled = TRUE;          // Caps+ включён по умолчанию

#define WM_TRAYICON      (WM_USER + 1)
#define ID_TRAY_EXIT     1001
#define ID_TRAY_ENABLE   1002   // пункт меню: Caps+ ON/OFF

// Обновление текста подсказки в трее по текущему состоянию
void UpdateTrayTip()
{
    if (!g_hwnd) return;

    if (g_enabled) {
        strcpy(g_nid.szTip, "Caps+: ON (Ctrl+CapsLock)");
    } else {
        strcpy(g_nid.szTip, "Caps+: OFF (Ctrl+CapsLock)");
    }

    g_nid.uFlags = NIF_TIP;
    Shell_NotifyIcon(NIM_MODIFY, &g_nid); // меняем tooltip на лету [web:115][web:120]
}

// Хук клавиатуры
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* k = (KBDLLHOOKSTRUCT*)lParam;
        BOOL is_down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        BOOL is_caps = (k->vkCode == VK_CAPITAL);

        BOOL ctrl_down  = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        BOOL alt_down   = (GetAsyncKeyState(VK_MENU)    & 0x8000) != 0;
        BOOL shift_down = (GetAsyncKeyState(VK_SHIFT)   & 0x8000) != 0;

        // Ctrl+CapsLock > тумблер Caps+ ON/OFF
        if (is_down && is_caps && ctrl_down) {
            g_enabled = !g_enabled;
            UpdateTrayTip();
            return 1;   // не даём этому CapsLock менять регистр
        }

        // если Caps+ выключен — пропускаем всё как есть
        if (!g_enabled) {
            return CallNextHookEx(g_hHook, nCode, wParam, lParam);
        }

        // CapsLock без Ctrl, Alt и Shift > Alt+Shift (переключение языка)
        if (is_down && is_caps &&
            !ctrl_down &&
            !alt_down &&
            !shift_down) {

            keybd_event(VK_MENU,  0, 0, 0);             // Alt down
            keybd_event(VK_SHIFT, 0, 0, 0);             // Shift down
            keybd_event(VK_SHIFT, 0, KEYEVENTF_KEYUP, 0);
            keybd_event(VK_MENU,  0, KEYEVENTF_KEYUP, 0);
            return 1;
        }
    }
    return CallNextHookEx(g_hHook, nCode, wParam, lParam);
}

// Показ контекстного меню для трея
void ShowTrayMenu()
{
    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    UINT checked = g_enabled ? MF_CHECKED : MF_UNCHECKED;
    AppendMenu(hMenu, MF_STRING | checked, ID_TRAY_ENABLE, "Enable Caps+"); // [web:124]
    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT, "Exit");

    SetForegroundWindow(g_hwnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN,
                   pt.x, pt.y, 0, g_hwnd, NULL);

    DestroyMenu(hMenu);
}

// Окно для цикла сообщений и трея
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
        case WM_CREATE: {
            g_hwnd = hwnd;

            g_nid.cbSize = sizeof(NOTIFYICONDATA);
            g_nid.hWnd = hwnd;
            g_nid.uID = 1;
            g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
            g_nid.uCallbackMessage = WM_TRAYICON;

            // Пытаемся загрузить иконку из файла
            HICON hIcon = (HICON)LoadImageA(
                NULL,
                "C:\\tools\\locks\\capslock.ico", // при необходимости поменяешь путь
                IMAGE_ICON,
                16, 16,
                LR_LOADFROMFILE
            );
            if (!hIcon) {
                hIcon = LoadIcon(NULL, IDI_APPLICATION);
            }
            g_nid.hIcon = hIcon;

            strcpy(g_nid.szTip, "Caps+: ON (Ctrl+CapsLock)");
            Shell_NotifyIcon(NIM_ADD, &g_nid); // добавить иконку [web:115]
            break;
        }

        case WM_DESTROY:
            Shell_NotifyIcon(NIM_DELETE, &g_nid);
            PostQuitMessage(0);
            break;

        case WM_TRAYICON: {
            if (lParam == WM_RBUTTONDOWN || lParam == WM_CONTEXTMENU) {
                ShowTrayMenu();
            }
            break;
        }

        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case ID_TRAY_EXIT:
                    DestroyWindow(hwnd);
                    break;

                case ID_TRAY_ENABLE:
                    g_enabled = !g_enabled;
                    UpdateTrayTip();
                    break;
            }
            break;
        }

        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR lpCmd, int nShow)
{
    // Single-instance через mutex
    HANDLE hMutex = CreateMutexA(NULL, TRUE, "caps_lang_single_instance");
    if (hMutex == NULL || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (hMutex) CloseHandle(hMutex);
        return 0;
    }

    // Регистрируем класс окна
    WNDCLASS wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = "CapsLangHook";
    RegisterClass(&wc);

    // Создаём невидимое message-окно
    HWND hwnd = CreateWindowEx(0, "CapsLangHook", "",
                               0, 0, 0, 0, 0, HWND_MESSAGE, NULL, hInst, NULL);
    if (!hwnd) {
        if (hMutex) {
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
        }
        return 1;
    }

    // Ставим глобальный низкоуровневый хук
    g_hHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, hInst, 0);
    if (!g_hHook) {
        MessageBox(NULL, "Failed to set hook", "Error", MB_OK | MB_ICONERROR);
        if (hMutex) {
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
        }
        return 1;
    }

    // Цикл сообщений
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Чистим за собой
    UnhookWindowsHookEx(g_hHook);

    if (hMutex) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }

    return 0;
}
