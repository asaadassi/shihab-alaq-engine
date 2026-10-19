#pragma once

namespace ServiceCommon {

static inline constexpr WCHAR kName[] = L"ShihabStyler";

static inline constexpr WCHAR kInfoFileMappingName[] =
    L"Global\\ShihabStylerServiceInfoFileMapping";

static inline constexpr WCHAR kMutexName[] = L"Global\\ShihabStylerServiceMutex";

static inline constexpr WCHAR kScanForProcessesEventName[] =
    L"Global\\ShihabStylerScanForProcesses";

static inline constexpr WCHAR kEmergencyStopEventName[] =
    L"Global\\ShihabStylerServiceEmergencyStopEvent";

static inline constexpr WCHAR kSafeModeStopEventName[] =
    L"Global\\ShihabStylerServiceSafeModeStopEvent";

struct ServiceInfo {
    DWORD version;
    DWORD processId;
    ULONGLONG processCreationTime;
};

}  // namespace ServiceCommon
