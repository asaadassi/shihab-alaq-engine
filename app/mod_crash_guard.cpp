#include "stdafx.h"

#include "mod_crash_guard.h"

#include "logger.h"
#include "storage_manager.h"

namespace {

constexpr ULONGLONG kUnixEpochFileTime = 116444736000000000ULL;

// Before any crash was handled: the crashes of the last day.
constexpr WCHAR kFirstRunTimeFilter[] =
    L"TimeCreated[timediff(@SystemTime) <= 86400000]";

// A FILETIME the way the event log's XPath writes times, to the 100ns, so that
// "after this time" never matches the event the time was taken from.
std::wstring FileTimeToXPathTime(ULONGLONG fileTime) {
    FILETIME ft{static_cast<DWORD>(fileTime),
                static_cast<DWORD>(fileTime >> 32)};
    SYSTEMTIME st;
    THROW_IF_WIN32_BOOL_FALSE(FileTimeToSystemTime(&ft, &st));
    return std::format(L"{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:07}Z", st.wYear,
                       st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                       fileTime % 10000000);
}

int FileTimeToUnixSeconds(ULONGLONG fileTime) {
    if (fileTime < kUnixEpochFileTime) {
        return 0;
    }

    return static_cast<int>(((fileTime - kUnixEpochFileTime) / 10000000) &
                            0x7FFFFFFF);
}

std::vector<std::wstring> GetModNames() {
    std::vector<std::wstring> names;
    StorageManager::GetInstance().EnumEngineMods(
        [&names](PCWSTR modName) { names.emplace_back(modName); });
    return names;
}

// The values Shihab Alaq shows next to a mod the guard turned off, and clears
// when the mod is turned on again (docs/ENGINE.md in the app's repository).
void DisableMod(PCWSTR modName,
                PCWSTR reason,
                PCWSTR program,
                DWORD exceptionCode,
                ULONGLONG time) {
    auto config =
        StorageManager::GetInstance().GetEngineModConfig(modName, true);
    config->SetString(L"GuardReason", reason);
    config->SetInt(L"GuardTime", FileTimeToUnixSeconds(time));
    config->SetString(L"GuardProgram", program);
    config->SetString(
        L"GuardCode",
        exceptionCode ? std::format(L"0x{:08X}", exceptionCode).c_str() : L"");
    // Last: this is what makes every process unload the mod.
    config->SetInt(L"Disabled", 1);

    LOG(L"Crash guard turned off %s (%s, %s)", modName, reason, program);
}

// The tray icon of every session watches Settings and shows a notice when
// CrashGuardNotice changes.
void NotifyTray(PCWSTR reason, PCWSTR modName, PCWSTR program) {
    auto settings =
        StorageManager::GetInstance().GetAppConfig(L"Settings", true);
    settings->SetString(L"CrashGuardNoticeReason", reason);
    settings->SetString(L"CrashGuardNoticeMod", modName);
    settings->SetString(L"CrashGuardNoticeProgram", program);
    // Last: the notice is read once this changes.
    int notice = settings->GetInt(L"CrashGuardNotice").value_or(0);
    settings->SetInt(L"CrashGuardNotice", (notice + 1) & 0x7FFFFFFF);
}

}  // namespace

ModCrashGuard::ModCrashGuard()
    : m_explorerPath(wil::GetWindowsDirectory<std::wstring>() +
                     L"\\explorer.exe") {
    // Manual reset, signaled: the first drain handles what was logged before.
    m_event.reset(CreateEvent(nullptr, TRUE, TRUE, nullptr));
    THROW_LAST_ERROR_IF_NULL(m_event);

    std::wstring timeFilter = kFirstRunTimeFilter;
    auto lastEventTime =
        StorageManager::GetInstance()
            .GetAppConfig(L"CrashGuard", false)
            ->GetString(L"LastEventTime");
    // Written by this class only, but checked before it becomes part of a
    // query.
    if (lastEventTime && !lastEventTime->empty() &&
        lastEventTime->find_first_not_of(L"0123456789-:.TZ") ==
            std::wstring::npos) {
        timeFilter = L"TimeCreated[@SystemTime>'" + *lastEventTime + L"']";
    }

    std::wstring query =
        L"*[System[Provider[@Name='Application Error'] and (EventID=1000) "
        L"and " +
        timeFilter + L"]]";

    // From the oldest matching record: the crashes logged since the last one
    // handled, then every new one.
    m_subscription.reset(EvtSubscribe(nullptr, m_event.get(), L"Application",
                                      query.c_str(), nullptr, nullptr, nullptr,
                                      EvtSubscribeStartAtOldestRecord));
    THROW_LAST_ERROR_IF_NULL(m_subscription);
}

HANDLE ModCrashGuard::GetEventHandle() const {
    return m_event.get();
}

void ModCrashGuard::HandleNewEvents() {
    // Reset before draining, so that an event which arrives during the drain
    // keeps the handle signaled (as in EventViewerCrashMonitor).
    ResetEvent(m_event.get());

    ULONGLONG lastEventTime = 0;

    while (true) {
        wil::unique_evt_handle eventHandle;
        DWORD dwReturned;
        if (!EvtNext(m_subscription.get(), 1, &eventHandle, INFINITE, 0,
                     &dwReturned)) {
            THROW_LAST_ERROR_IF(GetLastError() != ERROR_NO_MORE_ITEMS);
            break;
        }

        try {
            if (auto crash = ReadCrash(eventHandle.get())) {
                HandleCrash(*crash);
                if (crash->time > lastEventTime) {
                    lastEventTime = crash->time;
                }
            }
        } catch (const std::exception& e) {
            // One unreadable event doesn't stop the others.
            LOG(L"Crash guard: event failed: %S", e.what());
        }
    }

    if (lastEventTime) {
        StorageManager::GetInstance()
            .GetAppConfig(L"CrashGuard", true)
            ->SetString(L"LastEventTime",
                        FileTimeToXPathTime(lastEventTime).c_str());
    }
}

// static
int ModCrashGuard::DisableAllMods(PCWSTR reason) {
    ULONGLONG now = wil::filetime::to_int64(wil::filetime::get_system_time());

    int count = 0;
    for (const auto& modName : GetModNames()) {
        try {
            auto config = StorageManager::GetInstance().GetEngineModConfig(
                modName.c_str(), false);
            if (!config->GetString(L"LibraryFileName") ||
                config->GetInt(L"Disabled").value_or(0)) {
                continue;
            }

            DisableMod(modName.c_str(), reason, L"", 0, now);
            count++;
        } catch (const std::exception& e) {
            // The others are still turned off.
            LOG(L"Crash guard: turning off %s failed: %S", modName.c_str(),
                e.what());
        }
    }

    // The user who asked is told even when everything was already off.
    if (count > 0 || wcscmp(reason, L"user") == 0) {
        NotifyTray(reason, L"", L"");
    }
    return count;
}

std::optional<ModCrashGuard::Crash> ModCrashGuard::ReadCrash(
    EVT_HANDLE eventHandle) {
    // The event's own data. For event 1000: 0 program name, 3 faulting module
    // name, 6 exception code, 10 program path.
    wil::unique_evt_handle context(
        EvtCreateRenderContext(0, nullptr, EvtRenderContextUser));
    THROW_LAST_ERROR_IF_NULL(context);

    DWORD dwBufferUsed = 0;
    DWORD dwPropertyCount = 0;
    if (EvtRender(context.get(), eventHandle, EvtRenderEventValues, 0, nullptr,
                  &dwBufferUsed, &dwPropertyCount)) {
        throw std::logic_error("Unexpected result from EvtRender");
    }
    THROW_LAST_ERROR_IF(GetLastError() != ERROR_INSUFFICIENT_BUFFER);

    auto buffer = std::make_unique<BYTE[]>(dwBufferUsed);
    THROW_IF_WIN32_BOOL_FALSE(EvtRender(context.get(), eventHandle,
                                        EvtRenderEventValues, dwBufferUsed,
                                        buffer.get(), &dwBufferUsed,
                                        &dwPropertyCount));

    if (dwPropertyCount < 11) {
        LOG(L"Crash guard: not enough property values (%u)", dwPropertyCount);
        return std::nullopt;
    }

    auto values = reinterpret_cast<const EVT_VARIANT*>(buffer.get());
    auto stringAt = [values](int i) {
        return values[i].Type == EvtVarTypeString && values[i].StringVal
                   ? std::wstring(values[i].StringVal)
                   : std::wstring();
    };

    Crash crash;
    crash.program = stringAt(0);
    crash.faultingModule = stringAt(3);
    crash.programPath = stringAt(10);
    if (values[6].Type == EvtVarTypeHexInt32) {
        crash.exceptionCode = values[6].UInt32Val;
    }

    // The time it was logged, from the system part of the event.
    wil::unique_evt_handle systemContext(
        EvtCreateRenderContext(0, nullptr, EvtRenderContextSystem));
    THROW_LAST_ERROR_IF_NULL(systemContext);

    if (EvtRender(systemContext.get(), eventHandle, EvtRenderEventValues, 0,
                  nullptr, &dwBufferUsed, &dwPropertyCount)) {
        throw std::logic_error("Unexpected result from EvtRender");
    }
    THROW_LAST_ERROR_IF(GetLastError() != ERROR_INSUFFICIENT_BUFFER);

    auto systemBuffer = std::make_unique<BYTE[]>(dwBufferUsed);
    THROW_IF_WIN32_BOOL_FALSE(EvtRender(systemContext.get(), eventHandle,
                                        EvtRenderEventValues, dwBufferUsed,
                                        systemBuffer.get(), &dwBufferUsed,
                                        &dwPropertyCount));

    auto systemValues = reinterpret_cast<const EVT_VARIANT*>(systemBuffer.get());
    if (dwPropertyCount > static_cast<DWORD>(EvtSystemTimeCreated) &&
        systemValues[EvtSystemTimeCreated].Type == EvtVarTypeFileTime) {
        crash.time = systemValues[EvtSystemTimeCreated].FileTimeVal;
    } else {
        crash.time = wil::filetime::to_int64(wil::filetime::get_system_time());
    }

    if (crash.faultingModule.empty()) {
        return std::nullopt;
    }

    return crash;
}

void ModCrashGuard::HandleCrash(const Crash& crash) {
    // Every install gives a mod's DLL a new file name, the same in every
    // architecture folder, so the faulting module names one mod exactly, and an
    // old crash of a since-updated mod names none.
    for (const auto& modName : GetModNames()) {
        auto config = StorageManager::GetInstance().GetEngineModConfig(
            modName.c_str(), false);
        auto libraryFileName = config->GetString(L"LibraryFileName");
        if (!libraryFileName ||
            _wcsicmp(libraryFileName->c_str(), crash.faultingModule.c_str()) !=
                0) {
            continue;
        }

        if (!config->GetInt(L"Disabled").value_or(0)) {
            DisableMod(modName.c_str(), L"crash", crash.program.c_str(),
                       crash.exceptionCode, crash.time);
            NotifyTray(L"crash", modName.c_str(), crash.program.c_str());
        }
        return;
    }

    // Not in a mod. Explorer crashing again and again may still be a mod's
    // doing, through a hook, so all of them are turned off.
    if (_wcsicmp(crash.programPath.c_str(), m_explorerPath.c_str()) != 0) {
        return;
    }

    std::erase_if(m_explorerCrashTimes, [&crash](ULONGLONG t) {
        return t + kExplorerCrashPeriod < crash.time;
    });
    m_explorerCrashTimes.push_back(crash.time);

    if (m_explorerCrashTimes.size() >= kExplorerCrashLimit) {
        m_explorerCrashTimes.clear();
        LOG(L"Crash guard: explorer keeps crashing, turning all mods off");
        DisableAllMods(L"explorer");
    }
}
