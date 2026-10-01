#include "unified_telemetry.h"

#include <windows.h>
#include <winhttp.h>
#include <tlhelp32.h>
#include <psapi.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>
#include <fstream>
#include <cstring>
#include <cstring>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "psapi.lib")

static LlmTelemetry g_llm{};
static std::mutex g_llm_mutex;

static std::thread g_inference_thread;
static std::atomic<bool> g_stop_inference{false};
static std::atomic<bool> g_workload_running{false};

static std::mutex g_workload_mutex;

static ExperimentLlmLevel g_workload_level =
    ExperimentLlmLevel::LOW;

static double g_workload_intensity = 0.0;

static uint64_t GetTimestampUsInternal()
{
    static LARGE_INTEGER frequency{};
    static std::once_flag flag;

    std::call_once(
        flag,
        []()
        {
            QueryPerformanceFrequency(
                &frequency
            );
        }
    );

    if (frequency.QuadPart <= 0)
        return 0;

    LARGE_INTEGER counter{};

    if (!QueryPerformanceCounter(
            &counter))
    {
        return 0;
    }

    return static_cast<uint64_t>(
        (
            static_cast<long double>(
                counter.QuadPart
            ) *
            1000000.0L
        ) /
        static_cast<long double>(
            frequency.QuadPart
        )
    );
}

uint64_t GetTelemetryTimestampUs()
{
    return GetTimestampUsInternal();
}

static double ClampIntensity(
    double value)
{
    if (value < 0.0)
        return 0.0;

    if (value > 1.0)
        return 1.0;

    return value;
}

static double GetLevelBaseIntensity(
    ExperimentLlmLevel level)
{
    switch (level)
    {
    case ExperimentLlmLevel::LOW:
        return 0.15;

    case ExperimentLlmLevel::MEDIUM:
        return 0.40;

    case ExperimentLlmLevel::HIGH:
        return 0.70;

    case ExperimentLlmLevel::PEAK:
        return 1.0;
    }

    return 0.0;
}

static double GetEffectiveIntensity()
{
    std::lock_guard<std::mutex> lock(
        g_workload_mutex
    );

    double base =
        GetLevelBaseIntensity(
            g_workload_level
        );

    double requested =
        ClampIntensity(
            g_workload_intensity
        );

    double effective =
        base +
        requested *
        (1.0 - base);

    return ClampIntensity(
        effective
    );
}

static int Interpolate(
    double intensity,
    int low,
    int high)
{
    intensity =
        ClampIntensity(
            intensity
        );

    return static_cast<int>(
        static_cast<double>(low) +
        (
            static_cast<double>(
                high - low
            ) *
            intensity
        )
    );
}

static int GetPromptRepetitions(
    double intensity)
{
    return Interpolate(
        intensity,
        1,
        18
    );
}

static int GetMaxTokens(
    double intensity)
{
    return Interpolate(
        intensity,
        64,
        512
    );
}

static int GetCooldownMs(
    double intensity)
{
    int cooldown =
        Interpolate(
            1.0 - intensity,
            40,
            1800
        );

    if (cooldown < 20)
        cooldown = 20;

    return cooldown;
}

static int GetContextLength(
    double intensity)
{
    if (intensity >= 0.80)
        return 4096;

    if (intensity >= 0.45)
        return 3072;

    return 2048;
}

static std::string BuildPrompt(
    double intensity)
{
    const std::string base =
        "Analyze Windows system resource contention, "
        "CPU scheduling, memory bandwidth, cache behavior, "
        "DRAM traffic, GPU workload, NPU workload, "
        "foreground frame time, latency, throughput, "
        "and background LLM execution. "
        "Explain the interactions clearly. ";

    int repetitions =
        GetPromptRepetitions(
            intensity
        );

    std::string prompt;

    prompt.reserve(
        base.size() *
        static_cast<size_t>(
            repetitions
        )
    );

    for (int i = 0; i < repetitions; ++i)
        prompt += base;

    return prompt;
}

static std::string EscapeJson(
    const std::string& input)
{
    std::string result;

    result.reserve(
        input.size() + 64
    );

    for (char c : input)
    {
        switch (c)
        {
        case '\\':
            result += "\\\\";
            break;

        case '"':
            result += "\\\"";
            break;

        case '\n':
            result += "\\n";
            break;

        case '\r':
            result += "\\r";
            break;

        case '\t':
            result += "\\t";
            break;

        default:
            result.push_back(c);
            break;
        }
    }

    return result;
}

static std::string BuildRequestBody(
    double intensity)
{
    std::string prompt =
        EscapeJson(
            BuildPrompt(
                intensity
            )
        );

    int max_tokens =
        GetMaxTokens(
            intensity
        );

    return
        "{"
        "\"model\":\"gemma-3-4b-instruct-gguf\","
        "\"messages\":[{"
        "\"role\":\"user\","
        "\"content\":\"" +
        prompt +
        "\"}],"
        "\"max_tokens\":" +
        std::to_string(
            max_tokens
        ) +
        ","
        "\"stream\":true,"
        "\"stream_options\":"
        "{\"include_usage\":true}"
        "}";
}

static bool ExtractIntField(
    const std::string& data,
    const std::string& field,
    int& value)
{
    size_t position =
        data.find(
            "\"" + field + "\""
        );

    if (position == std::string::npos)
        return false;

    size_t colon =
        data.find(
            ':',
            position
        );

    if (colon == std::string::npos)
        return false;

    size_t begin =
        colon + 1;

    while (
        begin < data.size() &&
        (
            data[begin] == ' ' ||
            data[begin] == '\t'
        ))
    {
        ++begin;
    }

    size_t end = begin;

    if (
        end < data.size() &&
        (
            data[end] == '-' ||
            data[end] == '+'
        ))
    {
        ++end;
    }

    while (
        end < data.size() &&
        data[end] >= '0' &&
        data[end] <= '9')
    {
        ++end;
    }

    if (end == begin)
        return false;

    try
    {
        value =
            std::stoi(
                data.substr(
                    begin,
                    end - begin
                )
            );

        return true;
    }
    catch (...)
    {
        return false;
    }
}

static bool ExtractDoubleField(
    const std::string& data,
    const std::string& field,
    double& value)
{
    size_t position =
        data.find(
            "\"" + field + "\""
        );

    if (position == std::string::npos)
        return false;

    size_t colon =
        data.find(
            ':',
            position
        );

    if (colon == std::string::npos)
        return false;

    size_t begin =
        colon + 1;

    while (
        begin < data.size() &&
        (
            data[begin] == ' ' ||
            data[begin] == '\t'
        ))
    {
        ++begin;
    }

    size_t end = begin;

    while (
        end < data.size() &&
        (
            (data[end] >= '0' &&
             data[end] <= '9') ||
            data[end] == '-' ||
            data[end] == '+' ||
            data[end] == '.' ||
            data[end] == 'e' ||
            data[end] == 'E'
        ))
    {
        ++end;
    }

    if (end == begin)
        return false;

    try
    {
        value =
            std::stod(
                data.substr(
                    begin,
                    end - begin
                )
            );

        return true;
    }
    catch (...)
    {
        return false;
    }
}

static bool ExtractStringField(
    const std::string& data,
    const std::string& field,
    std::string& value)
{
    size_t position =
        data.find(
            "\"" + field + "\""
        );

    if (position == std::string::npos)
        return false;

    size_t colon =
        data.find(
            ':',
            position
        );

    if (colon == std::string::npos)
        return false;

    size_t begin =
        colon + 1;

    while (
        begin < data.size() &&
        (
            data[begin] == ' ' ||
            data[begin] == '\t'
        ))
    {
        ++begin;
    }

    if (
        begin >= data.size() ||
        data[begin] != '"')
    {
        return false;
    }

    ++begin;

    std::string result;
    bool escaped = false;

    for (
        size_t i = begin;
        i < data.size();
        ++i)
    {
        char c = data[i];

        if (escaped)
        {
            if (c == 'n')
                result.push_back('\n');
            else if (c == 'r')
                result.push_back('\r');
            else if (c == 't')
                result.push_back('\t');
            else
                result.push_back(c);

            escaped = false;
            continue;
        }

        if (c == '\\')
        {
            escaped = true;
            continue;
        }

        if (c == '"')
        {
            value = result;
            return true;
        }

        result.push_back(c);
    }

    return false;
}

static void ResetInferenceMetrics(
    int prompt_tokens,
    int context_length)
{
    std::lock_guard<std::mutex> lock(
        g_llm_mutex
    );

    g_llm.active = true;

    g_llm.tokens_per_second = 0.0;
    g_llm.token_latency_ms = 0.0;
    g_llm.time_to_first_token_ms = 0.0;

    g_llm.prompt_tokens =
        static_cast<double>(
            prompt_tokens
        );

    g_llm.completion_tokens = 0.0;

    g_llm.total_tokens =
        static_cast<double>(
            prompt_tokens
        );

    g_llm.context_length =
        static_cast<double>(
            context_length
        );

    g_llm.predicted_tokens = 0.0;
    g_llm.predicted_per_token_ms = 0.0;
    g_llm.predicted_per_second = 0.0;

    g_llm.token_count = 0;

    g_llm.inference_count++;

    g_llm.available = true;
}

static void ApplyFinalMetrics(
    const std::string& response)
{
    int generated = 0;
    int prompt = 0;
    int total = 0;

    double latency = 0.0;
    double rate = 0.0;

    bool generated_ok =
        ExtractIntField(
            response,
            "completion_tokens",
            generated
        );

    if (!generated_ok)
    {
        generated_ok =
            ExtractIntField(
                response,
                "predicted_n",
                generated
            );
    }

    bool prompt_ok =
        ExtractIntField(
            response,
            "prompt_tokens",
            prompt
        );

    if (!prompt_ok)
    {
        prompt_ok =
            ExtractIntField(
                response,
                "prompt_n",
                prompt
            );
    }

    bool total_ok =
        ExtractIntField(
            response,
            "total_tokens",
            total
        );

    bool latency_ok =
        ExtractDoubleField(
            response,
            "predicted_per_token_ms",
            latency
        );

    bool rate_ok =
        ExtractDoubleField(
            response,
            "predicted_per_second",
            rate
        );

    std::lock_guard<std::mutex> lock(
        g_llm_mutex
    );

    if (generated_ok)
    {
        g_llm.completion_tokens =
            static_cast<double>(
                generated
            );

        g_llm.token_count =
            static_cast<uint64_t>(
                generated
            );
    }

    if (prompt_ok)
    {
        g_llm.prompt_tokens =
            static_cast<double>(
                prompt
            );
    }

    if (total_ok)
    {
        g_llm.total_tokens =
            static_cast<double>(
                total
            );
    }
    else
    {
        g_llm.total_tokens =
            g_llm.prompt_tokens +
            g_llm.completion_tokens;
    }

    if (latency_ok)
    {
        g_llm.predicted_per_token_ms =
            latency;
    }

    if (rate_ok)
    {
        g_llm.predicted_per_second =
            rate;
    }
}

static bool HttpStreamPost(
    const std::wstring& path,
    const std::string& body)
{
    HINTERNET session =
        WinHttpOpen(
            L"AEGIS-AI",
            WINHTTP_ACCESS_TYPE_NO_PROXY,
            nullptr,
            nullptr,
            0
        );

    if (!session)
        return false;

    HINTERNET connection =
        WinHttpConnect(
            session,
            L"127.0.0.1",
            8080,
            0
        );

    if (!connection)
    {
        WinHttpCloseHandle(
            session
        );

        return false;
    }

    HINTERNET request =
        WinHttpOpenRequest(
            connection,
            L"POST",
            path.c_str(),
            nullptr,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            0
        );

    if (!request)
    {
        WinHttpCloseHandle(
            connection
        );

        WinHttpCloseHandle(
            session
        );

        return false;
    }

    const wchar_t* headers =
        L"Content-Type: application/json\r\n"
        L"Accept: text/event-stream\r\n";

    BOOL sent =
        WinHttpSendRequest(
            request,
            headers,
            static_cast<DWORD>(-1L),
            const_cast<char*>(
                body.data()
            ),
            static_cast<DWORD>(
                body.size()
            ),
            static_cast<DWORD>(
                body.size()
            ),
            0
        );

    if (!sent)
    {
        WinHttpCloseHandle(
            request
        );

        WinHttpCloseHandle(
            connection
        );

        WinHttpCloseHandle(
            session
        );

        return false;
    }

    if (!WinHttpReceiveResponse(
            request,
            nullptr))
    {
        WinHttpCloseHandle(
            request
        );

        WinHttpCloseHandle(
            connection
        );

        WinHttpCloseHandle(
            session
        );

        return false;
    }

    DWORD status_code = 0;
    DWORD status_size =
        sizeof(status_code);

    if (!WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_STATUS_CODE |
            WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status_code,
            &status_size,
            WINHTTP_NO_HEADER_INDEX))
    {
        WinHttpCloseHandle(
            request
        );

        WinHttpCloseHandle(
            connection
        );

        WinHttpCloseHandle(
            session
        );

        return false;
    }

    if (status_code != 200)
    {
        WinHttpCloseHandle(
            request
        );

        WinHttpCloseHandle(
            connection
        );

        WinHttpCloseHandle(
            session
        );

        return false;
    }

    std::string pending;
    std::string complete_response;

    uint64_t inference_start_us =
        GetTimestampUsInternal();

    uint64_t first_token_us = 0;
    uint64_t previous_token_us = 0;

    while (
        !g_stop_inference.load())
    {
        DWORD available = 0;

        if (!WinHttpQueryDataAvailable(
                request,
                &available))
        {
            break;
        }

        if (available == 0)
            break;

        std::vector<char> buffer(
            available
        );

        DWORD downloaded = 0;

        if (!WinHttpReadData(
                request,
                buffer.data(),
                available,
                &downloaded))
        {
            break;
        }

        if (downloaded == 0)
            continue;

        pending.append(
            buffer.data(),
            downloaded
        );

        while (true)
        {
            size_t newline =
                pending.find(
                    '\n'
                );

            if (
                newline ==
                std::string::npos)
            {
                break;
            }

            std::string line =
                pending.substr(
                    0,
                    newline
                );

            pending.erase(
                0,
                newline + 1
            );

            if (
                !line.empty() &&
                line.back() == '\r')
            {
                line.pop_back();
            }

            if (
                line.rfind(
                    "data:",
                    0
                ) != 0)
            {
                continue;
            }

            std::string data =
                line.substr(5);

            while (
                !data.empty() &&
                (
                    data.front() == ' ' ||
                    data.front() == '\t'
                ))
            {
                data.erase(
                    data.begin()
                );
            }

            if (data == "[DONE]")
                continue;

            complete_response += data;
            complete_response.push_back(
                '\n'
            );

            std::string content;

            bool has_content =
                ExtractStringField(
                    data,
                    "content",
                    content
                );

            if (
                has_content &&
                !content.empty())
            {
                uint64_t now =
                    GetTimestampUsInternal();

                std::lock_guard<std::mutex> lock(
                    g_llm_mutex
                );

                if (first_token_us == 0)
                {
                    first_token_us = now;

                    g_llm.time_to_first_token_ms =
                        static_cast<double>(
                            now -
                            inference_start_us
                        ) /
                        1000.0;
                }

                if (previous_token_us != 0)
                {
                    g_llm.token_latency_ms =
                        static_cast<double>(
                            now -
                            previous_token_us
                        ) /
                        1000.0;
                }

                previous_token_us = now;

                g_llm.token_count++;

                g_llm.completion_tokens =
                    static_cast<double>(
                        g_llm.token_count
                    );

                g_llm.total_tokens =
                    g_llm.prompt_tokens +
                    g_llm.completion_tokens;

                uint64_t elapsed =
                    now -
                    inference_start_us;

                if (elapsed > 0)
                {
                    g_llm.tokens_per_second =
                        static_cast<double>(
                            g_llm.token_count
                        ) *
                        1000000.0 /
                        static_cast<double>(
                            elapsed
                        );
                }

                g_llm.available = true;
            }

            int int_value = 0;

            if (
                ExtractIntField(
                    data,
                    "completion_tokens",
                    int_value))
            {
                std::lock_guard<std::mutex> lock(
                    g_llm_mutex
                );

                g_llm.completion_tokens =
                    static_cast<double>(
                        int_value
                    );

                g_llm.token_count =
                    static_cast<uint64_t>(
                        int_value
                    );
            }

            if (
                ExtractIntField(
                    data,
                    "predicted_n",
                    int_value))
            {
                std::lock_guard<std::mutex> lock(
                    g_llm_mutex
                );

                g_llm.predicted_tokens =
                    static_cast<double>(
                        int_value
                    );
            }

            if (
                ExtractIntField(
                    data,
                    "prompt_tokens",
                    int_value))
            {
                std::lock_guard<std::mutex> lock(
                    g_llm_mutex
                );

                g_llm.prompt_tokens =
                    static_cast<double>(
                        int_value
                    );
            }

            if (
                ExtractIntField(
                    data,
                    "prompt_n",
                    int_value))
            {
                std::lock_guard<std::mutex> lock(
                    g_llm_mutex
                );

                g_llm.prompt_tokens =
                    static_cast<double>(
                        int_value
                    );
            }

            if (
                ExtractIntField(
                    data,
                    "total_tokens",
                    int_value))
            {
                std::lock_guard<std::mutex> lock(
                    g_llm_mutex
                );

                g_llm.total_tokens =
                    static_cast<double>(
                        int_value
                    );
            }

            double double_value = 0.0;

            if (
                ExtractDoubleField(
                    data,
                    "predicted_per_token_ms",
                    double_value))
            {
                std::lock_guard<std::mutex> lock(
                    g_llm_mutex
                );

                g_llm.predicted_per_token_ms =
                    double_value;
            }

            if (
                ExtractDoubleField(
                    data,
                    "predicted_per_second",
                    double_value))
            {
                std::lock_guard<std::mutex> lock(
                    g_llm_mutex
                );

                g_llm.predicted_per_second =
                    double_value;
            }
        }
    }

    if (!pending.empty())
    {
        complete_response += pending;
    }

    WinHttpCloseHandle(
        request
    );

    WinHttpCloseHandle(
        connection
    );

    WinHttpCloseHandle(
        session
    );

    ApplyFinalMetrics(
        complete_response
    );

    return true;
}

static void RunLlmWorkload()
{
    std::mt19937 rng(
        static_cast<uint32_t>(
            GetTimestampUsInternal()
        )
    );

    std::ofstream events(
        "data/raw/llm_events.csv",
        std::ios::app
    );

    if (events.is_open())
    {
        events.seekp(
            0,
            std::ios::end
        );

        if (events.tellp() == 0)
        {
            events
                << "timestamp_us,level,intensity,event\n";
        }
    }

    while (
        !g_stop_inference.load())
    {
        if (!g_workload_running.load())
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(25)
            );

            continue;
        }

        double intensity =
            GetEffectiveIntensity();

        if (intensity <= 0.001)
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(25)
            );

            continue;
        }

        int prompt_tokens =
            Interpolate(
                intensity,
                128,
                1536
            );

        int context_length =
            GetContextLength(
                intensity
            );

        int cooldown =
            GetCooldownMs(
                intensity
            );

        ResetInferenceMetrics(
            prompt_tokens,
            context_length
        );

        if (events.is_open())
        {
            events
                << GetTimestampUsInternal()
                << ","
                << static_cast<int>(
                    GetLlmWorkloadLevel()
                )
                << ","
                << intensity
                << ",START\n";

            events.flush();
        }

        std::string body =
            BuildRequestBody(
                intensity
            );

        HttpStreamPost(
            L"/v1/chat/completions",
            body
        );

        {
            std::lock_guard<std::mutex> lock(
                g_llm_mutex
            );

            g_llm.active = false;
        }

        if (events.is_open())
        {
            events
                << GetTimestampUsInternal()
                << ","
                << static_cast<int>(
                    GetLlmWorkloadLevel()
                )
                << ","
                << GetEffectiveIntensity()
                << ",END\n";

            events.flush();
        }

        int spread =
            static_cast<int>(
                static_cast<double>(
                    cooldown
                ) *
                0.20
            );

        if (spread < 5)
            spread = 5;

        std::uniform_int_distribution<int>
            distribution(
                -spread,
                spread
            );

        int sleep_ms =
            cooldown +
            distribution(
                rng
            );

        if (sleep_ms < 10)
            sleep_ms = 10;

        auto deadline =
            std::chrono::steady_clock::now() +
            std::chrono::milliseconds(
                sleep_ms
            );

        while (
            !g_stop_inference.load() &&
            std::chrono::steady_clock::now() <
                deadline)
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(10)
            );
        }
    }

    {
        std::lock_guard<std::mutex> lock(
            g_llm_mutex
        );

        g_llm.active = false;
    }

    if (events.is_open())
        events.close();
}

bool CollectLlmTelemetry(
    LlmTelemetry& telemetry)
{
    std::lock_guard<std::mutex> lock(
        g_llm_mutex
    );

    telemetry =
        g_llm;

    return telemetry.available;
}

static bool GetProcessCpuSnapshot(
    HANDLE process,
    uint64_t& process_time)
{
    FILETIME creation_time{};
    FILETIME exit_time{};
    FILETIME kernel_time{};
    FILETIME user_time{};

    if (!GetProcessTimes(
            process,
            &creation_time,
            &exit_time,
            &kernel_time,
            &user_time))
    {
        return false;
    }

    ULARGE_INTEGER kernel{};
    kernel.LowPart =
        kernel_time.dwLowDateTime;
    kernel.HighPart =
        kernel_time.dwHighDateTime;

    ULARGE_INTEGER user{};
    user.LowPart =
        user_time.dwLowDateTime;
    user.HighPart =
        user_time.dwHighDateTime;

    process_time =
        kernel.QuadPart +
        user.QuadPart;

    return true;
}

LlmRuntimeInfo CollectLlmRuntimeInfo()
{
    LlmRuntimeInfo info{};

    HANDLE snapshot =
        CreateToolhelp32Snapshot(
            TH32CS_SNAPPROCESS,
            0
        );

    if (
        snapshot ==
        INVALID_HANDLE_VALUE)
    {
        return info;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize =
        sizeof(entry);

    bool found = false;

    if (Process32FirstW(
            snapshot,
            &entry))
    {
        do
        {
            if (
                _wcsicmp(
                    entry.szExeFile,
                    L"llama-server.exe"
                ) == 0)
            {
                info.process_id =
                    static_cast<uint32_t>(
                        entry.th32ProcessID
                    );

                found = true;
                break;
            }

        } while (
            Process32NextW(
                snapshot,
                &entry
            )
        );
    }

    CloseHandle(
        snapshot
    );

    if (!found)
        return info;

    strcpy_s(info.runtime_name, sizeof(info.runtime_name), "llama.cpp");

    strcpy_s(info.model_name, sizeof(info.model_name), "gemma-3-4b-instruct-gguf");

    info.available = true;

    HANDLE process =
        OpenProcess(
            PROCESS_QUERY_INFORMATION |
            PROCESS_VM_READ,
            FALSE,
            static_cast<DWORD>(
                info.process_id
            )
        );

    if (!process)
        return info;

    PROCESS_MEMORY_COUNTERS memory{};

    if (
        GetProcessMemoryInfo(
            process,
            &memory,
            sizeof(memory)))
    {
        info.process_memory_mb =
            static_cast<double>(
                memory.WorkingSetSize
            ) /
            (
                1024.0 *
                1024.0
            );
    }

    static std::mutex cpu_mutex;
    static uint64_t previous_process_time = 0;
    static uint64_t previous_time_us = 0;
    static uint32_t previous_pid = 0;

    uint64_t process_time = 0;

    if (
        GetProcessCpuSnapshot(
            process,
            process_time))
    {
        uint64_t now_us =
            GetTimestampUsInternal();

        std::lock_guard<std::mutex> lock(
            cpu_mutex
        );

        if (
            previous_pid ==
                info.process_id &&
            previous_process_time != 0 &&
            previous_time_us != 0 &&
            now_us > previous_time_us &&
            process_time >=
                previous_process_time)
        {
            uint64_t process_delta =
                process_time -
                previous_process_time;

            uint64_t wall_delta =
                (
                    now_us -
                    previous_time_us
                ) *
                10ULL;

            double logical_processors =
                static_cast<double>(
                    GetActiveProcessorCount(
                        ALL_PROCESSOR_GROUPS
                    )
                );

            if (logical_processors < 1.0)
                logical_processors = 1.0;

            double cpu =
                (
                    static_cast<double>(
                        process_delta
                    ) /
                    static_cast<double>(
                        wall_delta
                    )
                ) *
                100.0 /
                logical_processors;

            if (cpu < 0.0)
                cpu = 0.0;

            if (cpu > 100.0)
                cpu = 100.0;

            info.process_cpu_percent =
                cpu;
        }

        previous_process_time =
            process_time;

        previous_time_us =
            now_us;

        previous_pid =
            info.process_id;
    }

    CloseHandle(
        process
    );

    return info;
}

bool DetectLlmRuntime(
    LlmRuntimeInfo& info)
{
    info =
        CollectLlmRuntimeInfo();

    return info.available;
}

bool SetLlmWorkload(
    ExperimentLlmLevel level,
    double intensity)
{
    intensity =
        ClampIntensity(
            intensity
        );

    {
        std::lock_guard<std::mutex> lock(
            g_workload_mutex
        );

        g_workload_level =
            level;

        g_workload_intensity =
            intensity;
    }

    if (intensity <= 0.001)
    {
        g_workload_running.store(
            false
        );

        std::lock_guard<std::mutex> lock(
            g_llm_mutex
        );

        g_llm.active = false;
    }
    else
    {
        g_workload_running.store(
            true
        );
    }

    return true;
}

ExperimentLlmLevel
GetLlmWorkloadLevel()
{
    std::lock_guard<std::mutex> lock(
        g_workload_mutex
    );

    return g_workload_level;
}

double GetLlmWorkloadIntensity()
{
    std::lock_guard<std::mutex> lock(
        g_workload_mutex
    );

    return g_workload_intensity;
}

void StartLlmWorkload()
{
    g_workload_running.store(
        true
    );

    if (
        !g_inference_thread.joinable())
    {
        g_stop_inference.store(
            false
        );

        g_inference_thread =
            std::thread(
                RunLlmWorkload
            );
    }
}

void StopLlmWorkload()
{
    g_workload_running.store(
        false
    );

    std::lock_guard<std::mutex> lock(
        g_llm_mutex
    );

    g_llm.active = false;
}

bool LlmBeginInference(
    int interval_ms,
    int max_tokens)
{
    if (interval_ms < 5)
        interval_ms = 5;

    if (max_tokens < 1)
        max_tokens = 1;

    StartLlmWorkload();

    return true;
}

void LlmRecordToken()
{
    std::lock_guard<std::mutex> lock(
        g_llm_mutex
    );

    if (!g_llm.active)
        return;

    g_llm.token_count++;

    g_llm.completion_tokens =
        static_cast<double>(
            g_llm.token_count
        );

    g_llm.total_tokens =
        g_llm.prompt_tokens +
        g_llm.completion_tokens;
}

void LlmEndInference()
{
    StopLlmWorkload();
}