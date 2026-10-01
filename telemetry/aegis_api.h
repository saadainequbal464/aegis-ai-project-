#pragma once

#ifndef AEGIS_API_H
#define AEGIS_API_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

namespace aegis::api {

// ============================================================
// Runtime state exposed to the frontend
// ============================================================

struct RuntimeState {
    bool connected = false;
    bool aegis_running = false;

    bool server_running = false;
    bool llm_started = false;

    int llm_level_id = 0;
    std::string llm_level = "LOW";

    // Live AEGIS telemetry
    double tokens_per_sec = 0.0;
    double frame_time_ms = 0.0;
    double fps = 0.0;

    // Multi-horizon risk forecast
    double risk_250ms = 0.0;
    double risk_750ms = 0.0;
    double risk_2000ms = 0.0;

    // Current controller decision
    std::string controller_action = "CONTINUE";
    std::string controller_reason;
};

// ============================================================
// Commands supplied by the existing AEGIS runtime
// ============================================================

struct RuntimeCallbacks {
    // Start the existing AEGIS runtime.
    std::function<bool()> start_aegis;

    // Increase the existing LLM workload level.
    std::function<bool()> increase_llm;

    // Stop the existing AEGIS runtime.
    std::function<bool()> stop_aegis;

    // Return the current real runtime state.
    std::function<RuntimeState()> get_state;
};

// ============================================================
// AEGIS HTTP API
// ============================================================

class AegisApi {
public:
    AegisApi() = default;
    ~AegisApi();

    AegisApi(const AegisApi&) = delete;
    AegisApi& operator=(const AegisApi&) = delete;

    // Attach the existing AEGIS runtime to this API layer.
    void SetCallbacks(RuntimeCallbacks callbacks);

    // Start HTTP server on 127.0.0.1:8787.
    bool Start(const std::string& host = "127.0.0.1",
               std::uint16_t port = 8787);

    // Stop HTTP server.
    void Stop();

    // Check whether API server is running.
    bool IsRunning() const;

private:
    class Impl;
    Impl* impl_ = nullptr;

    std::atomic<bool> running_{false};
};

} // namespace aegis::api

#endif // AEGIS_API_H