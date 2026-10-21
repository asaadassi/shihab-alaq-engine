#include "stdafx.h"

#include "main_window.h"

#include "functions.h"
#include "logger.h"
#include "mod_crash_guard.h"
#include "resource.h"
#include "ui_control.h"
#include "version.h"

#include <dwmapi.h>

#include <deque>

#pragma comment(lib, "dwmapi.lib")

namespace {

constexpr auto kHandleNewProcessInterval = 1000;       // 1sec
constexpr auto kUpdateInitialDelay = 1000 * 10;        // 10sec
constexpr auto kUpdateInterval = 1000 * 60 * 60 * 24;  // 24h
constexpr auto kUpdateRetryTime = 1000 * 60 * 60;      // 1h
constexpr auto kModTasksDlgInitialDelay = 1000;        // 1sec

// Shihab Alaq: the tray icon's menu is drawn in the Shihab look instead of the
// grey system menu: the colours are the dark tokens of Shihab.Common
// (ShihabTheme.xaml: Panel, ControlHover, Stroke, Text, Muted, Accent) and the
// text is Almarai when it is installed, Segoe UI otherwise.
namespace ShihabMenu {
constexpr COLORREF kPanel = RGB(0x1C, 0x17, 0x20);
constexpr COLORREF kHover = RGB(0x37, 0x2F, 0x3D);
constexpr COLORREF kStroke = RGB(0x35, 0x2D, 0x3B);
constexpr COLORREF kText = RGB(0xF5, 0xF1, 0xF7);
constexpr COLORREF kMuted = RGB(0xA7, 0x9C, 0xAD);

// "label" or "label\tshortcut". Alive while the menu is open: each item's data
// points at one of these, and a deque never moves its elements when it grows.
std::deque<std::wstring> g_items;
HBRUSH g_panelBrush = nullptr;
HFONT g_textFont = nullptr;
UINT g_fontDpi = 0;

int CALLBACK OnFontFound(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM found) {
    *reinterpret_cast<bool*>(found) = true;
    return 0;
}

bool IsFontInstalled(const wchar_t* face) {
    HDC dc = GetDC(nullptr);
    LOGFONTW lf{};
    lf.lfCharSet = DEFAULT_CHARSET;
    wcscpy_s(lf.lfFaceName, face);
    bool found = false;
    EnumFontFamiliesExW(dc, &lf, OnFontFound, reinterpret_cast<LPARAM>(&found), 0);
    ReleaseDC(nullptr, dc);
    return found;
}

void EnsureResources(UINT dpi) {
    if (!g_panelBrush) {
        g_panelBrush = CreateSolidBrush(kPanel);
    }
    if (g_fontDpi == dpi && g_textFont) {
        return;
    }
    if (g_textFont) {
        DeleteObject(g_textFont);
    }
    static const bool almarai = IsFontInstalled(L"Almarai");
    g_textFont = CreateFontW(-MulDiv(13, dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE,
                             FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                             almarai ? L"Almarai" : L"Segoe UI");
    g_fontDpi = dpi;
}

void Append(HMENU menu, UINT flags, UINT_PTR id = 0, PCWSTR text = nullptr) {
    if (flags & MF_SEPARATOR) {
        AppendMenuW(menu, MF_SEPARATOR | MF_OWNERDRAW, 0, nullptr);
        return;
    }
    g_items.push_back(text ? text : L"");
    AppendMenuW(menu, flags | MF_OWNERDRAW, id,
                reinterpret_cast<PCWSTR>(&g_items.back()));
}

// The panel colour behind the items.
void Finish(HMENU menu) {
    MENUINFO info{sizeof(info)};
    info.fMask = MIM_BACKGROUND | MIM_APPLYTOSUBMENUS;
    info.hbrBack = g_panelBrush;
    SetMenuInfo(menu, &info);
}

bool Measure(HWND hwnd, MEASUREITEMSTRUCT* mis) {
    if (mis->CtlType != ODT_MENU) {
        return false;
    }
    const UINT dpi = g_fontDpi ? g_fontDpi : 96;
    const auto* text = reinterpret_cast<const std::wstring*>(mis->itemData);
    if (!text) {  // separator
        mis->itemWidth = 0;
        mis->itemHeight = MulDiv(9, dpi, 96);
        return true;
    }
    HDC dc = GetDC(hwnd);
    HGDIOBJ old = SelectObject(dc, g_textFont);
    // Measured as drawn: the "&" of the keyboard letter takes no room
    RECT size{};
    DrawTextW(dc, text->c_str(), static_cast<int>(text->size()), &size,
              DT_CALCRECT | DT_SINGLELINE | DT_EXPANDTABS | DT_HIDEPREFIX);
    SelectObject(dc, old);
    ReleaseDC(hwnd, dc);
    // Room for the text, the gap before a shortcut, and the padding
    mis->itemWidth = size.right + MulDiv(34 + 40, dpi, 96);
    mis->itemHeight = MulDiv(32, dpi, 96);
    return true;
}

bool Draw(DRAWITEMSTRUCT* dis, bool rtl) {
    if (dis->CtlType != ODT_MENU) {
        return false;
    }
    HDC dc = dis->hDC;
    const UINT dpi = g_fontDpi ? g_fontDpi : 96;
    auto px = [dpi](int v) { return MulDiv(v, dpi, 96); };

    // A right-to-left menu gets a mirrored DC, and right-to-left text drawn on
    // it loses its order. Draw unmirrored, through device coordinates (the
    // first paint goes to a memory DC with no window to measure), and place the
    // text from the right here instead.
    RECT rc = dis->rcItem;
    const DWORD layout = GetLayout(dc);
    if (layout & LAYOUT_RTL) {
        POINT corners[2] = {{rc.left, rc.top}, {rc.right, rc.bottom}};
        LPtoDP(dc, corners, 2);
        SetLayout(dc, 0);
        DPtoLP(dc, corners, 2);
        rc = {std::min(corners[0].x, corners[1].x), rc.top,
              std::max(corners[0].x, corners[1].x), rc.bottom};
    }
    FillRect(dc, &rc, g_panelBrush);

    const auto* text = reinterpret_cast<const std::wstring*>(dis->itemData);
    if (!text) {  // separator: a hairline
        const int y = (rc.top + rc.bottom) / 2;
        RECT line{rc.left + px(10), y, rc.right - px(10), y + std::max(1, px(1))};
        HBRUSH stroke = CreateSolidBrush(kStroke);
        FillRect(dc, &line, stroke);
        DeleteObject(stroke);
    } else {
        const bool disabled = (dis->itemState & (ODS_GRAYED | ODS_DISABLED)) != 0;
        if ((dis->itemState & ODS_SELECTED) && !disabled) {
            HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(DC_BRUSH));
            HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
            SetDCBrushColor(dc, kHover);
            RoundRect(dc, rc.left + px(4), rc.top + px(2), rc.right - px(4),
                      rc.bottom - px(1), px(10), px(10));
            SelectObject(dc, oldPen);
            SelectObject(dc, oldBrush);
        }

        std::wstring label = *text;
        std::wstring shortcut;
        if (size_t tab = label.find(L'\t'); tab != std::wstring::npos) {
            shortcut = label.substr(tab + 1);
            label.resize(tab);
        }

        SetBkMode(dc, TRANSPARENT);
        HGDIOBJ oldFont = SelectObject(dc, g_textFont);
        RECT textRect{rc.left + px(18), rc.top, rc.right - px(18), rc.bottom};
        SetTextColor(dc, disabled ? kMuted : kText);
        // DT_HIDEPREFIX: the "&" before the keyboard letter isn't shown
        DrawTextW(dc, label.c_str(), static_cast<int>(label.size()), &textRect,
                  DT_SINGLELINE | DT_VCENTER | DT_HIDEPREFIX |
                      (rtl ? DT_RIGHT | DT_RTLREADING : DT_LEFT));
        if (!shortcut.empty()) {
            // A shortcut reads left to right whatever the menu's direction
            SetTextColor(dc, kMuted);
            DrawTextW(dc, shortcut.c_str(), static_cast<int>(shortcut.size()),
                      &textRect,
                      DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX |
                          (rtl ? DT_LEFT : DT_RIGHT));
        }
        SelectObject(dc, oldFont);
    }

    if (layout & LAYOUT_RTL) {
        SetLayout(dc, layout);
    }
    // Stops the system from painting over the item
    ExcludeClipRect(dc, dis->rcItem.left, dis->rcItem.top, dis->rcItem.right,
                    dis->rcItem.bottom);
    return true;
}

// Owner-drawn items lose the "&" keyboard letters of plain menus: the item
// whose letter was typed is found here (WM_MENUCHAR) and run.
LRESULT MenuChar(wchar_t typed, HMENU menu) {
    const int count = GetMenuItemCount(menu);
    for (int i = 0; i < count; i++) {
        MENUITEMINFOW info{sizeof(info)};
        info.fMask = MIIM_DATA;
        if (!GetMenuItemInfoW(menu, i, TRUE, &info) || !info.dwItemData) {
            continue;
        }
        const auto* text = reinterpret_cast<const std::wstring*>(info.dwItemData);
        for (size_t p = text->find(L'&'); p != std::wstring::npos && p + 1 < text->size();
             p = text->find(L'&', p + 2)) {
            if ((*text)[p + 1] == L'&') {
                continue;  // "&&" is a plain "&"
            }
            if (CharUpperW(reinterpret_cast<LPWSTR>(static_cast<ULONG_PTR>((*text)[p + 1]))) ==
                CharUpperW(reinterpret_cast<LPWSTR>(static_cast<ULONG_PTR>(typed)))) {
                return MAKELRESULT(i, MNC_EXECUTE);
            }
            break;
        }
    }
    return MAKELRESULT(0, MNC_IGNORE);
}

// The menu window itself: a Stroke-coloured border and round corners (Windows 11).
void StyleMenuWindow(HWND menuWindow) {
    if (!menuWindow) {
        return;
    }
    const COLORREF border = kStroke;
    DwmSetWindowAttribute(menuWindow, 34 /* DWMWA_BORDER_COLOR */, &border,
                          sizeof(border));
    const int corner = 2;  // DWMWCP_ROUND
    DwmSetWindowAttribute(menuWindow, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */,
                          &corner, sizeof(corner));
}
}  // namespace ShihabMenu

ULONGLONG GetTaskbarProcessCreationTime() {
    HWND currentTaskbarWindow = FindWindow(L"Shell_TrayWnd", nullptr);
    if (!currentTaskbarWindow) {
        return 0;
    }

    DWORD currentTaskbarProcessId;
    if (!GetWindowThreadProcessId(currentTaskbarWindow,
                                  &currentTaskbarProcessId)) {
        return 0;
    }

    wil::unique_process_handle currentTaskbarProcess(OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION, FALSE, currentTaskbarProcessId));
    if (!currentTaskbarProcess) {
        return 0;
    }

    FILETIME creationTime;
    FILETIME exitTime;
    FILETIME kernelTime;
    FILETIME userTime;
    if (!GetProcessTimes(currentTaskbarProcess.get(), &creationTime, &exitTime,
                         &kernelTime, &userTime)) {
        return 0;
    }

    return wil::filetime::to_int64(creationTime);
}

}  // namespace

CMainWindow::CMainWindow(bool trayOnly, bool portable)
    : m_trayOnly(trayOnly),
      m_portable(portable),
      m_taskbarCreatedMsg(RegisterWindowMessage(L"TaskbarCreated")) {}

BOOL CMainWindow::PreTranslateMessage(MSG* pMsg) {
    if (m_modTasksDlg && m_modTasksDlg->IsDialogMessage(pMsg)) {
        return TRUE;
    }

    if (m_modStatusesDlg && m_modStatusesDlg->IsDialogMessage(pMsg)) {
        return TRUE;
    }

    if (m_toolkitDlg && m_toolkitDlg->IsDialogMessage(pMsg)) {
        return TRUE;
    }

    return FALSE;
}

BOOL CMainWindow::OnIdle() {
    enum {
        kServiceMutex,
        kAppSettingsChanged,
        kNewUpdatesFound,
        kModTasksChanged,
        kModStatusesChanged,
        kExplorerCrashed,
        kMaxHandles,
    };

    HANDLE handleArray[kMaxHandles];
    int handleTypes[kMaxHandles];
    DWORD handleCount = 0;

    if (m_serviceMutex) {
        handleArray[handleCount] = m_serviceMutex.get();
        handleTypes[handleCount] = kServiceMutex;
        handleCount++;
    }

    if (m_appSettingsChangedEvent) {
        handleArray[handleCount] = m_appSettingsChangedEvent.get();
        handleTypes[handleCount] = kAppSettingsChanged;
        handleCount++;
    }

    if (m_newUpdatesFoundEvent) {
        handleArray[handleCount] = m_newUpdatesFoundEvent.get();
        handleTypes[handleCount] = kNewUpdatesFound;
        handleCount++;
    }

    if (m_modTasksChangeNotification) {
        handleArray[handleCount] = m_modTasksChangeNotification->GetHandle();
        handleTypes[handleCount] = kModTasksChanged;
        handleCount++;
    }

    if (m_modStatusesChangeNotification) {
        handleArray[handleCount] = m_modStatusesChangeNotification->GetHandle();
        handleTypes[handleCount] = kModStatusesChanged;
        handleCount++;
    }

    if (m_explorerCrashMonitor) {
        handleArray[handleCount] = m_explorerCrashMonitor->GetEventHandle();
        handleTypes[handleCount] = kExplorerCrashed;
        handleCount++;
    }

    if (handleCount > 0) {
        DWORD nWaitResult =
            MsgWaitForMultipleObjectsEx(handleCount, handleArray, INFINITE,
                                        QS_ALLINPUT, MWMO_INPUTAVAILABLE);

        if (nWaitResult >= WAIT_OBJECT_0 &&
            nWaitResult < WAIT_OBJECT_0 + handleCount) {
            switch (handleTypes[nWaitResult - WAIT_OBJECT_0]) {
                case kServiceMutex:
                    ::ReleaseMutex(m_serviceMutex.get());
                    Exit();
                    break;

                case kAppSettingsChanged:
                    LoadSettings();
                    break;

                case kNewUpdatesFound:
                    if (!m_disableUpdateCheck) {
                        NotifyAboutAvailableUpdates(
                            UserProfile::GetUpdateStatus(), true);
                    }
                    break;

                case kModTasksChanged:
                    if (m_modTasksDlg) {
                        m_modTasksDlg->DataChanged();

                        try {
                            m_modTasksChangeNotification->ContinueMonitoring();
                        } catch (const std::exception& e) {
                            LOG(L"Tasks ContinueMonitoring failed: %S",
                                e.what());
                            m_modTasksChangeNotification.reset();
                        }
                    } else {
                        // In the common case, there's a short-lived event, such
                        // as mod initialization, that is cleared right away.
                        // Wait a bit before creating a dialog, and only create
                        // it if events still exist.
                        m_modTasksChangeNotification.reset();
                        SetTimer(Timer::kModTasksDlgCreate,
                                 kModTasksDlgInitialDelay);
                    }
                    break;

                case kModStatusesChanged:
                    if (m_modStatusesDlg) {
                        m_modStatusesDlg->DataChanged();
                    }

                    try {
                        m_modStatusesChangeNotification->ContinueMonitoring();
                    } catch (const std::exception& e) {
                        LOG(L"Statuses ContinueMonitoring failed: %S",
                            e.what());
                        m_modStatusesChangeNotification.reset();
                    }
                    break;

                case kExplorerCrashed: {
                    int explorerCrashCount = 0;
                    try {
                        explorerCrashCount =
                            m_explorerCrashMonitor->GetAmountOfNewEvents();
                    } catch (const std::exception& e) {
                        LOG(L"Explorer crash monitor failed: %S", e.what());
                        m_explorerCrashMonitor.reset();
                        break;
                    }

                    if (explorerCrashCount > 0) {
                        try {
                            HandleExplorerCrash(explorerCrashCount);
                        } catch (const std::exception& e) {
                            LOG(L"Explorer crash handling failed: %S",
                                e.what());
                        }
                    }
                    break;
                }
            }
        }
    } else {
        // Just wait for a message to avoid running an infinite loop.
        MsgWaitForMultipleObjectsEx(0, nullptr, INFINITE, QS_ALLINPUT,
                                    MWMO_INPUTAVAILABLE);
    }

    return FALSE;
}

int CMainWindow::OnCreate(LPCREATESTRUCT lpCreateStruct) {
    // Register object for message filtering and idle updates.
    CMessageLoop* pLoop = _Module.GetMessageLoop();
    ATLASSERT(pLoop != nullptr);
    pLoop->AddMessageFilter(this);
    pLoop->AddIdleHandler(this);

    try {
        if (m_portable) {
            InitForPortableVersion();
        } else {
            InitForNonPortableVersion();
        }
    } catch (const std::exception& e) {
        ::MessageBoxA(nullptr, e.what(), "Could not initialize AlaqEngine",
                      MB_ICONERROR);
        return -1;
    }

    m_trayIcon.emplace(m_hWnd, UWM_TRAYICON, /*hidden=*/true);
    m_trayIcon->Create();

    LoadSettings();

    try {
        m_modTasksChangeNotification.emplace(L"mod-task");
    } catch (const std::exception& e) {
        LOG(L"Tasks ChangeNotification failed: %S", e.what());
    }

    if (!m_disableToolkitHotkey) {
        m_toolkitHotkeyRegistered =
            ::RegisterHotKey(m_hWnd, static_cast<int>(Hotkey::kToolkit),
                             MOD_CONTROL | MOD_WIN | MOD_NOREPEAT, 'W');
        if (!m_toolkitHotkeyRegistered) {
            LOG(L"RegisterHotKey failed: %u", GetLastError());
        }
    }

    if (!m_trayOnly) {
        RunUI();
    }

    return 0;
}

void CMainWindow::OnDestroy() {
    if (m_toolkitHotkeyRegistered) {
        ::UnregisterHotKey(m_hWnd, static_cast<int>(Hotkey::kToolkit));
        m_toolkitHotkeyRegistered = false;
    }

    if (m_trayIcon) {
        m_trayIcon->Remove();
    }

    // Unregister message filtering and idle updates.
    CMessageLoop* pLoop = _Module.GetMessageLoop();
    ATLASSERT(pLoop != NULL);
    pLoop->RemoveMessageFilter(this);
    pLoop->RemoveIdleHandler(this);

    PostQuitMessage(0);
}

void CMainWindow::OnHotKey(int nHotKeyID, UINT uModifiers, UINT uVirtKey) {
    switch (static_cast<Hotkey>(nHotKeyID)) {
        case Hotkey::kToolkit:
            SetForegroundWindow(GetLastActivePopup());
            ShowToolkitDialog();
            break;
    }
}

void CMainWindow::OnTimer(UINT_PTR nIDEvent) {
    switch (static_cast<Timer>(nIDEvent)) {
        case Timer::kHandleNewProcesses:
            if (m_engineControl) {
                m_engineControl->HandleNewProcesses();
            }

            SetTimer(Timer::kHandleNewProcesses, kHandleNewProcessInterval);
            break;

        case Timer::kUpdateCheck:
            KillTimer(Timer::kUpdateCheck);

            try {
                m_updateChecker = std::make_unique<UpdateChecker>(
                    m_portable ? UpdateChecker::kFlagPortable : 0,
                    [this] { PostMessage(UWM_UPDATE_CHECKED); });
            } catch (const std::exception& e) {
                LOG(L"UpdateChecker failed: %S", e.what());
                SetTimer(Timer::kUpdateCheck, kUpdateRetryTime);
            }
            break;

        case Timer::kModTasksDlgCreate:
            KillTimer(Timer::kModTasksDlgCreate);

            try {
                m_modTasksChangeNotification.emplace(L"mod-task");

                if (!CTaskManagerDlg::IsDataSourceEmpty(
                        CTaskManagerDlg::DataSource::kModTask)) {
                    m_modTasksDlg.emplace(CTaskManagerDlg::DialogOptions{
                        .dataSource = CTaskManagerDlg::DataSource::kModTask,
                        .autonomousMode = true,
                        .autonomousModeShowDelay = m_modTasksDlgDelay,
                        .sessionManagerProcessId = m_serviceInfo.processId,
                        .sessionManagerProcessCreationTime =
                            m_serviceInfo.processCreationTime,
                        .runButtonCallback = [this](HWND hWnd) { RunUI(hWnd); },
                        .finalMessageCallback =
                            [this](HWND hWnd) { m_modTasksDlg.reset(); }});

                    if (!m_modTasksDlg->Create(m_hWnd)) {
                        m_modTasksDlg.reset();
                    }
                }
            } catch (const std::exception& e) {
                LOG(L"%S", e.what());
            }
            break;
    }
}

BOOL CMainWindow::OnPowerBroadcast(DWORD dwPowerEvent, DWORD_PTR dwData) {
    if (dwPowerEvent == PBT_APMRESUMEAUTOMATIC && m_checkForUpdates &&
        !m_updateChecker) {
        KillTimer(Timer::kUpdateCheck);

        ULONGLONG lastUpdateCheck;
        try {
            auto settings =
                StorageManager::GetInstance().GetAppConfig(L"Settings", false);
            lastUpdateCheck = std::wcstoull(
                settings->GetString(L"LastUpdateCheck").value_or(L"0").c_str(),
                nullptr, 10);
        } catch (const std::exception& e) {
            LOG(L"Getting LastUpdateCheck failed: %S", e.what());
            lastUpdateCheck = 0;
        }

        SetTimer(Timer::kUpdateCheck, GetNextUpdateDelay(lastUpdateCheck));
    }

    return FALSE;
}

LRESULT CMainWindow::OnPortableAppCommand(UINT uMsg,
                                          WPARAM wParam,
                                          LPARAM lParam) {
    if (!m_portable) {
        return 0;
    }

    switch ((PortableAppCommand)wParam) {
        case PortableAppCommand::kRunUI:
            RunUI();
            break;

        case PortableAppCommand::kExit:
            Exit();
            break;
    }

    return 0;
}

LRESULT CMainWindow::OnMeasureItem(UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (!ShihabMenu::Measure(m_hWnd,
                             reinterpret_cast<MEASUREITEMSTRUCT*>(lParam))) {
        SetMsgHandled(FALSE);
        return 0;
    }
    return TRUE;
}

LRESULT CMainWindow::OnDrawItem(UINT uMsg, WPARAM wParam, LPARAM lParam) {
    const bool rtl = (GetExStyle() & WS_EX_LAYOUTRTL) != 0;
    if (!ShihabMenu::Draw(reinterpret_cast<DRAWITEMSTRUCT*>(lParam), rtl)) {
        SetMsgHandled(FALSE);
        return 0;
    }
    return TRUE;
}

LRESULT CMainWindow::OnMenuChar(UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (HIWORD(wParam) != MF_POPUP) {
        SetMsgHandled(FALSE);
        return 0;
    }
    return ShihabMenu::MenuChar(LOWORD(wParam), reinterpret_cast<HMENU>(lParam));
}

LRESULT CMainWindow::OnEnterIdle(UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // lParam is the window of the menu being shown
    if (wParam == MSGF_MENU) {
        ShihabMenu::StyleMenuWindow(reinterpret_cast<HWND>(lParam));
    }
    SetMsgHandled(FALSE);
    return 0;
}

LRESULT CMainWindow::OnTrayIcon(UINT uMsg, WPARAM wParam, LPARAM lParam) {
    enum class Action {
        kNone,
        kOpenUI,
        kOpenUpdatePage,
        kModTaskManager,
        kDisableAllMods,
        kToolkit,
        kExit,
    };

    auto contextMenuFunc = [this]() {
        CMenu menu;
        if (!menu.CreatePopupMenu()) {
            return Action::kNone;
        }

        // Shihab Alaq: the items are drawn by ShihabMenu (OnMeasureItem,
        // OnDrawItem), in the Shihab look.
        ShihabMenu::g_items.clear();
        // The screen's DPI: GetDpiForWindow isn't there on Windows 7
        {
            CClientDC screen(nullptr);
            ShihabMenu::EnsureResources(screen.GetDeviceCaps(LOGPIXELSY));
        }
        ShihabMenu::Append(menu, MF_STRING,
                           static_cast<UINT_PTR>(Action::kOpenUI),
                           Functions::LoadStrFromRsrc(IDS_TRAY_OPEN));
        ShihabMenu::Append(menu, MF_SEPARATOR);
        ShihabMenu::Append(menu, MF_STRING,
                           static_cast<UINT_PTR>(Action::kModTaskManager),
                           Functions::LoadStrFromRsrc(IDS_TRAY_LOADED_MODS));
        // Shihab Alaq: one click turns every mod off.
        ShihabMenu::Append(menu, MF_STRING,
                           static_cast<UINT_PTR>(Action::kDisableAllMods),
                           Functions::LoadStrFromRsrc(IDS_TRAY_DISABLE_ALL_MODS));
        ShihabMenu::Append(
            menu, MF_STRING, static_cast<UINT_PTR>(Action::kToolkit),
            (std::wstring(Functions::LoadStrFromRsrc(IDS_TRAY_TOOLKIT)) +
             (m_disableToolkitHotkey ? L"" : L"\tCtrl+Win+W"))
                .c_str());
        ShihabMenu::Append(menu, MF_SEPARATOR);
        ShihabMenu::Append(menu, MF_STRING, static_cast<UINT_PTR>(Action::kExit),
                           Functions::LoadStrFromRsrc(IDS_TRAY_EXIT));
        ShihabMenu::Finish(menu);

        CPoint point;
        GetCursorPos(&point);

        BOOL result = menu.TrackPopupMenu(TPM_RIGHTBUTTON | TPM_RETURNCMD,
                                          point.x, point.y, m_hWnd);

        return static_cast<Action>(result);
    };

    Action action = Action::kNone;

    switch (m_trayIcon->HandleMsg(wParam, lParam)) {
        case AppTrayIcon::TrayAction::kDefault:
            action = Action::kOpenUI;
            break;

        case AppTrayIcon::TrayAction::kBalloon:
            if (m_lastUpdateStatus && m_lastUpdateStatus->appUpdateAvailable) {
                action = Action::kOpenUpdatePage;
            } else {
                action = Action::kOpenUI;
            }
            break;

        case AppTrayIcon::TrayAction::kContextMenu:
            ::SetForegroundWindow(m_hWnd);
            action = contextMenuFunc();
            break;
    }

    switch (action) {
        case Action::kOpenUI:
            RunUI();
            break;

        case Action::kOpenUpdatePage:
            OpenUpdatePage();
            break;

        case Action::kModTaskManager:
            ShowLoadedModsDialog();
            break;

        case Action::kDisableAllMods:
            DisableAllMods();
            break;

        case Action::kToolkit:
            ShowToolkitDialog();
            break;

        case Action::kExit:
            if (m_portable) {
                Exit();
            } else {
                StopService();
            }
            break;
    }

    return 0;
}

LRESULT CMainWindow::OnUpdateChecked(UINT uMsg, WPARAM wParam, LPARAM lParam) {
    UpdateChecker::Result result = m_updateChecker->HandleResponse();
    m_updateChecker.reset();

    if (m_exitWhenUpdateCheckDone) {
        DestroyWindow();
        return 0;
    }

    if (!m_checkForUpdates) {
        return 0;
    }

    if (SUCCEEDED(result.hrError)) {
        NotifyAboutAvailableUpdates(result.updateStatus);

        SetLastUpdateTime();

        SetTimer(Timer::kUpdateCheck, kUpdateInterval);
    } else {
        SetTimer(Timer::kUpdateCheck, kUpdateRetryTime);
    }

    return 0;
}

LRESULT CMainWindow::OnTaskbarCreated(UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // If the toolkit was never active, close it.
    // if (m_toolkitDlg && !m_toolkitDlg->WasActive()) {
    //     m_toolkitDlg->Close();
    // }

    // Reload icons since the DPI might have changed. From the documentation:
    // "On Windows 10, the taskbar also broadcasts this message when the DPI of
    // the primary display changes."
    m_trayIcon->UpdateIcons(m_hWnd);

    m_trayIcon->Create();

    // Necessary to apply the newly loaded icon in Windows 11 22H2.
    m_trayIcon->Modify();

    return 0;
}

UINT_PTR CMainWindow::SetTimer(Timer nIDEvent,
                               UINT nElapse,
                               TIMERPROC lpfnTimer) {
    return CWindowImpl::SetTimer(static_cast<UINT_PTR>(nIDEvent), nElapse,
                                 lpfnTimer);
}

BOOL CMainWindow::KillTimer(Timer nIDEvent) {
    return CWindowImpl::KillTimer(static_cast<UINT_PTR>(nIDEvent));
}

void CMainWindow::InitForPortableVersion() {
    auto settings =
        StorageManager::GetInstance().GetAppConfig(L"Settings", false);

    if (!settings->GetInt(L"SafeMode").value_or(0)) {
        m_engineControl.emplace();
        m_engineControl->HandleNewProcesses();
    }

    SetTimer(Timer::kHandleNewProcesses, kHandleNewProcessInterval);

    m_appSettingsChangedEvent.reset(Functions::CreateEventForMediumIntegrity(
        L"ShihabAlaqAppSettingsChangedEvent-daemon"));

    FILETIME creationTime;
    FILETIME exitTime;
    FILETIME kernelTime;
    FILETIME userTime;
    THROW_IF_WIN32_BOOL_FALSE(GetProcessTimes(
        GetCurrentProcess(), &creationTime, &exitTime, &kernelTime, &userTime));

    // For the portable version, there's no service, set app info instead.
    m_serviceInfo.version = VER_FILE_VERSION_LONG;
    m_serviceInfo.processId = GetCurrentProcessId();
    m_serviceInfo.processCreationTime = wil::filetime::to_int64(creationTime);

    ::ChangeWindowMessageFilterEx(m_hWnd, UWM_PORTABLE_APP_COMMAND,
                                  MSGFLT_ALLOW, nullptr);
}

void CMainWindow::InitForNonPortableVersion() {
    m_serviceMutex.reset(
        OpenMutex(SYNCHRONIZE, FALSE, ServiceCommon::kMutexName));
    THROW_LAST_ERROR_IF(!m_serviceMutex);

    DWORD sessionId;
    THROW_IF_WIN32_BOOL_FALSE(
        ProcessIdToSessionId(GetCurrentProcessId(), &sessionId));

    std::wstring appSettingsChangedEventName =
        L"Global\\ShihabAlaqAppSettingsChangedEvent-daemon-session=" +
        std::to_wstring(sessionId);

    m_appSettingsChangedEvent.reset(Functions::CreateEventForMediumIntegrity(
        appSettingsChangedEventName.c_str()));

    std::wstring newUpdatesFoundEventName =
        L"Global\\ShihabAlaqNewUpdatesFoundEvent-daemon-session=" +
        std::to_wstring(sessionId);

    m_newUpdatesFoundEvent.reset(Functions::CreateEventForMediumIntegrity(
        newUpdatesFoundEventName.c_str()));

    wil::unique_handle fileMapping(OpenFileMapping(
        FILE_MAP_READ, FALSE, ServiceCommon::kInfoFileMappingName));
    THROW_LAST_ERROR_IF(!fileMapping);

    wil::unique_mapview_ptr<ServiceCommon::ServiceInfo> fileMappingView(
        reinterpret_cast<ServiceCommon::ServiceInfo*>(
            MapViewOfFile(fileMapping.get(), FILE_MAP_READ, 0, 0,
                          sizeof(ServiceCommon::ServiceInfo))));
    THROW_LAST_ERROR_IF(!fileMappingView);

    m_serviceInfo = *fileMappingView;

    if (m_serviceInfo.version != VER_FILE_VERSION_LONG) {
        LOG(L"Version mismatch, service: %08X, app: %08X",
            m_serviceInfo.version, VER_FILE_VERSION_LONG);
    }
}

void CMainWindow::LoadSettings() {
    LANGID languageId;
    bool hideTrayIcon;
    bool disableUpdateCheck;
    ULONGLONG lastUpdateCheck;
    bool dontAutoShowToolkit;
    bool disableToolkitHotkey;
    int modTasksDlgDelay;
    int crashGuardNotice;
    std::wstring crashGuardNoticeReason;
    std::wstring crashGuardNoticeMod;
    std::wstring crashGuardNoticeProgram;

    try {
        auto settings =
            StorageManager::GetInstance().GetAppConfig(L"Settings", false);

        languageId = LANGIDFROMLCID(LocaleNameToLCID(
            settings->GetString(L"Language").value_or(L"ar").c_str(), 0));
        hideTrayIcon = settings->GetInt(L"HideTrayIcon").value_or(0);
        // Shihab Alaq updates the engine with its own installer, so the
        // engine never checks for updates itself.
        disableUpdateCheck = true;

        if (m_portable) {
            lastUpdateCheck = std::wcstoull(
                settings->GetString(L"LastUpdateCheck").value_or(L"0").c_str(),
                nullptr, 10);
        } else {
            // For the non-portable version, update checking is done by another
            // process, and we're notified via an event.
            lastUpdateCheck = 0;
        }

        dontAutoShowToolkit =
            settings->GetInt(L"DontAutoShowToolkit").value_or(0);

        disableToolkitHotkey =
            settings->GetInt(L"DisableToolkitHotkey").value_or(0);

        modTasksDlgDelay =
            settings->GetInt(L"ModTasksDialogDelay")
                .value_or(CTaskManagerDlg::kAutonomousModeShowDelayDefault);

        // Shihab Alaq: written by the crash guard (ModCrashGuard).
        crashGuardNotice = settings->GetInt(L"CrashGuardNotice").value_or(0);
        crashGuardNoticeReason =
            settings->GetString(L"CrashGuardNoticeReason").value_or(L"");
        crashGuardNoticeMod =
            settings->GetString(L"CrashGuardNoticeMod").value_or(L"");
        crashGuardNoticeProgram =
            settings->GetString(L"CrashGuardNoticeProgram").value_or(L"");
    } catch (const std::exception& e) {
        ::MessageBoxA(nullptr, e.what(), "Could not load settings",
                      MB_ICONERROR);
        return;
    }

    if (languageId != m_languageId) {
        ::SetThreadUILanguage(
            languageId ? languageId
                       : MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US));

        bool languageRightToLeft = Functions::IsRightToLeftLanguage(languageId);
        ModifyStyleEx(languageRightToLeft ? 0 : WS_EX_LAYOUTRTL,
                      languageRightToLeft ? WS_EX_LAYOUTRTL : 0);

        if (m_modTasksDlg) {
            m_modTasksDlg->LoadLanguageStrings();
        }

        if (m_modStatusesDlg) {
            m_modStatusesDlg->LoadLanguageStrings();
        }

        if (m_toolkitDlg) {
            m_toolkitDlg->LoadLanguageStrings();
        }

        m_languageId = languageId;
    }

    if (hideTrayIcon != m_hideTrayIcon) {
        m_trayIcon->Hide(hideTrayIcon);

        m_hideTrayIcon = hideTrayIcon;
    }

    if (disableUpdateCheck != m_disableUpdateCheck) {
        // For the non-portable version, update checking is done by another
        // process, and we're notified via an event.
        if (m_portable) {
            m_checkForUpdates = !disableUpdateCheck;
            if (m_checkForUpdates) {
                if (!m_updateChecker) {
                    SetTimer(Timer::kUpdateCheck,
                             GetNextUpdateDelay(lastUpdateCheck));
                }
            } else {
                if (m_updateChecker) {
                    m_updateChecker->Abort();
                } else {
                    KillTimer(Timer::kUpdateCheck);
                }

                ResetLastUpdateTime();
            }
        }

        if (disableUpdateCheck) {
            NotifyAboutAvailableUpdates(UserProfile::UpdateStatus{});
        } else {
            NotifyAboutAvailableUpdates(UserProfile::GetUpdateStatus(), true);
        }

        m_disableUpdateCheck = disableUpdateCheck;
    }

    if (dontAutoShowToolkit != m_dontAutoShowToolkit) {
        if (!dontAutoShowToolkit) {
            try {
                auto explorerPath = wil::GetWindowsDirectory<std::wstring>() +
                                    L"\\explorer.exe";

                m_explorerCrashMonitor.emplace(explorerPath);
            } catch (const std::exception& e) {
                LOG(L"%S", e.what());
            }
        } else {
            m_explorerCrashMonitor.reset();
        }

        m_dontAutoShowToolkit = dontAutoShowToolkit;
    }

    m_disableToolkitHotkey = disableToolkitHotkey;

    m_modTasksDlgDelay = modTasksDlgDelay;

    // A notice written since the last read. The first read only takes the
    // current one as seen, so a start doesn't repeat an old notice.
    if (m_crashGuardNotice && crashGuardNotice != *m_crashGuardNotice) {
        ShowCrashGuardNotice(crashGuardNoticeReason, crashGuardNoticeMod,
                             crashGuardNoticeProgram);
    }
    m_crashGuardNotice = crashGuardNotice;
}

void CMainWindow::NotifyAboutAvailableUpdates(
    UserProfile::UpdateStatus updateStatus,
    bool alwaysShowUpdateNotification) {
    m_lastUpdateStatus.emplace(std::move(updateStatus));

    if (alwaysShowUpdateNotification || m_lastUpdateStatus->newUpdatesFound) {
        ShowUpdateNotificationMessage(m_lastUpdateStatus->appUpdateAvailable,
                                      m_lastUpdateStatus->modUpdatesAvailable);
    }

    MarkAppUpdateAvailable(m_lastUpdateStatus->appUpdateAvailable);
}

void CMainWindow::Exit() {
    CloseUI();

    if (m_portable) {
        KillTimer(Timer::kHandleNewProcesses);
    }

    if (m_updateChecker) {
        m_updateChecker->Abort();
        m_exitWhenUpdateCheckDone = true;
    } else {
        if (m_checkForUpdates) {
            KillTimer(Timer::kUpdateCheck);
        }

        DestroyWindow();
    }
}

void CMainWindow::StopService(HWND hWnd) {
    struct CALLBACK_STATE {
        bool showOnTaskbar;
        bool verificationChecked;
        bool handlingOkButton;
    };

    CALLBACK_STATE callbackState{
        .showOnTaskbar = !hWnd,
    };

    TASKDIALOGCONFIG tdcTaskDialogConfig = {sizeof(TASKDIALOGCONFIG)};
    TASKDIALOG_BUTTON tbButtons[2];

    tbButtons[0].nButtonID = IDOK;
    tbButtons[0].pszButtonText =
        Functions::LoadStrFromRsrc(IDS_EXITDLG_BUTTON_EXIT);
    tbButtons[1].nButtonID = IDCANCEL;
    tbButtons[1].pszButtonText =
        Functions::LoadStrFromRsrc(IDS_EXITDLG_BUTTON_CANCEL);

    tdcTaskDialogConfig.hwndParent = hWnd ? hWnd : m_hWnd;
    tdcTaskDialogConfig.hInstance = GetModuleHandle(nullptr);
    tdcTaskDialogConfig.pszWindowTitle =
        Functions::LoadStrFromRsrc(IDS_EXITDLG_TITLE);
    tdcTaskDialogConfig.pszMainIcon = MAKEINTRESOURCE(IDR_MAINFRAME);
    tdcTaskDialogConfig.pszContent =
        Functions::LoadStrFromRsrc(IDS_EXITDLG_CONTENT);
    tdcTaskDialogConfig.cButtons = _countof(tbButtons);
    tdcTaskDialogConfig.pButtons = tbButtons;
    tdcTaskDialogConfig.nDefaultButton = IDOK;
    tdcTaskDialogConfig.pszVerificationText =
        Functions::LoadStrFromRsrc(IDS_EXITDLG_CHECKBOX_AUTOSTART);
    tdcTaskDialogConfig.pfCallback = [](HWND hWnd, UINT uNotification,
                                        WPARAM wParam, LPARAM lParam,
                                        LONG_PTR lpRefData) {
        auto& callbackState = *reinterpret_cast<CALLBACK_STATE*>(lpRefData);

        CWindow wnd(hWnd);

        switch (uNotification) {
            case TDN_DIALOG_CONSTRUCTED: {
                if (callbackState.showOnTaskbar) {
                    wnd.ModifyStyleEx(0, WS_EX_APPWINDOW);
                }

                bool languageRightToLeft =
                    Functions::IsRightToLeftLanguage(GetThreadUILanguage());
                wnd.ModifyStyleEx(languageRightToLeft ? 0 : WS_EX_LAYOUTRTL,
                                  languageRightToLeft ? WS_EX_LAYOUTRTL : 0);

                if (!Functions::IsRunAsAdmin()) {
                    wnd.SendMessage(TDM_SET_BUTTON_ELEVATION_REQUIRED_STATE,
                                    IDOK, TRUE);
                }
                break;
            }

            case TDN_VERIFICATION_CLICKED:
                callbackState.verificationChecked =
                    static_cast<BOOL>(wParam) != FALSE;
                break;

            case TDN_BUTTON_CLICKED:
                switch (wParam) {
                    case IDOK:
                        if (callbackState.handlingOkButton) {
                            return S_FALSE;
                        }

                        callbackState.handlingOkButton = true;

                        auto resetStateFlagOnScopeExit =
                            wil::scope_exit([&callbackState] {
                                callbackState.handlingOkButton = false;
                            });

                        try {
                            auto modulePath =
                                wil::GetModuleFileName<std::wstring>();
                            PCWSTR commandLine = L"-service-stop";
                            if (callbackState.verificationChecked) {
                                commandLine =
                                    L"-service-stop -also-no-autostart";
                            }

                            if ((int)(UINT_PTR)ShellExecute(
                                    nullptr, L"runas", modulePath.c_str(),
                                    commandLine, nullptr, SW_SHOWNORMAL) > 32) {
                                return S_OK;
                            }

                            THROW_LAST_ERROR_IF(GetLastError() !=
                                                ERROR_CANCELLED);
                        } catch (const std::exception& e) {
                            try {
                                std::string msg =
                                    "Exiting failed with the error below. If "
                                    "nothing else works, you can choose to "
                                    "send an exit signal to the AlaqEngine "
                                    "service. Send exit signal?\n\nError:\n";

                                msg += e.what();

                                if (::MessageBoxA(hWnd, msg.c_str(),
                                                  "Exiting failed",
                                                  MB_ICONERROR | MB_YESNO |
                                                      MB_DEFBUTTON2) == IDYES) {
                                    wil::unique_event namedEvent(::OpenEvent(
                                        EVENT_MODIFY_STATE, FALSE,
                                        ServiceCommon::
                                            kEmergencyStopEventName));
                                    THROW_LAST_ERROR_IF_NULL(namedEvent);

                                    namedEvent.SetEvent();
                                }
                            } catch (const std::exception& e) {
                                ::MessageBoxA(hWnd, e.what(), "Error",
                                              MB_ICONERROR);
                            }
                        }

                        return S_FALSE;  // leave dialog open
                }
                break;
        }

        return S_OK;
    };
    tdcTaskDialogConfig.lpCallbackData =
        reinterpret_cast<LONG_PTR>(&callbackState);

    BOOL bVerificationFlagChecked;
    ::TaskDialogIndirect(&tdcTaskDialogConfig, nullptr, nullptr,
                         &bVerificationFlagChecked);
}

void CMainWindow::RunUI(HWND hWnd) {
    if (!hWnd) {
        hWnd = m_hWnd;
    }

    try {
        UIControl::RunUIOrBringToFront(
            hWnd, !m_portable && !Functions::IsRunAsAdmin());
    } catch (const std::exception& e) {
        ::MessageBoxA(hWnd, e.what(), "Could not launch the UI process",
                      MB_ICONERROR);
    }
}

void CMainWindow::CloseUI() {
    try {
        UIControl::CloseUI();
    } catch (const std::exception& e) {
        LOG(L"CloseUI failed: %S", e.what());
    }
}

void CMainWindow::ShowUpdateNotificationMessage(bool appUpdateAvailable,
                                                int modUpdatesAvailable) {
    WCHAR message[AppTrayIcon::kMaxNotificationTooltipSize] = L"";

    if (appUpdateAvailable) {
        if (modUpdatesAvailable == 0) {
            wcsncpy_s(message,
                      Functions::LoadStrFromRsrc(IDS_NOTIFICATION_UPDATE_APP),
                      _TRUNCATE);
        } else if (modUpdatesAvailable == 1) {
            wcsncpy_s(
                message,
                Functions::LoadStrFromRsrc(IDS_NOTIFICATION_UPDATE_APP_MOD),
                _TRUNCATE);
        } else {
            _snwprintf_s(
                message, _TRUNCATE,
                Functions::LoadStrFromRsrc(IDS_NOTIFICATION_UPDATE_APP_MODS),
                modUpdatesAvailable);
        }
    } else {
        if (modUpdatesAvailable == 1) {
            wcsncpy_s(message,
                      Functions::LoadStrFromRsrc(IDS_NOTIFICATION_UPDATE_MOD),
                      _TRUNCATE);
        } else if (modUpdatesAvailable > 1) {
            _snwprintf_s(
                message, _TRUNCATE,
                Functions::LoadStrFromRsrc(IDS_NOTIFICATION_UPDATE_MODS),
                modUpdatesAvailable);
        }
    }

    if (*message != L'\0') {
        m_trayIcon->ShowNotificationMessage(message);
    }
}

void CMainWindow::MarkAppUpdateAvailable(bool appUpdateAvailable) {
    m_trayIcon->SetNotificationIconAndTooltip(
        appUpdateAvailable
            ? Functions::LoadStrFromRsrc(IDS_TRAYICON_TOOLTIP_UPDATE)
            : nullptr);
}

UINT CMainWindow::GetNextUpdateDelay(ULONGLONG lastUpdateCheck) {
    if (lastUpdateCheck == 0) {
        return kUpdateInitialDelay;
    }

    ULONGLONG now = wil::filetime::convert_100ns_to_msec(
        wil::filetime::to_int64(wil::filetime::get_system_time()));

    ULONGLONG nextUpdateDelay = kUpdateInitialDelay;
    ULONGLONG nextUpdateTime = lastUpdateCheck + kUpdateInterval;
    if (nextUpdateTime > now) {
        nextUpdateDelay = nextUpdateTime - now;
        if (nextUpdateDelay < kUpdateInitialDelay) {
            nextUpdateDelay = kUpdateInitialDelay;
        } else if (nextUpdateDelay > kUpdateInterval) {
            nextUpdateDelay = kUpdateInterval;
        }
    }

    return static_cast<UINT>(nextUpdateDelay);
}

void CMainWindow::SetLastUpdateTime() {
    ULONGLONG now = wil::filetime::convert_100ns_to_msec(
        wil::filetime::to_int64(wil::filetime::get_system_time()));

    try {
        auto settings =
            StorageManager::GetInstance().GetAppConfig(L"Settings", true);
        settings->SetString(L"LastUpdateCheck", std::to_wstring(now).c_str());
    } catch (const std::exception& e) {
        LOG(L"%S", e.what());
    }
}

void CMainWindow::ResetLastUpdateTime() {
    try {
        auto settings =
            StorageManager::GetInstance().GetAppConfig(L"Settings", true);
        settings->Remove(L"LastUpdateCheck");
    } catch (const std::exception& e) {
        LOG(L"%S", e.what());
    }
}

void CMainWindow::OpenUpdatePage() {
    PCWSTR url =
        L"https://github.com/asaadassi/shihab-alaq-releases/releases";

    if ((int)(UINT_PTR)ShellExecute(m_hWnd, nullptr, url, nullptr, nullptr,
                                    SW_SHOWNORMAL) <= 32) {
        MessageBox(
            L"Could not open the update page, please update Alaq manually",
            L"Error", MB_ICONERROR);
    }
}

void CMainWindow::ShowLoadedModsDialog() {
    if (m_modStatusesDlg) {
        ::SetForegroundWindow(*m_modStatusesDlg);
        return;
    }

    m_modStatusesDlg.emplace(CTaskManagerDlg::DialogOptions{
        .dataSource = CTaskManagerDlg::DataSource::kModStatus,
        .sessionManagerProcessId = m_serviceInfo.processId,
        .sessionManagerProcessCreationTime = m_serviceInfo.processCreationTime,
        .runButtonCallback = [this](HWND hWnd) { RunUI(hWnd); },
        .finalMessageCallback =
            [this](HWND hWnd) {
                m_modStatusesDlg.reset();
                m_modStatusesChangeNotification.reset();
            }});

    if (!m_modStatusesDlg->Create(m_hWnd)) {
        m_modStatusesDlg.reset();
        return;
    }

    m_modStatusesDlg->ShowWindow(SW_SHOWNORMAL);

    try {
        m_modStatusesChangeNotification.emplace(L"mod-status");
    } catch (const std::exception& e) {
        LOG(L"Statuses ChangeNotification failed: %S", e.what());
    }
}

void CMainWindow::ShowToolkitDialog(bool triggeredBySystemInstability) {
    if (m_toolkitDlg) {
        ::SetForegroundWindow(*m_toolkitDlg);
        return;
    }

    bool createInactive = triggeredBySystemInstability;

    m_toolkitDlg.emplace(CToolkitDlg::DialogOptions{
        .createInactive = createInactive,
        .showTaskbarCrashExplanation = triggeredBySystemInstability,
        .runButtonCallback = [this](HWND hWnd) { RunUI(hWnd); },
        .loadedModsButtonCallback =
            [this](HWND hWnd) { ShowLoadedModsDialog(); },
        .exitButtonCallback =
            [this](HWND hWnd) {
                if (m_portable) {
                    Exit();
                } else {
                    StopService(hWnd);
                }
            },
        .safeModeButtonCallback =
            [this](HWND hWnd) {
                if (::MessageBox(
                        hWnd, Functions::LoadStrFromRsrc(IDS_SAFE_MODE_TEXT),
                        Functions::LoadStrFromRsrc(IDS_SAFE_MODE_TITLE),
                        MB_ICONWARNING | MB_OKCANCEL | MB_DEFBUTTON2) == IDOK) {
                    try {
                        SwitchToSafeMode();
                    } catch (const std::exception& e) {
                        ::MessageBoxA(hWnd, e.what(), "Error", MB_ICONERROR);
                    }
                }
            },
        .finalMessageCallback = [this](HWND hWnd) { m_toolkitDlg.reset(); }});

    if (!m_toolkitDlg->Create(m_hWnd)) {
        m_toolkitDlg.reset();
        return;
    }

    m_toolkitDlg->ShowWindow(createInactive ? SW_SHOWNOACTIVATE
                                            : SW_SHOWNORMAL);
}

void CMainWindow::SwitchToSafeMode() {
    try {
        auto modulePath = wil::GetModuleFileName<std::wstring>();

        std::wstring commandLine = L"\"" + modulePath + L"\" -wait";

        STARTUPINFO si = {sizeof(STARTUPINFO)};
        wil::unique_process_information process;

        THROW_IF_WIN32_BOOL_FALSE(CreateProcess(
            modulePath.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
            NORMAL_PRIORITY_CLASS, nullptr, nullptr, &si, &process));
    } catch (const std::exception& e) {
        LOG(L"%S", e.what());
    }

    if (m_portable) {
        auto settings =
            StorageManager::GetInstance().GetAppConfig(L"Settings", true);
        settings->SetInt(L"SafeMode", 1);

        Exit();
    } else {
        wil::unique_event namedEvent(::OpenEvent(
            EVENT_MODIFY_STATE, FALSE, ServiceCommon::kSafeModeStopEventName));
        THROW_LAST_ERROR_IF_NULL(namedEvent);

        namedEvent.SetEvent();
    }
}

void CMainWindow::DisableAllMods() {
    try {
        if (m_portable) {
            // No service: this process may write the mods' configuration, and
            // reads the notice right after.
            ModCrashGuard::DisableAllMods(L"user");
            LoadSettings();
        } else {
            // The service writes the mods' configuration and then the notice.
            wil::unique_event namedEvent(
                ::OpenEvent(EVENT_MODIFY_STATE, FALSE,
                            ServiceCommon::kDisableAllModsEventName));
            THROW_LAST_ERROR_IF_NULL(namedEvent);

            namedEvent.SetEvent();
        }
    } catch (const std::exception& e) {
        ::MessageBoxA(m_hWnd, e.what(), "Error", MB_ICONERROR);
    }
}

void CMainWindow::ShowCrashGuardNotice(const std::wstring& reason,
                                       const std::wstring& modName,
                                       const std::wstring& program) {
    if (!m_trayIcon) {
        return;
    }

    std::wstring text;
    if (reason == L"crash") {
        text = Functions::ReplaceAll(
            Functions::ReplaceAll(
                Functions::LoadStrFromRsrc(IDS_CRASH_GUARD_MOD_DISABLED),
                L"{mod}", modName),
            L"{program}", program);
    } else if (reason == L"explorer") {
        text = Functions::LoadStrFromRsrc(IDS_CRASH_GUARD_EXPLORER_DISABLED);
    } else if (reason == L"user") {
        text = Functions::LoadStrFromRsrc(IDS_CRASH_GUARD_ALL_DISABLED);
    } else {
        return;
    }

    m_trayIcon->ShowNotificationMessage(text.c_str());
}

void CMainWindow::HandleExplorerCrash(int explorerCrashCount) {
    VERBOSE(L"Detected %d explorer crashes", explorerCrashCount);

    ULONGLONG currentTickCount = GetTickCount64();

    if (explorerCrashCount >= 2 ||
        currentTickCount - m_explorerLastTerminatedTickCount <=
            kExplorerSecondCrashMaxPeriod) {
        bool skipShowingToolkit = false;
        ULONGLONG taskbarProcessCreationTime = GetTaskbarProcessCreationTime();
        if (taskbarProcessCreationTime) {
            ULONGLONG currentTime =
                wil::filetime::to_int64(wil::filetime::get_system_time());
            ULONGLONG msSinceCreationTime =
                wil::filetime::convert_100ns_to_msec(
                    currentTime - taskbarProcessCreationTime);
            if (msSinceCreationTime > kExplorerSecondCrashMaxPeriod) {
                VERBOSE(
                    L"Taskbar process created %u ms ago, not showing toolkit",
                    msSinceCreationTime);
                skipShowingToolkit = true;
            }
        }

        if (!skipShowingToolkit && !m_toolkitDlg) {
            ShowToolkitDialog(/*triggeredBySystemInstability=*/true);
        }
    }

    m_explorerLastTerminatedTickCount = currentTickCount;
}
