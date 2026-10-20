#pragma once

namespace ServiceCommon {

static inline constexpr WCHAR kName[] = L"ShihabAlaq";

static inline constexpr WCHAR kInfoFileMappingName[] =
    L"Global\\ShihabAlaqServiceInfoFileMapping";

static inline constexpr WCHAR kMutexName[] = L"Global\\ShihabAlaqServiceMutex";

static inline constexpr WCHAR kScanForProcessesEventName[] =
    L"Global\\ShihabAlaqScanForProcesses";

static inline constexpr WCHAR kEmergencyStopEventName[] =
    L"Global\\ShihabAlaqServiceEmergencyStopEvent";

static inline constexpr WCHAR kSafeModeStopEventName[] =
    L"Global\\ShihabAlaqServiceSafeModeStopEvent";

// Shihab Alaq: set by the tray icon's "Turn off all mods"; the service, which
// may write the mods' configuration, turns them off.
static inline constexpr WCHAR kDisableAllModsEventName[] =
    L"Global\\ShihabAlaqServiceDisableAllModsEvent";

struct ServiceInfo {
    DWORD version;
    DWORD processId;
    ULONGLONG processCreationTime;
};

}  // namespace ServiceCommon
