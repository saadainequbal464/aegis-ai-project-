#include "foreground_workload.h"
#include "unified_telemetry.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <chrono>
#include <cmath>
#include <mutex>
#include <algorithm>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

static std::atomic<bool> g_running{false};
static std::atomic<bool> g_stop{false};
static std::atomic<int> g_level{0};
static std::atomic<double> g_intensity{0.0};

static std::thread g_thread;
static HWND g_window = nullptr;
static std::mutex g_window_mutex;

static const wchar_t* kWindowClass = L"AEGISForegroundWorkload";

static ForegroundWorkloadLevel GetLevel()
{
    int value = g_level.load();

    if (value == 1)
        return ForegroundWorkloadLevel::MEDIUM;

    if (value == 2)
        return ForegroundWorkloadLevel::HIGH;

    if (value == 3)
        return ForegroundWorkloadLevel::PEAK;

    return ForegroundWorkloadLevel::LOW;
}

static double GetIntensity()
{
    double value = g_intensity.load();

    if (value < 0.0)
        return 0.0;

    if (value > 1.0)
        return 1.0;

    return value;
}

static double GetLevelBaseIntensity(
    ForegroundWorkloadLevel level)
{
    switch (level)
    {
    case ForegroundWorkloadLevel::LOW:
        return 0.10;

    case ForegroundWorkloadLevel::MEDIUM:
        return 0.35;

    case ForegroundWorkloadLevel::HIGH:
        return 0.70;

    case ForegroundWorkloadLevel::PEAK:
        return 1.0;
    }

    return 0.0;
}

static double GetEffectiveIntensity()
{
    double base =
        GetLevelBaseIntensity(
            GetLevel());

    double requested =
        GetIntensity();

    double effective =
        base +
        requested * (1.0 - base);

    if (effective < 0.0)
        effective = 0.0;

    if (effective > 1.0)
        effective = 1.0;

    return effective;
}

static LRESULT CALLBACK WindowProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    if (message == WM_CLOSE)
    {
        g_stop.store(true);
        DestroyWindow(hwnd);
        return 0;
    }

    if (message == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(
        hwnd,
        message,
        wParam,
        lParam);
}

static bool CreateWorkloadWindow(
    HINSTANCE instance)
{
    WNDCLASSW wc{};

    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClass;
    wc.hCursor =
        LoadCursorW(
            nullptr,
            MAKEINTRESOURCEW(32512));

    if (!RegisterClassW(&wc))
    {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return false;
    }

    HWND hwnd =
        CreateWindowExW(
            0,
            kWindowClass,
            L"AEGIS Foreground Workload",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            1280,
            720,
            nullptr,
            nullptr,
            instance,
            nullptr);

    if (!hwnd)
        return false;

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    {
        std::lock_guard<std::mutex> lock(
            g_window_mutex);

        g_window = hwnd;
    }

    return true;
}

static void DestroyWorkloadWindow()
{
    HWND hwnd = nullptr;

    {
        std::lock_guard<std::mutex> lock(
            g_window_mutex);

        hwnd = g_window;
        g_window = nullptr;
    }

    if (hwnd)
        DestroyWindow(hwnd);
}

static bool CreateDeviceAndSwapChain(
    HWND hwnd,
    ID3D11Device** device,
    ID3D11DeviceContext** context,
    IDXGISwapChain** swap_chain,
    ID3D11RenderTargetView** render_target)
{
    DXGI_SWAP_CHAIN_DESC desc{};

    desc.BufferCount = 2;
    desc.BufferDesc.Width = 1280;
    desc.BufferDesc.Height = 720;
    desc.BufferDesc.Format =
        DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferDesc.RefreshRate.Numerator = 60;
    desc.BufferDesc.RefreshRate.Denominator = 1;
    desc.BufferUsage =
        DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = hwnd;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect =
        DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL feature_levels[] =
    {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };

    D3D_FEATURE_LEVEL feature_level{};

    HRESULT hr =
        D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            0,
            feature_levels,
            3,
            D3D11_SDK_VERSION,
            &desc,
            swap_chain,
            device,
            &feature_level,
            context);

    if (FAILED(hr))
        return false;

    ID3D11Texture2D* back_buffer = nullptr;

    hr =
        (*swap_chain)->GetBuffer(
            0,
            __uuidof(ID3D11Texture2D),
            reinterpret_cast<void**>(
                &back_buffer));

    if (FAILED(hr))
        return false;

    hr =
        (*device)->CreateRenderTargetView(
            back_buffer,
            nullptr,
            render_target);

    back_buffer->Release();

    if (FAILED(hr))
        return false;

    return true;
}

static void RenderFrame(
    ID3D11DeviceContext* context,
    ID3D11RenderTargetView* render_target,
    double phase,
    double intensity)
{
    float base =
        static_cast<float>(
            0.06 +
            0.22 * intensity);

    float r =
        static_cast<float>(
            0.08 +
            base *
            std::sin(phase));

    float g =
        static_cast<float>(
            0.12 +
            base *
            std::sin(
                phase * 1.7));

    float b =
        static_cast<float>(
            0.18 +
            base *
            std::cos(
                phase * 0.8));

    r =
        std::clamp(
            r,
            0.0f,
            1.0f);

    g =
        std::clamp(
            g,
            0.0f,
            1.0f);

    b =
        std::clamp(
            b,
            0.0f,
            1.0f);

    float clear_color[] =
    {
        r,
        g,
        b,
        1.0f
    };

    context->ClearRenderTargetView(
        render_target,
        clear_color);
}

static void WorkloadThread()
{
    HINSTANCE instance =
        GetModuleHandleW(nullptr);

    if (!CreateWorkloadWindow(instance))
    {
        g_running.store(false);
        return;
    }

    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGISwapChain* swap_chain = nullptr;
    ID3D11RenderTargetView* render_target = nullptr;

    if (!CreateDeviceAndSwapChain(
            g_window,
            &device,
            &context,
            &swap_chain,
            &render_target))
    {
        DestroyWorkloadWindow();
        g_running.store(false);
        return;
    }

    MSG message{};
    double phase = 0.0;

    while (!g_stop.load())
    {
        while (
            PeekMessageW(
                &message,
                nullptr,
                0,
                0,
                PM_REMOVE))
        {
            if (message.message == WM_QUIT)
            {
                g_stop.store(true);
                break;
            }

            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        if (g_stop.load())
            break;

        double intensity =
            GetEffectiveIntensity();

        RenderFrame(
            context,
            render_target,
            phase,
            intensity);

        UINT sync_interval =
            intensity >= 0.85
            ? 0
            : 1;

        swap_chain->Present(
            sync_interval,
            0);

        double phase_step =
            0.010 +
            intensity * 0.090;

        phase += phase_step;

        if (phase > 100000.0)
            phase = 0.0;
    }

    if (render_target)
        render_target->Release();

    if (swap_chain)
        swap_chain->Release();

    if (context)
        context->Release();

    if (device)
        device->Release();

    DestroyWorkloadWindow();

    g_running.store(false);
}

bool StartForegroundWorkload(
    ForegroundWorkloadLevel level)
{
    if (g_running.load())
    {
        SetForegroundWorkloadLevel(level);
        return true;
    }

    g_level.store(
        static_cast<int>(level));

    g_intensity.store(0.0);
    g_stop.store(false);
    g_running.store(true);

    g_thread =
        std::thread(
            WorkloadThread);

    return true;
}

void SetForegroundWorkloadLevel(
    ForegroundWorkloadLevel level)
{
    g_level.store(
        static_cast<int>(level));
}

void SetForegroundWorkloadIntensity(
    double intensity)
{
    intensity =
        std::clamp(
            intensity,
            0.0,
            1.0);

    g_intensity.store(intensity);
}

void SetForegroundWorkload(
    ForegroundWorkloadLevel level,
    double intensity)
{
    g_level.store(
        static_cast<int>(level));

    SetForegroundWorkloadIntensity(
        intensity);

    if (!g_running.load())
    {
        g_stop.store(false);
        g_running.store(true);

        g_thread =
            std::thread(
                WorkloadThread);
    }
}

void SetForegroundWorkload(
    ExperimentForegroundLevel level,
    double intensity)
{
    g_level.store(
        static_cast<int>(level));

    SetForegroundWorkloadIntensity(
        intensity);

    if (!g_running.load())
    {
        g_stop.store(false);
        g_running.store(true);

        g_thread =
            std::thread(
                WorkloadThread);
    }
}

void SetForegroundWorkloadLevel(
    ExperimentForegroundLevel level)
{
    g_level.store(
        static_cast<int>(level));
}

ForegroundWorkloadLevel
GetForegroundWorkloadLevel()
{
    return GetLevel();
}

double GetForegroundWorkloadIntensity()
{
    return GetIntensity();
}

void StopForegroundWorkload()
{
    g_stop.store(true);

    if (g_thread.joinable())
        g_thread.join();

    g_running.store(false);
}

bool IsForegroundWorkloadRunning()
{
    return g_running.load();
}