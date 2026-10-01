#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace aegis
{

constexpr int NUM_HORIZONS = 3;

enum class LLMAction : uint8_t
{
    CONTINUE = 0,
    YIELD = 1,
    PAUSE = 2,
    RESUME = 3
};

struct RiskForecast
{
    std::array<float, NUM_HORIZONS> p_exceed{};
    uint64_t timestamp_ns = 0;
};

struct ControlDecision
{
    LLMAction action = LLMAction::CONTINUE;
    float yield_intensity = 0.0f;
    float risk_score = 0.0f;
    uint64_t decided_at_ns = 0;
    std::string reason_code;
};

struct ControllerConfig
{
    std::array<float, NUM_HORIZONS> horizon_weights{
        0.20f,
        0.30f,
        0.50f
    };

    float yield_threshold = 0.35f;
    float pause_threshold = 0.70f;
    float resume_threshold = 0.20f;

    uint64_t minimum_dwell_ns = 500000000ULL;
    uint64_t stale_forecast_ns = 2000000000ULL;

    int pause_confirmations = 2;
    int resume_confirmations = 3;
};

class AegisController
{
public:
    AegisController();
    explicit AegisController(const ControllerConfig& config);

    void reset(uint64_t timestamp_ns = 0);

    ControlDecision update(
        const RiskForecast& forecast,
        uint64_t timestamp_ns
    );

    LLMAction state() const;
    float risk_score() const;
    const ControllerConfig& config() const;

private:
    ControllerConfig config_{};
    LLMAction state_ = LLMAction::CONTINUE;

    uint64_t last_transition_ns_ = 0;
    uint64_t last_forecast_ns_ = 0;

    int pause_confirmation_count_ = 0;
    int resume_confirmation_count_ = 0;

    float last_risk_score_ = 0.0f;

    bool valid_forecast(
        const RiskForecast& forecast,
        uint64_t timestamp_ns
    ) const;

    float aggregate_risk(
        const RiskForecast& forecast
    ) const;

    float yield_intensity(
        float risk
    ) const;

    ControlDecision hold(
        uint64_t timestamp_ns,
        const std::string& reason
    ) const;

    bool dwell_elapsed(
        uint64_t timestamp_ns
    ) const;

    void transition(
        LLMAction next,
        uint64_t timestamp_ns
    );
};

}