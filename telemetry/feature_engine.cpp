#include "unified_telemetry.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <deque>
#include <vector>

namespace
{
double finite_or_zero(double v)
{
    return std::isfinite(v) ? v : 0.0;
}

double mean(const std::vector<double>& v)
{
    if (v.empty()) return 0.0;

    double sum = 0.0;
    for (double x : v) sum += x;
    return sum / static_cast<double>(v.size());
}

double maximum(const std::vector<double>& v)
{
    if (v.empty()) return 0.0;
    return *std::max_element(v.begin(), v.end());
}

double minimum(const std::vector<double>& v)
{
    if (v.empty()) return 0.0;
    return *std::min_element(v.begin(), v.end());
}

double standard_deviation(const std::vector<double>& v)
{
    if (v.size() < 2) return 0.0;

    const double m = mean(v);
    double variance = 0.0;

    for (double x : v)
    {
        const double d = x - m;
        variance += d * d;
    }

    return std::sqrt(variance / static_cast<double>(v.size()));
}

double trend(const std::vector<double>& v)
{
    if (v.size() < 2) return 0.0;

    const double n = static_cast<double>(v.size());
    const double x_mean = (n - 1.0) / 2.0;
    const double y_mean = mean(v);

    double numerator = 0.0;
    double denominator = 0.0;

    for (size_t i = 0; i < v.size(); ++i)
    {
        const double x = static_cast<double>(i);
        numerator += (x - x_mean) * (v[i] - y_mean);
        denominator += (x - x_mean) * (x - x_mean);
    }

    return denominator == 0.0 ? 0.0 : numerator / denominator;
}

template <typename Getter>
std::vector<double> collect(
    const std::deque<TelemetryRecord>& records,
    Getter getter)
{
    std::vector<double> values;
    values.reserve(records.size());

    for (const auto& record : records)
        values.push_back(finite_or_zero(getter(record)));

    return values;
}

std::vector<double> gpu_memory_ratio(
    const std::deque<TelemetryRecord>& records)
{
    std::vector<double> values;
    values.reserve(records.size());

    for (const auto& r : records)
    {
        if (r.gpu_memory_budget_bytes == 0)
            values.push_back(0.0);
        else
            values.push_back(
                static_cast<double>(r.gpu_memory_used_bytes) /
                static_cast<double>(r.gpu_memory_budget_bytes)
            );
    }

    return values;
}
}

FeatureVector ComputeFeatureVector(
    const std::deque<TelemetryRecord>& records)
{
    FeatureVector result;

    if (records.empty())
        return result;

    const auto cpu = collect(records,
        [](const TelemetryRecord& r) { return r.cpu_util_total; });

    const auto memory = collect(records,
        [](const TelemetryRecord& r) { return r.memory_load_percent; });

    const auto gpu = collect(records,
        [](const TelemetryRecord& r) { return r.gpu_utilization_percent; });

    const auto gpu_mem = gpu_memory_ratio(records);

    const auto gpu_temp = collect(records,
        [](const TelemetryRecord& r) { return r.gpu_temperature_c; });

    const auto npu = collect(records,
        [](const TelemetryRecord& r) { return r.npu_utilization_percent; });

    const auto dram_read = collect(records,
        [](const TelemetryRecord& r) { return r.dram_read_bandwidth_mb_s; });

    const auto dram_write = collect(records,
        [](const TelemetryRecord& r) { return r.dram_write_bandwidth_mb_s; });

    const auto dram_util = collect(records,
        [](const TelemetryRecord& r) { return r.dram_utilization_percent; });

    const auto frame_time = collect(records,
        [](const TelemetryRecord& r) { return r.frame_time_ms; });

    const auto fps = collect(records,
        [](const TelemetryRecord& r) { return r.fps; });

    const auto token_rate = collect(records,
        [](const TelemetryRecord& r) { return r.tokens_per_sec; });

    const auto token_latency = collect(records,
        [](const TelemetryRecord& r) { return r.token_latency_ms; });

    const auto ttft = collect(records,
        [](const TelemetryRecord& r) { return r.ttft_ms; });

    result.features.reserve(40);

    result.features.push_back(static_cast<float>(mean(cpu)));
    result.features.push_back(static_cast<float>(maximum(cpu)));
    result.features.push_back(static_cast<float>(standard_deviation(cpu)));
    result.features.push_back(static_cast<float>(trend(cpu)));

    result.features.push_back(static_cast<float>(mean(memory)));
    result.features.push_back(static_cast<float>(maximum(memory)));
    result.features.push_back(static_cast<float>(trend(memory)));

    result.features.push_back(static_cast<float>(mean(gpu)));
    result.features.push_back(static_cast<float>(maximum(gpu)));
    result.features.push_back(static_cast<float>(trend(gpu)));
    result.features.push_back(static_cast<float>(mean(gpu_mem)));
    result.features.push_back(static_cast<float>(maximum(gpu_mem)));
    result.features.push_back(static_cast<float>(trend(gpu_mem)));
    result.features.push_back(static_cast<float>(mean(gpu_temp)));
    result.features.push_back(static_cast<float>(maximum(gpu_temp)));

    result.features.push_back(static_cast<float>(mean(npu)));
    result.features.push_back(static_cast<float>(maximum(npu)));
    result.features.push_back(static_cast<float>(trend(npu)));

    result.features.push_back(static_cast<float>(mean(dram_read)));
    result.features.push_back(static_cast<float>(maximum(dram_read)));
    result.features.push_back(static_cast<float>(mean(dram_write)));
    result.features.push_back(static_cast<float>(maximum(dram_write)));
    result.features.push_back(static_cast<float>(mean(dram_util)));
    result.features.push_back(static_cast<float>(maximum(dram_util)));
    result.features.push_back(static_cast<float>(trend(dram_util)));

    result.features.push_back(static_cast<float>(mean(frame_time)));
    result.features.push_back(static_cast<float>(maximum(frame_time)));
    result.features.push_back(static_cast<float>(standard_deviation(frame_time)));
    result.features.push_back(static_cast<float>(trend(frame_time)));
    result.features.push_back(static_cast<float>(mean(fps)));
    result.features.push_back(static_cast<float>(minimum(fps)));
    result.features.push_back(static_cast<float>(trend(fps)));

    result.features.push_back(static_cast<float>(mean(token_rate)));
    result.features.push_back(static_cast<float>(minimum(token_rate)));
    result.features.push_back(static_cast<float>(trend(token_rate)));

    result.features.push_back(static_cast<float>(mean(token_latency)));
    result.features.push_back(static_cast<float>(maximum(token_latency)));
    result.features.push_back(static_cast<float>(trend(token_latency)));

    result.features.push_back(static_cast<float>(mean(ttft)));
    result.features.push_back(static_cast<float>(maximum(ttft)));

    return result;
}

FeatureEngine::FeatureEngine(size_t window_size)
    : window_size(window_size)
{
}

void FeatureEngine::push(const TelemetryRecord& record)
{
    if (window_size == 0)
        return;

    records.push_back(record);

    while (records.size() > window_size)
        records.pop_front();
}

FeatureVector FeatureEngine::compute() const
{
    return ComputeFeatureVector(records);
}

size_t FeatureEngine::size() const
{
    return records.size();
}

void FeatureEngine::clear()
{
    records.clear();
}