#include "unified_telemetry.h"

#include <windows.h>
#include <setupapi.h>

#include <algorithm>
#include <mutex>
#include <string>

#pragma comment(lib, "setupapi.lib")

static std::mutex g_npu_mutex;

static bool ContainsNpuName(
    const std::wstring& name)
{
    return
        name.find(L"NPU") !=
            std::wstring::npos ||
        name.find(L"npu") !=
            std::wstring::npos ||
        name.find(L"Hexagon") !=
            std::wstring::npos ||
        name.find(L"hexagon") !=
            std::wstring::npos ||
        name.find(L"Qualcomm AI") !=
            std::wstring::npos ||
        name.find(L"Qualcomm NPU") !=
            std::wstring::npos;
}

static bool ContainsSnapdragonName(
    const std::wstring& name)
{
    return
        name.find(L"Qualcomm") !=
            std::wstring::npos ||
        name.find(L"qualcomm") !=
            std::wstring::npos ||
        name.find(L"Snapdragon") !=
            std::wstring::npos ||
        name.find(L"snapdragon") !=
            std::wstring::npos ||
        name.find(L"Hexagon") !=
            std::wstring::npos ||
        name.find(L"hexagon") !=
            std::wstring::npos;
}

static bool DetectNpu(
    bool& npu_present,
    bool& snapdragon_npu)
{
    npu_present = false;
    snapdragon_npu = false;

    HDEVINFO device_info =
        SetupDiGetClassDevsW(
            nullptr,
            nullptr,
            nullptr,
            DIGCF_ALLCLASSES |
            DIGCF_PRESENT);

    if (device_info ==
        INVALID_HANDLE_VALUE)
    {
        return false;
    }

    SP_DEVINFO_DATA device_data{};
    device_data.cbSize =
        sizeof(SP_DEVINFO_DATA);

    for (DWORD index = 0;
         SetupDiEnumDeviceInfo(
             device_info,
             index,
             &device_data);
         ++index)
    {
        WCHAR friendly_name[512]{};
        WCHAR device_desc[512]{};

        DWORD friendly_size =
            sizeof(friendly_name);

        DWORD desc_size =
            sizeof(device_desc);

        DWORD friendly_type = 0;
        DWORD desc_type = 0;

        bool friendly_available =
            SetupDiGetDeviceRegistryPropertyW(
                device_info,
                &device_data,
                SPDRP_FRIENDLYNAME,
                &friendly_type,
                reinterpret_cast<PBYTE>(
                    friendly_name),
                friendly_size,
                nullptr) != FALSE;

        bool description_available =
            SetupDiGetDeviceRegistryPropertyW(
                device_info,
                &device_data,
                SPDRP_DEVICEDESC,
                &desc_type,
                reinterpret_cast<PBYTE>(
                    device_desc),
                desc_size,
                nullptr) != FALSE;

        std::wstring friendly =
            friendly_available
                ? std::wstring(friendly_name)
                : std::wstring();

        std::wstring description =
            description_available
                ? std::wstring(device_desc)
                : std::wstring();

        if (!ContainsNpuName(friendly) &&
            !ContainsNpuName(description))
        {
            continue;
        }

        npu_present = true;

        if (ContainsSnapdragonName(friendly) ||
            ContainsSnapdragonName(description))
        {
            snapdragon_npu = true;
        }

        break;
    }

    SetupDiDestroyDeviceInfoList(
        device_info);

    return true;
}

NpuTelemetry CollectNpuTelemetry()
{
    std::lock_guard<std::mutex> lock(
        g_npu_mutex);

    NpuTelemetry telemetry{};

    bool npu_present = false;
    bool snapdragon_npu = false;

    if (!DetectNpu(
            npu_present,
            snapdragon_npu))
    {
        telemetry.npu_present = false;
        telemetry.snapdragon_npu = false;

        telemetry.utilization_available =
            false;

        telemetry.frequency_available =
            false;

        telemetry.temperature_available =
            false;

        return telemetry;
    }

    telemetry.npu_present =
        npu_present;

    telemetry.snapdragon_npu =
        snapdragon_npu;

    telemetry.utilization_percent =
        0.0;

    telemetry.frequency_mhz =
        0.0;

    telemetry.temperature_c =
        0.0;

    telemetry.utilization_available =
        false;

    telemetry.frequency_available =
        false;

    telemetry.temperature_available =
        false;

    return telemetry;
}