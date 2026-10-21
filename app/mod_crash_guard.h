#pragma once

// Shihab Alaq: the crash guard. Runs in the service, which may write the mods'
// configuration. Every program crash Windows logs (Application Error, event
// 1000) is checked as it is logged: when the faulting module is a mod's DLL,
// that mod is turned off at once. The subscription resumes after the last
// crash it handled, so crashes logged while the engine wasn't running, such as
// just before a shutdown, are handled on the next start, before any mod loads.
// Explorer crashing again and again in a module which isn't a mod turns all
// mods off, since no single one can be blamed.
class ModCrashGuard {
   public:
    ModCrashGuard();

    // Signaled when new crash events are available; manual reset, drained by
    // HandleNewEvents.
    HANDLE GetEventHandle() const;
    void HandleNewEvents();

    // Turns every mod which is on off, recording why ("crash", "explorer" or
    // "user"), and tells the tray icon. Returns how many were turned off.
    static int DisableAllMods(PCWSTR reason);

   private:
    struct Crash {
        std::wstring program;
        std::wstring programPath;
        std::wstring faultingModule;
        DWORD exceptionCode = 0;
        ULONGLONG time = 0;  // FILETIME, UTC
    };

    std::optional<Crash> ReadCrash(EVT_HANDLE eventHandle);
    void HandleCrash(const Crash& crash);

    // Explorer crashes this many times within the period below, outside any
    // mod, and all mods are turned off.
    constexpr static size_t kExplorerCrashLimit = 3;
    constexpr static ULONGLONG kExplorerCrashPeriod =
        2ULL * 60 * 10000000;  // 2 minutes, in FILETIME units

    wil::unique_event m_event;
    wil::unique_evt_handle m_subscription;
    std::wstring m_explorerPath;
    std::vector<ULONGLONG> m_explorerCrashTimes;
};
