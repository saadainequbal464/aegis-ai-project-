#include "unified_telemetry.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cwctype>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "advapi32.lib")

static constexpr size_t MAX_FRAME_HISTORY = 120;
static constexpr uint64_t FRAME_STALE_US = 1000000ULL;
static constexpr int FOREGROUND_POLL_MS = 100;
static constexpr int PUMP_SLEEP_MS = 5;
static constexpr int STATUS_INTERVAL_MS = 1000;
static constexpr int RESTART_DELAY_MS = 1000;
static constexpr int NO_ROWS_WARN_MS = 8000;
static constexpr size_t MAX_PENDING_BYTES = 1u << 20;
static constexpr DWORD PIPE_BUFFER_BYTES = 1u << 16;

static std::atomic<bool> g_running{false};
static std::thread g_worker;

static std::mutex g_state_mutex;
static ForegroundQoSTelemetry g_state{};

static std::deque<double> g_frame_times;

static HANDLE g_process_handle = nullptr;
static HANDLE g_stdout_read = nullptr;
static HANDLE g_stderr_read = nullptr;

static std::wstring g_presentmon_exe;

static uint32_t g_own_pid = 0;
static bool g_debug = true;

static std::string g_target_app;
static uint32_t g_target_pid = 0;

struct CsvColumns
{
    bool ready = false;
    int application = -1;
    int frame_time = -1;
    std::string frame_time_name;
};

static CsvColumns g_columns;

struct RowStats
{
    uint64_t rows_total = 0;
    uint64_t rows_bad = 0;
    uint64_t ignored_lines = 0;
};

static RowStats g_stats;

static void DebugLog(const char* format, ...)
{
    if (!g_debug)
        return;

    char buffer[2048]{};

    va_list args;
    va_start(args, format);

    vsnprintf(
        buffer,
        sizeof(buffer),
        format,
        args
    );

    va_end(args);

    fprintf(
        stderr,
        "[QoS] %s\n",
        buffer
    );
}

static std::string Narrow(const std::wstring& wide)
{
    if (wide.empty())
        return {};

    int size =
        WideCharToMultiByte(
            CP_UTF8,
            0,
            wide.c_str(),
            static_cast<int>(wide.size()),
            nullptr,
            0,
            nullptr,
            nullptr
        );

    if (size <= 0)
        return {};

    std::string result(
        static_cast<size_t>(size),
        '\0'
    );

    WideCharToMultiByte(
        CP_UTF8,
        0,
        wide.c_str(),
        static_cast<int>(wide.size()),
        &result[0],
        size,
        nullptr,
        nullptr
    );

    return result;
}

static std::wstring GetEnv(const wchar_t* name)
{
    wchar_t buffer[4096]{};

    DWORD length =
        GetEnvironmentVariableW(
            name,
            buffer,
            4096
        );

    if (length == 0 || length >= 4096)
        return {};

    return std::wstring(
        buffer,
        length
    );
}

static std::string Win32ErrorText(DWORD code)
{
    char buffer[256]{};

    DWORD length =
        FormatMessageA(
            FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            code,
            0,
            buffer,
            sizeof(buffer),
            nullptr
        );

    while (
        length > 0 &&
        (
            buffer[length - 1] == '\r' ||
            buffer[length - 1] == '\n' ||
            buffer[length - 1] == ' '
        ))
    {
        buffer[--length] = '\0';
    }

    if (length == 0)
        return "unknown error";

    return std::string(
        buffer,
        length
    );
}

static std::string ToLowerAscii(std::string value)
{
    for (char& c : value)
    {
        c =
            static_cast<char>(
                std::tolower(
                    static_cast<unsigned char>(c)
                )
            );
    }

    return value;
}

static std::wstring ToLower(std::wstring value)
{
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](wchar_t c)
        {
            return static_cast<wchar_t>(
                towlower(c)
            );
        }
    );

    return value;
}

static int64_t GetQpcFrequency()
{
    static const int64_t frequency =
        []() -> int64_t
        {
            LARGE_INTEGER value{};

            if (!QueryPerformanceFrequency(&value))
                return 0;

            if (value.QuadPart <= 0)
                return 0;

            return static_cast<int64_t>(
                value.QuadPart
            );
        }();

    return frequency;
}

static uint64_t GetTimestampUs()
{
    const int64_t frequency =
        GetQpcFrequency();

    if (frequency <= 0)
        return 0;

    LARGE_INTEGER counter{};

    if (!QueryPerformanceCounter(&counter))
        return 0;

    const uint64_t ticks =
        static_cast<uint64_t>(
            counter.QuadPart
        );

    const uint64_t freq =
        static_cast<uint64_t>(
            frequency
        );

    return
        (ticks / freq) * 1000000ULL +
        ((ticks % freq) * 1000000ULL) / freq;
}

static std::wstring GetProcessName(uint32_t pid)
{
    HANDLE process =
        OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION,
            FALSE,
            pid
        );

    if (!process)
        return {};

    wchar_t buffer[MAX_PATH]{};
    DWORD size = MAX_PATH;

    std::wstring result;

    if (
        QueryFullProcessImageNameW(
            process,
            0,
            buffer,
            &size
        ))
    {
        result.assign(
            buffer,
            size
        );

        size_t slash =
            result.find_last_of(L"\\/");

        if (slash != std::wstring::npos)
            result = result.substr(slash + 1);
    }

    CloseHandle(process);

    return result;
}

static bool IsExcludedName(const std::wstring& name)
{
    static const wchar_t* const excluded[] =
    {
        L"cmd.exe",
        L"conhost.exe",
        L"openconsole.exe",
        L"powershell.exe",
        L"pwsh.exe",
        L"windowsterminal.exe",
        L"wt.exe",
        L"explorer.exe",
        L"dwm.exe",
        L"searchhost.exe",
        L"searchapp.exe",
        L"startmenuexperiencehost.exe",
        L"shellexperiencehost.exe",
        L"applicationframehost.exe"
    };

    for (const wchar_t* item : excluded)
    {
        if (name == item)
            return true;
    }

    return false;
}

struct ForegroundInfo
{
    bool valid = false;
    bool ignored = false;
    uint32_t pid = 0;
    std::wstring name;
    const char* reason = "";
};

static ForegroundInfo QueryForeground()
{
    ForegroundInfo info;

    HWND window =
        GetForegroundWindow();

    if (!window)
    {
        info.reason = "no foreground window";
        return info;
    }

    DWORD pid = 0;

    GetWindowThreadProcessId(
        window,
        &pid
    );

    if (pid == 0)
    {
        info.reason = "no process id";
        return info;
    }

    info.pid =
        static_cast<uint32_t>(pid);

    info.name =
        ToLower(
            GetProcessName(info.pid)
        );

    if (info.pid == g_own_pid)
    {
        info.ignored = true;
        info.reason = "aegis itself";
        return info;
    }

    if (info.name.empty())
    {
        info.reason =
            "process name unavailable";
        return info;
    }

    if (IsExcludedName(info.name))
    {
        info.ignored = true;
        info.reason = "excluded process";
        return info;
    }

    info.valid = true;

    return info;
}

static bool IsProcessElevated()
{
    HANDLE token = nullptr;

    if (!OpenProcessToken(
            GetCurrentProcess(),
            TOKEN_QUERY,
            &token))
    {
        return false;
    }

    TOKEN_ELEVATION elevation{};

    DWORD size =
        sizeof(elevation);

    BOOL ok =
        GetTokenInformation(
            token,
            TokenElevation,
            &elevation,
            sizeof(elevation),
            &size
        );

    CloseHandle(token);

    return
        ok &&
        elevation.TokenIsElevated != 0;
}

static bool FileExists(const std::wstring& path)
{
    DWORD attributes =
        GetFileAttributesW(
            path.c_str()
        );

    return
        attributes != INVALID_FILE_ATTRIBUTES &&
        !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static std::wstring FindPresentMonIn(
    const std::wstring& directory)
{
    std::wstring exact =
        directory +
        L"PresentMon.exe";

    if (FileExists(exact))
        return exact;

    WIN32_FIND_DATAW data{};

    HANDLE find =
        FindFirstFileW(
            (
                directory +
                L"PresentMon-*.exe"
            ).c_str(),
            &data
        );

    if (find == INVALID_HANDLE_VALUE)
        return {};

    std::wstring best;

    do
    {
        if (
            data.dwFileAttributes &
            FILE_ATTRIBUTE_DIRECTORY)
        {
            continue;
        }

        std::wstring candidate =
            data.cFileName;

        if (candidate > best)
            best = candidate;

    } while (
        FindNextFileW(
            find,
            &data
        )
    );

    FindClose(find);

    if (best.empty())
        return {};

    return directory + best;
}

static std::wstring GetExeDirectory()
{
    wchar_t path[MAX_PATH]{};

    DWORD length =
        GetModuleFileNameW(
            nullptr,
            path,
            MAX_PATH
        );

    if (length == 0 || length >= MAX_PATH)
        return {};

    std::wstring result(
        path,
        length
    );

    size_t slash =
        result.find_last_of(L"\\/");

    if (slash == std::wstring::npos)
        return {};

    result.resize(slash + 1);

    return result;
}

static std::wstring GetCurrentDirectoryWithSlash()
{
    wchar_t path[MAX_PATH]{};

    DWORD length =
        GetCurrentDirectoryW(
            MAX_PATH,
            path
        );

    if (length == 0 || length >= MAX_PATH)
        return {};

    std::wstring result(
        path,
        length
    );

    if (
        result.back() != L'\\' &&
        result.back() != L'/')
    {
        result += L'\\';
    }

    return result;
}

static std::wstring FindPresentMon()
{
    // 1. Explicit environment override
    std::wstring from_env =
        GetEnv(L"AEGIS_PRESENTMON_PATH");

    if (!from_env.empty() && FileExists(from_env))
        return from_env;

    // 2. Directory containing aegis_ai_arm64.exe
    std::wstring exe_dir =
        GetExeDirectory();

    if (!exe_dir.empty())
    {
        std::wstring found =
            FindPresentMonIn(exe_dir);

        if (!found.empty())
            return found;

        // 3. Parent directory of arm64_build
        std::wstring parent_dir = exe_dir;

        size_t slash =
            parent_dir.find_last_of(L"\\/");

        if (slash != std::wstring::npos)
        {
            parent_dir.resize(slash);

            slash =
                parent_dir.find_last_of(L"\\/");

            if (slash != std::wstring::npos)
            {
                parent_dir.resize(slash + 1);

                std::wstring parent_found =
                    FindPresentMonIn(parent_dir);

                if (!parent_found.empty())
                    return parent_found;
            }
        }
    }

    // 4. Current working directory
    std::wstring cwd =
        GetCurrentDirectoryWithSlash();

    if (!cwd.empty())
    {
        std::wstring found =
            FindPresentMonIn(cwd);

        if (!found.empty())
            return found;
    }

    // 5. Fallback
    return L"PresentMon.exe";
}
static bool CreatePipePair(
    HANDLE& read_end,
    HANDLE& write_end)
{
    SECURITY_ATTRIBUTES security{};

    security.nLength =
        sizeof(security);

    security.bInheritHandle = TRUE;

    if (!CreatePipe(
            &read_end,
            &write_end,
            &security,
            PIPE_BUFFER_BYTES))
    {
        return false;
    }

    if (!SetHandleInformation(
            read_end,
            HANDLE_FLAG_INHERIT,
            0))
    {
        CloseHandle(read_end);
        CloseHandle(write_end);

        read_end = nullptr;
        write_end = nullptr;

        return false;
    }

    return true;
}

static bool StartPresentMon(
    const std::wstring& exe,
    const std::string& target,
    DWORD& error_code)
{
    error_code = 0;

    HANDLE out_read = nullptr;
    HANDLE out_write = nullptr;

    HANDLE err_read = nullptr;
    HANDLE err_write = nullptr;

    if (!CreatePipePair(
            out_read,
            out_write))
    {
        error_code =
            GetLastError();

        return false;
    }

    if (!CreatePipePair(
            err_read,
            err_write))
    {
        error_code =
            GetLastError();

        CloseHandle(out_read);
        CloseHandle(out_write);

        return false;
    }

    SECURITY_ATTRIBUTES inherit{};

    inherit.nLength =
        sizeof(inherit);

    inherit.bInheritHandle = TRUE;

    HANDLE nul_in =
        CreateFileW(
            L"NUL",
            GENERIC_READ,
            FILE_SHARE_READ |
            FILE_SHARE_WRITE,
            &inherit,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );

    if (nul_in == INVALID_HANDLE_VALUE)
    {
        error_code =
            GetLastError();

        CloseHandle(out_read);
        CloseHandle(out_write);
        CloseHandle(err_read);
        CloseHandle(err_write);

        return false;
    }

    // Capture all presenting applications. The worker filters rows by the
    // PresentMon Application column against g_target_app. This keeps one
    // long-lived ETW/PresentMon session across foreground changes.
    std::wstring command =
        L"\"" +
        exe +
        L"\""
        L" --stop_existing_session"
        L" --session_name AEGIS_QOS"
        L" --output_stdout"
        L" --no_console_stats"
        L" --v2_metrics"
        L" --qpc_time_ms";

    std::vector<wchar_t>
        command_buffer(
            command.begin(),
            command.end()
        );

    command_buffer.push_back(L'\0');

    STARTUPINFOW startup{};

    startup.cb =
        sizeof(startup);

    startup.dwFlags =
        STARTF_USESTDHANDLES;

    startup.hStdInput =
        nul_in;

    startup.hStdOutput =
        out_write;

    startup.hStdError =
        err_write;

    PROCESS_INFORMATION process_info{};

    BOOL created =
        CreateProcessW(
            nullptr,
            command_buffer.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startup,
            &process_info
        );

    error_code =
        created
        ? 0
        : GetLastError();

    CloseHandle(out_write);
    CloseHandle(err_write);
    CloseHandle(nul_in);

    if (!created)
    {
        CloseHandle(out_read);
        CloseHandle(err_read);

        return false;
    }

    CloseHandle(
        process_info.hThread
    );

    g_process_handle =
        process_info.hProcess;

    g_stdout_read =
        out_read;

    g_stderr_read =
        err_read;

    return true;
}

static void StopPresentMon()
{
    if (g_process_handle)
    {
        DWORD exit_code = 0;

        if (
            GetExitCodeProcess(
                g_process_handle,
                &exit_code
            ) &&
            exit_code == STILL_ACTIVE)
        {
            TerminateProcess(
                g_process_handle,
                0
            );

            WaitForSingleObject(
                g_process_handle,
                1000
            );
        }

        CloseHandle(
            g_process_handle
        );

        g_process_handle =
            nullptr;
    }

    if (g_stdout_read)
    {
        CloseHandle(
            g_stdout_read
        );

        g_stdout_read =
            nullptr;
    }

    if (g_stderr_read)
    {
        CloseHandle(
            g_stderr_read
        );

        g_stderr_read =
            nullptr;
    }
}

static void ReadAvailable(
    HANDLE pipe,
    std::string& pending)
{
    if (!pipe)
        return;

    for (int pass = 0;
         pass < 16;
         ++pass)
    {
        DWORD available = 0;

        if (
            !PeekNamedPipe(
                pipe,
                nullptr,
                0,
                nullptr,
                &available,
                nullptr) ||
            available == 0)
        {
            return;
        }

        char buffer[8192]{};

        DWORD to_read =
            available <
            sizeof(buffer)
            ? available
            : static_cast<DWORD>(
                sizeof(buffer)
            );

        DWORD bytes_read = 0;

        if (
            !ReadFile(
                pipe,
                buffer,
                to_read,
                &bytes_read,
                nullptr) ||
            bytes_read == 0)
        {
            return;
        }

        pending.append(
            buffer,
            bytes_read
        );
    }
}

template <typename Handler>
static void ConsumeLines(
    std::string& pending,
    Handler handler)
{
    size_t start = 0;
    size_t newline = 0;

    while (
        (newline =
            pending.find(
                '\n',
                start
            )) != std::string::npos)
    {
        size_t end = newline;

        if (
            end > start &&
            pending[end - 1] == '\r')
        {
            --end;
        }

        handler(
            pending.substr(
                start,
                end - start
            )
        );

        start =
            newline + 1;
    }

    if (start > 0)
        pending.erase(0, start);

    if (pending.size() > MAX_PENDING_BYTES)
        pending.clear();
}

static std::vector<std::string>
SplitCsv(const std::string& line)
{
    std::vector<std::string> fields;

    fields.reserve(64);

    std::string current;
    bool quoted = false;

    for (char c : line)
    {
        if (c == '"')
        {
            quoted = !quoted;
            continue;
        }

        if (c == ',' && !quoted)
        {
            fields.push_back(current);
            current.clear();
        }
        else
        {
            current += c;
        }
    }

    fields.push_back(current);

    return fields;
}

static std::string NormalizeHeader(
    std::string value)
{
    if (
        value.size() >= 3 &&
        static_cast<unsigned char>(value[0]) == 0xEF &&
        static_cast<unsigned char>(value[1]) == 0xBB &&
        static_cast<unsigned char>(value[2]) == 0xBF)
    {
        value.erase(0, 3);
    }

    while (
        !value.empty() &&
        (
            value.front() == ' ' ||
            value.front() == '\t'
        ))
    {
        value.erase(value.begin());
    }

    while (
        !value.empty() &&
        (
            value.back() == ' ' ||
            value.back() == '\t' ||
            value.back() == '\r'
        ))
    {
        value.pop_back();
    }

    return value;
}

static bool TryParseHeader(
    const std::vector<std::string>& fields)
{
    int application = -1;
    int frame_time = -1;
    int between_presents = -1;

    for (size_t i = 0;
         i < fields.size();
         ++i)
    {
        std::string name =
            ToLowerAscii(
                NormalizeHeader(
                    fields[i]
                )
            );

        if (name == "application")
        {
            application = static_cast<int>(i);
        }
        else if (name == "msbetweenpresents")
        {
            between_presents = static_cast<int>(i);
        }
        else if (name == "frametime")
        {
            frame_time = static_cast<int>(i);
        }
    }

    if (application < 0)
        return false;

    if (between_presents >= 0)
    {
        g_columns.frame_time = between_presents;
        g_columns.frame_time_name = "MsBetweenPresents";
    }
    else if (frame_time >= 0)
    {
        g_columns.frame_time = frame_time;
        g_columns.frame_time_name = "FrameTime";
    }
    else
    {
        return false;
    }

    g_columns.application = application;
    g_columns.ready = true;

    DebugLog(
        "CSV header found: Application=%d %s=%d",
        g_columns.application,
        g_columns.frame_time_name.c_str(),
        g_columns.frame_time
    );

    return true;
}

static double GetPercentile(
    const std::vector<double>& sorted,
    double percentile)
{
    if (sorted.empty())
        return 0.0;

    double position =
        percentile *
        static_cast<double>(
            sorted.size() - 1
        );

    size_t lower =
        static_cast<size_t>(
            std::floor(position)
        );

    size_t upper =
        static_cast<size_t>(
            std::ceil(position)
        );

    if (lower == upper)
        return sorted[lower];

    double fraction =
        position -
        static_cast<double>(lower);

    return
        sorted[lower] +
        (
            sorted[upper] -
            sorted[lower]
        ) *
        fraction;
}

static void ProcessFrameTime(
    double frame_time_ms,
    uint32_t pid)
{
    if (!std::isfinite(frame_time_ms))
        return;

    if (
        frame_time_ms <= 0.0 ||
        frame_time_ms > 1000.0)
    {
        return;
    }

    g_frame_times.push_back(
        frame_time_ms
    );

    if (
        g_frame_times.size() >
        MAX_FRAME_HISTORY)
    {
        g_frame_times.pop_front();
    }

    double total = 0.0;

    for (double value : g_frame_times)
        total += value;

    double average =
        total /
        static_cast<double>(
            g_frame_times.size()
        );

    std::vector<double> sorted(
        g_frame_times.begin(),
        g_frame_times.end()
    );

    std::sort(
        sorted.begin(),
        sorted.end()
    );

    ForegroundQoSTelemetry state{};

    state.frame_timestamp_us =
        GetTimestampUs();

    state.frame_time_ms =
        frame_time_ms;

    state.fps =
        average > 0.0
        ? 1000.0 / average
        : 0.0;

    state.frame_time_p95 =
        GetPercentile(
            sorted,
            0.95
        );

    state.frame_time_p99 =
        GetPercentile(
            sorted,
            0.99
        );

    state.foreground_pid =
        pid;

    state.frame_count =
        g_frame_times.size();

    state.available = true;
    state.frame_data_available = true;

    std::lock_guard<std::mutex>
        lock(g_state_mutex);

    g_state = state;
}

static void ProcessCsvLine(
    const std::string& line)
{
    if (line.empty())
        return;

    std::vector<std::string> fields =
        SplitCsv(line);

    if (!g_columns.ready)
    {
        if (TryParseHeader(fields))
            return;

        if (g_stats.ignored_lines < 10)
        {
            ++g_stats.ignored_lines;
            DebugLog(
                "waiting for CSV header: %zu fields: %.300s",
                fields.size(),
                line.c_str()
            );
        }

        return;
    }

    ++g_stats.rows_total;

    if (g_columns.application < 0 ||
        g_columns.frame_time < 0 ||
        g_target_app.empty())
    {
        return;
    }

    const size_t application_column =
        static_cast<size_t>(g_columns.application);

    const size_t frame_column =
        static_cast<size_t>(g_columns.frame_time);

    if (application_column >= fields.size() ||
        frame_column >= fields.size())
    {
        ++g_stats.rows_bad;
        return;
    }

    const std::string application =
        ToLowerAscii(
            NormalizeHeader(fields[application_column])
        );

    if (application != g_target_app)
        return;

    const std::string& value =
        fields[frame_column];

    if (value.empty() ||
        value == "NA" ||
        value == "N/A")
    {
        ++g_stats.rows_bad;
        return;
    }

    char* end_pointer = nullptr;

    double frame_time =
        std::strtod(
            value.c_str(),
            &end_pointer
        );

    if (end_pointer == value.c_str())
    {
        ++g_stats.rows_bad;
        return;
    }

    ProcessFrameTime(
        frame_time,
        g_target_pid
    );
}

static void LogPresentMonStderrLine(
    const std::string& line)
{
    if (!line.empty())
    {
        DebugLog(
            "PresentMon: %.500s",
            line.c_str()
        );
    }
}

static void ResetQoSState(
    uint32_t pid)
{
    g_frame_times.clear();

    std::lock_guard<std::mutex>
        lock(g_state_mutex);

    g_state = {};

    g_state.foreground_pid =
        pid;
}

static void ForegroundQoSWorker()
{
    using clock =
        std::chrono::steady_clock;

    using milliseconds =
        std::chrono::milliseconds;

    std::string pinned_target =
        ToLowerAscii(
            Narrow(
                GetEnv(
                    L"AEGIS_QOS_TARGET"
                )
            )
        );

    // Empty AEGIS_QOS_TARGET means: dynamically track the real foreground
    // application. Do NOT default to aegis_ai.exe; that would make AEGIS
    // measure itself instead of the foreground workload.

    if (!pinned_target.empty())
    {
        const size_t slash = pinned_target.find_last_of("\\\\/");
        if (slash != std::string::npos)
            pinned_target = pinned_target.substr(slash + 1);

        if (pinned_target.find('.') == std::string::npos)
            pinned_target += ".exe";
    }

    DebugLog(
        "worker started elevated=%s target=%s",
        IsProcessElevated()
            ? "yes"
            : "NO",
        pinned_target.empty()
            ? "foreground"
            : pinned_target.c_str()
    );

    std::string stdout_pending;
    std::string stderr_pending;

    auto next_foreground_poll =
        clock::now();

    auto next_start_allowed =
        clock::now();

    auto next_status =
        clock::now() +
        milliseconds(
            STATUS_INTERVAL_MS
        );

    auto presentmon_started =
        clock::now();

    bool warned_no_rows = false;

    while (g_running.load())
    {
        auto now =
            clock::now();

        if (
            now >=
            next_foreground_poll)
        {
            next_foreground_poll =
                now +
                milliseconds(
                    FOREGROUND_POLL_MS
                );

            ForegroundInfo foreground =
                QueryForeground();

            if (!pinned_target.empty())
            {
                if (
                    g_target_app !=
                    pinned_target)
                {
                    StopPresentMon();

                    g_target_app =
                        pinned_target;

                    g_target_pid =
                        g_own_pid;

                    ResetQoSState(0);

                    next_start_allowed =
                        now;

                    DebugLog(
                        "tracking pinned application: %s",
                        g_target_app.c_str()
                    );
                }
            }
            else if (foreground.valid)
            {
                std::string new_target =
                    ToLowerAscii(
                        Narrow(
                            foreground.name
                        )
                    );

                if (
                    new_target !=
                    g_target_app)
                {
                    // PresentMon is intentionally long-lived and captures all
                    // presenting applications. Only the software-side target
                    // changes here, avoiding a restart/blind gap on focus changes.
                    g_target_app =
                        new_target;

                    g_target_pid =
                        foreground.pid;

                    ResetQoSState(
                        foreground.pid
                    );

                    next_start_allowed =
                        now;

                    warned_no_rows = false;

                    DebugLog(
                        "tracking application: %s pid=%u",
                        g_target_app.c_str(),
                        g_target_pid
                    );
                }
                else
                {
                    g_target_pid =
                        foreground.pid;
                }
            }
            else if (foreground.ignored)
            {
                if (!g_target_app.empty())
                {
                    DebugLog(
                        "keeping previous target %s because foreground is %s",
                        g_target_app.c_str(),
                        foreground.reason
                    );
                }
            }
        }

        if (
            g_process_handle == nullptr &&
            now >= next_start_allowed)
        {
            g_presentmon_exe =
                FindPresentMon();

            g_columns =
                CsvColumns{};

            g_stats =
                RowStats{};

            stdout_pending.clear();
            stderr_pending.clear();

            DWORD error = 0;

            if (
                StartPresentMon(
                    g_presentmon_exe,
                    g_target_app,
                    error
                ))
            {
                presentmon_started =
                    now;

                warned_no_rows =
                    false;

                DebugLog(
                    "PresentMon started target=%s path=%s",
                    g_target_app.c_str(),
                    Narrow(
                        g_presentmon_exe
                    ).c_str()
                );
            }
            else
            {
                next_start_allowed =
                    now +
                    milliseconds(
                        RESTART_DELAY_MS
                    );

                DebugLog(
                    "PresentMon start failed: %lu (%s)",
                    static_cast<unsigned long>(
                        error
                    ),
                    Win32ErrorText(
                        error
                    ).c_str()
                );
            }
        }

        if (g_process_handle != nullptr)
        {
            ReadAvailable(
                g_stdout_read,
                stdout_pending
            );

            ReadAvailable(
                g_stderr_read,
                stderr_pending
            );

            ConsumeLines(
                stdout_pending,
                ProcessCsvLine
            );

            ConsumeLines(
                stderr_pending,
                LogPresentMonStderrLine
            );
        }

        if (g_process_handle != nullptr)
        {
            DWORD exit_code =
                STILL_ACTIVE;

            if (
                GetExitCodeProcess(
                    g_process_handle,
                    &exit_code
                ) &&
                exit_code != STILL_ACTIVE)
            {
                ReadAvailable(
                    g_stdout_read,
                    stdout_pending
                );

                ReadAvailable(
                    g_stderr_read,
                    stderr_pending
                );

                ConsumeLines(
                    stdout_pending,
                    ProcessCsvLine
                );

                ConsumeLines(
                    stderr_pending,
                    LogPresentMonStderrLine
                );

                long long uptime_ms =
                    std::chrono::duration_cast<
                        milliseconds
                    >(
                        now -
                        presentmon_started
                    ).count();

                DebugLog(
                    "PresentMon exited code=%lu uptime=%lldms rows=%llu",
                    static_cast<unsigned long>(
                        exit_code
                    ),
                    uptime_ms,
                    static_cast<unsigned long long>(
                        g_stats.rows_total
                    )
                );

                StopPresentMon();

                next_start_allowed =
                    now +
                    milliseconds(
                        RESTART_DELAY_MS
                    );
            }
        }

        if (
            g_process_handle != nullptr &&
            !warned_no_rows &&
            g_stats.rows_total == 0 &&
            now -
                presentmon_started >
            milliseconds(
                NO_ROWS_WARN_MS
            ))
        {
            warned_no_rows = true;

            DebugLog(
                "PresentMon running but no CSV rows after %d seconds",
                NO_ROWS_WARN_MS / 1000
            );
        }

        if (now >= next_status)
        {
            next_status =
                now +
                milliseconds(
                    STATUS_INTERVAL_MS
                );

            ForegroundQoSTelemetry snapshot{};

            {
                std::lock_guard<std::mutex>
                    lock(g_state_mutex);

                snapshot =
                    g_state;
            }

            uint64_t now_us =
                GetTimestampUs();

            uint64_t age_ms = 0;

            if (
                snapshot.frame_timestamp_us > 0 &&
                now_us >
                    snapshot.frame_timestamp_us)
            {
                age_ms =
                    (
                        now_us -
                        snapshot.frame_timestamp_us
                    ) /
                    1000ULL;
            }

            DebugLog(
                "status PM=%s header=%s rows=%llu bad=%llu target=%s frame=%s fps=%.1f p95=%.1f age=%llums",
                g_process_handle
                    ? "running"
                    : "NO",
                g_columns.ready
                    ? g_columns.frame_time_name.c_str()
                    : "waiting",
                static_cast<unsigned long long>(
                    g_stats.rows_total
                ),
                static_cast<unsigned long long>(
                    g_stats.rows_bad
                ),
                g_target_app.empty()
                    ? "none"
                    : g_target_app.c_str(),
                snapshot.frame_data_available
                    ? "YES"
                    : "NO",
                snapshot.fps,
                snapshot.frame_time_p95,
                static_cast<unsigned long long>(
                    age_ms
                )
            );
        }

        std::this_thread::sleep_for(
            milliseconds(
                PUMP_SLEEP_MS
            )
        );
    }

    StopPresentMon();

    DebugLog(
        "worker stopped"
    );
}

bool StartForegroundQoS()
{
    if (g_running.exchange(true))
        return true;

    g_own_pid =
        GetCurrentProcessId();

    g_debug =
        GetEnv(
            L"AEGIS_QOS_DEBUG"
        ) != L"0";

    {
        std::lock_guard<std::mutex>
            lock(g_state_mutex);

        g_state = {};
    }

    g_frame_times.clear();

    g_target_app.clear();
    g_target_pid = 0;

    g_columns =
        CsvColumns{};

    g_stats =
        RowStats{};

    g_worker =
        std::thread(
            ForegroundQoSWorker
        );

    return true;
}

void StopForegroundQoS()
{
    if (!g_running.exchange(false))
        return;

    if (g_worker.joinable())
        g_worker.join();

    ResetQoSState(0);
}

ForegroundQoSTelemetry
CollectForegroundQoSTelemetry()
{
    ForegroundQoSTelemetry snapshot{};

    {
        std::lock_guard<std::mutex>
            lock(g_state_mutex);

        snapshot =
            g_state;
    }

    // Keep the last valid PresentMon measurement. The timestamp in the
    // snapshot remains the measurement time, so downstream code can see
    // how old it is instead of silently converting valid QoS into zeros.
    return snapshot;
}
