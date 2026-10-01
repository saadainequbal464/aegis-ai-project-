#include "unified_telemetry.h"

#include <algorithm>

TelemetryBuffer::TelemetryBuffer(size_t capacity)
    : buffer(capacity),
      head(0),
      tail(0),
      max_capacity(capacity)
{
}

bool TelemetryBuffer::push(const TelemetryRecord& record)
{
    if (max_capacity == 0)
        return false;

    const size_t h = head.load(std::memory_order_relaxed);
    const size_t t = tail.load(std::memory_order_acquire);

    if (h - t >= max_capacity)
        return false;

    buffer[h % max_capacity] = record;
    head.store(h + 1, std::memory_order_release);
    return true;
}

bool TelemetryBuffer::pop(TelemetryRecord& record)
{
    if (max_capacity == 0)
        return false;

    const size_t t = tail.load(std::memory_order_relaxed);
    const size_t h = head.load(std::memory_order_acquire);

    if (t >= h)
        return false;

    record = buffer[t % max_capacity];
    tail.store(t + 1, std::memory_order_release);
    return true;
}

std::vector<TelemetryRecord> TelemetryBuffer::dumpAll()
{
    std::vector<TelemetryRecord> result;

    if (max_capacity == 0)
        return result;

    const size_t t = tail.load(std::memory_order_acquire);
    const size_t h = head.load(std::memory_order_acquire);
    const size_t count = std::min(h >= t ? h - t : size_t(0), max_capacity);

    result.reserve(count);

    for (size_t i = 0; i < count; ++i)
        result.push_back(buffer[(t + i) % max_capacity]);

    return result;
}

size_t TelemetryBuffer::size() const
{
    if (max_capacity == 0)
        return 0;

    const size_t h = head.load(std::memory_order_acquire);
    const size_t t = tail.load(std::memory_order_acquire);

    return std::min(h >= t ? h - t : size_t(0), max_capacity);
}

bool TelemetryBuffer::empty() const
{
    return size() == 0;
}

void TelemetryBuffer::clear()
{
    const size_t h = head.load(std::memory_order_acquire);
    tail.store(h, std::memory_order_release);
}
