#include "unified_telemetry.h"

#include <windows.h>
#include <wbemidl.h>

#include <algorithm>
#include <cmath>

#pragma comment(lib, "wbemuuid.lib")

static bool ReadCpuTemperature(
    double& temperature_c)
{
    temperature_c = 0.0;

    HRESULT com_result =
        CoInitializeEx(
            nullptr,
            COINIT_MULTITHREADED);

    bool should_uninitialize =
        SUCCEEDED(com_result);

    if (FAILED(com_result) &&
        com_result != RPC_E_CHANGED_MODE)
    {
        return false;
    }

    HRESULT security_result =
        CoInitializeSecurity(
            nullptr,
            -1,
            nullptr,
            nullptr,
            RPC_C_AUTHN_LEVEL_DEFAULT,
            RPC_C_IMP_LEVEL_IMPERSONATE,
            nullptr,
            EOAC_NONE,
            nullptr);

    if (FAILED(security_result) &&
        security_result != RPC_E_TOO_LATE)
    {
        if (should_uninitialize)
            CoUninitialize();

        return false;
    }

    IWbemLocator* locator = nullptr;
    IWbemServices* services = nullptr;
    IEnumWbemClassObject* enumerator = nullptr;

    HRESULT result =
        CoCreateInstance(
            CLSID_WbemLocator,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_IWbemLocator,
            reinterpret_cast<void**>(
                &locator));

    if (FAILED(result) ||
        locator == nullptr)
    {
        if (should_uninitialize)
            CoUninitialize();

        return false;
    }

    BSTR namespace_name =
        SysAllocString(L"ROOT\\WMI");

    result =
        locator->ConnectServer(
            namespace_name,
            nullptr,
            nullptr,
            nullptr,
            0,
            nullptr,
            nullptr,
            &services);

    SysFreeString(namespace_name);

    if (FAILED(result) ||
        services == nullptr)
    {
        locator->Release();

        if (should_uninitialize)
            CoUninitialize();

        return false;
    }

    result =
        CoSetProxyBlanket(
            services,
            RPC_C_AUTHN_WINNT,
            RPC_C_AUTHZ_NONE,
            nullptr,
            RPC_C_AUTHN_LEVEL_CALL,
            RPC_C_IMP_LEVEL_IMPERSONATE,
            nullptr,
            EOAC_NONE);

    if (FAILED(result))
    {
        services->Release();
        locator->Release();

        if (should_uninitialize)
            CoUninitialize();

        return false;
    }

    BSTR language =
        SysAllocString(L"WQL");

    BSTR query =
        SysAllocString(
            L"SELECT CurrentTemperature "
            L"FROM MSAcpi_ThermalZoneTemperature");

    result =
        services->ExecQuery(
            language,
            query,
            WBEM_FLAG_FORWARD_ONLY |
            WBEM_FLAG_RETURN_IMMEDIATELY,
            nullptr,
            &enumerator);

    SysFreeString(language);
    SysFreeString(query);

    if (FAILED(result) ||
        enumerator == nullptr)
    {
        services->Release();
        locator->Release();

        if (should_uninitialize)
            CoUninitialize();

        return false;
    }

    IWbemClassObject* object = nullptr;
    ULONG returned = 0;

    result =
        enumerator->Next(
            WBEM_INFINITE,
            1,
            &object,
            &returned);

    if (SUCCEEDED(result) &&
        returned == 1 &&
        object != nullptr)
    {
        VARIANT value{};
        VariantInit(&value);

        result =
            object->Get(
                L"CurrentTemperature",
                0,
                &value,
                nullptr,
                nullptr);

        if (SUCCEEDED(result) &&
            (value.vt == VT_I4 ||
             value.vt == VT_UI4 ||
             value.vt == VT_I8 ||
             value.vt == VT_UI8))
        {
            double raw_temperature = 0.0;

            if (value.vt == VT_I4)
                raw_temperature =
                    static_cast<double>(
                        value.lVal);
            else if (value.vt == VT_UI4)
                raw_temperature =
                    static_cast<double>(
                        value.ulVal);
            else if (value.vt == VT_I8)
                raw_temperature =
                    static_cast<double>(
                        value.llVal);
            else
                raw_temperature =
                    static_cast<double>(
                        value.ullVal);

            double converted =
                (raw_temperature / 10.0) -
                273.15;

            if (std::isfinite(converted) &&
                converted >= -40.0 &&
                converted <= 150.0)
            {
                temperature_c = converted;
            }
        }

        VariantClear(&value);
        object->Release();
    }

    enumerator->Release();
    services->Release();
    locator->Release();

    if (should_uninitialize)
        CoUninitialize();

    return temperature_c != 0.0;
}

ThermalTelemetry CollectThermalTelemetry()
{
    ThermalTelemetry telemetry{};

    double temperature = 0.0;

    if (ReadCpuTemperature(
            temperature))
    {
        telemetry.cpu_temperature_c =
            temperature;

        telemetry.cpu_temperature_available =
            true;

        telemetry.thermal_available =
            true;
    }

    telemetry.throttle_event = false;
    telemetry.cooling_mode = 0;

    return telemetry;
}