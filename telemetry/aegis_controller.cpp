#include "aegis_controller.h"

#include <algorithm>
#include <cmath>

namespace aegis
{

namespace
{

constexpr float kRiskEpsilon = 1.0e-6f;

bool finite_probability(float value)
{
    return std::isfinite(value) &&
           value >= 0.0f &&
           value <= 1.0f;
}

bool valid_weights(
    const std::array<float, NUM_HORIZONS>& weights)
{
    float sum = 0.0f;

    for (float weight : weights)
    {
        if (!std::isfinite(weight) || weight < 0.0f)
            return false;

        sum += weight;
    }

    return std::isfinite(sum) && sum > 0.0f;
}

} // namespace

AegisController::AegisController() = default;

AegisController::AegisController(
    const ControllerConfig& config)
    : config_(config)
{
    if (!valid_weights(config_.horizon_weights))
    {
        config_.horizon_weights = {
            0.20f,
            0.30f,
            0.50f
        };
    }

    // Valid controller thresholds must stay in the same
    // probability domain as p_exceed: [0, 1], with
    // resume < yield < pause.
    if (!std::isfinite(config_.yield_threshold) ||
        !std::isfinite(config_.pause_threshold) ||
        !std::isfinite(config_.resume_threshold) ||
        config_.resume_threshold < 0.0f ||
        config_.yield_threshold < 0.0f ||
        config_.pause_threshold < 0.0f ||
        config_.resume_threshold >= config_.yield_threshold ||
        config_.yield_threshold >= config_.pause_threshold ||
        config_.pause_threshold > 1.0f)
    {
        config_.yield_threshold = 0.35f;
        config_.pause_threshold = 0.70f;
        config_.resume_threshold = 0.20f;
    }

    if (config_.pause_confirmations < 1)
        config_.pause_confirmations = 1;

    if (config_.resume_confirmations < 1)
        config_.resume_confirmations = 1;
}

void AegisController::reset(
    uint64_t timestamp_ns)
{
    state_ = LLMAction::CONTINUE;
    last_transition_ns_ = timestamp_ns;
    last_forecast_ns_ = 0;
    pause_confirmation_count_ = 0;
    resume_confirmation_count_ = 0;
    last_risk_score_ = 0.0f;
}

bool AegisController::valid_forecast(
    const RiskForecast& forecast,
    uint64_t timestamp_ns) const
{
    if (timestamp_ns == 0)
        return false;

    if (forecast.timestamp_ns == 0)
        return false;

    if (forecast.timestamp_ns > timestamp_ns)
        return false;

    if (last_forecast_ns_ != 0 &&
        forecast.timestamp_ns <= last_forecast_ns_)
        return false;

    if (timestamp_ns - forecast.timestamp_ns >
        config_.stale_forecast_ns)
        return false;

    for (float probability : forecast.p_exceed)
    {
        if (!finite_probability(probability))
            return false;
    }

    return true;
}

float AegisController::aggregate_risk(
    const RiskForecast& forecast) const
{
    float weight_sum = 0.0f;
    float score = 0.0f;

    for (int i = 0; i < NUM_HORIZONS; ++i)
    {
        const float weight =
            config_.horizon_weights[
                static_cast<std::size_t>(i)];

        score +=
            weight *
            forecast.p_exceed[
                static_cast<std::size_t>(i)];

        weight_sum += weight;
    }

    if (weight_sum <= 0.0f)
        return 0.0f;

    return std::clamp(
        score / weight_sum,
        0.0f,
        1.0f);
}

float AegisController::yield_intensity(
    float risk) const
{
    if (risk <=
        config_.yield_threshold + kRiskEpsilon)
    {
        return 0.0f;
    }

    const float span =
        config_.pause_threshold -
        config_.yield_threshold;

    if (span <= 0.0f)
        return 1.0f;

    return std::clamp(
        (risk - config_.yield_threshold) / span,
        0.0f,
        1.0f);
}

bool AegisController::dwell_elapsed(
    uint64_t timestamp_ns) const
{
    if (last_transition_ns_ == 0)
        return true;

    return timestamp_ns >= last_transition_ns_ &&
           timestamp_ns - last_transition_ns_ >=
               config_.minimum_dwell_ns;
}

ControlDecision AegisController::hold(
    uint64_t timestamp_ns,
    const std::string& reason) const
{
    ControlDecision decision;

    decision.action = state_;
    decision.yield_intensity =
        state_ == LLMAction::YIELD
            ? yield_intensity(last_risk_score_)
            : 0.0f;
    decision.risk_score = last_risk_score_;
    decision.decided_at_ns = timestamp_ns;
    decision.reason_code = reason;

    return decision;
}

void AegisController::transition(
    LLMAction next,
    uint64_t timestamp_ns)
{
    if (state_ == next)
        return;

    state_ = next;
    last_transition_ns_ = timestamp_ns;

    pause_confirmation_count_ = 0;
    resume_confirmation_count_ = 0;
}

ControlDecision AegisController::update(
    const RiskForecast& forecast,
    uint64_t timestamp_ns)
{
    if (!valid_forecast(forecast, timestamp_ns))
    {
        return hold(
            timestamp_ns,
            forecast.timestamp_ns < timestamp_ns
                ? "STALE_FORECAST"
                : "INVALID_FORECAST");
    }

    last_forecast_ns_ = forecast.timestamp_ns;

    const float risk =
        aggregate_risk(forecast);

    last_risk_score_ = risk;

    const bool pause_band =
        risk + kRiskEpsilon >=
        config_.pause_threshold;

    const bool yield_band =
        risk + kRiskEpsilon >=
        config_.yield_threshold;

    const bool resume_band =
        risk <=
        config_.resume_threshold + kRiskEpsilon;

    if (state_ == LLMAction::PAUSE)
    {
        if (!dwell_elapsed(timestamp_ns))
        {
            return hold(
                timestamp_ns,
                "MIN_DWELL_HOLD");
        }

        if (resume_band)
        {
            ++resume_confirmation_count_;

            if (resume_confirmation_count_ >=
                config_.resume_confirmations)
            {
                transition(
                    LLMAction::RESUME,
                    timestamp_ns);

                return hold(
                    timestamp_ns,
                    "RESUME_CONFIRMED");
            }

            return hold(
                timestamp_ns,
                "RESUME_CONFIRMATION_PENDING");
        }

        resume_confirmation_count_ = 0;

        return hold(
            timestamp_ns,
            "PAUSE_HOLD");
    }

    if (state_ == LLMAction::YIELD)
    {
        if (pause_band)
        {
            ++pause_confirmation_count_;

            if (pause_confirmation_count_ >=
                config_.pause_confirmations &&
                dwell_elapsed(timestamp_ns))
            {
                transition(
                    LLMAction::PAUSE,
                    timestamp_ns);

                return hold(
                    timestamp_ns,
                    "PAUSE_RISK_CONFIRMED");
            }

            return hold(
                timestamp_ns,
                "PAUSE_CONFIRMATION_PENDING");
        }

        pause_confirmation_count_ = 0;

        if (yield_band)
        {
            return hold(
                timestamp_ns,
                "YIELD_HOLD");
        }

        if (resume_band &&
            dwell_elapsed(timestamp_ns))
        {
            transition(
                LLMAction::CONTINUE,
                timestamp_ns);

            return hold(
                timestamp_ns,
                "RISK_CLEARED");
        }

        return hold(
            timestamp_ns,
            "HYSTERESIS_HOLD");
    }

    if (state_ == LLMAction::RESUME)
    {
        if (yield_band)
        {
            transition(
                LLMAction::YIELD,
                timestamp_ns);

            return hold(
                timestamp_ns,
                "PREDICTIVE_RISK");
        }

        transition(
            LLMAction::CONTINUE,
            timestamp_ns);

        return hold(
            timestamp_ns,
            "RESUME_COMPLETE");
    }

    if (pause_band)
    {
        ++pause_confirmation_count_;

        if (pause_confirmation_count_ >=
            config_.pause_confirmations &&
            dwell_elapsed(timestamp_ns))
        {
            transition(
                LLMAction::PAUSE,
                timestamp_ns);

            return hold(
                timestamp_ns,
                "PAUSE_RISK_CONFIRMED");
        }

        return hold(
            timestamp_ns,
            "PAUSE_CONFIRMATION_PENDING");
    }

    pause_confirmation_count_ = 0;

    if (yield_band)
    {
        if (state_ != LLMAction::YIELD)
        {
            transition(
                LLMAction::YIELD,
                timestamp_ns);

            return hold(
                timestamp_ns,
                "PREDICTIVE_RISK");
        }

        return hold(
            timestamp_ns,
            "YIELD_HOLD");
    }

    return hold(
        timestamp_ns,
        "NORMAL_OPERATION");
}

LLMAction AegisController::state() const
{
    return state_;
}

float AegisController::risk_score() const
{
    return last_risk_score_;
}

const ControllerConfig&
AegisController::config() const
{
    return config_;
}

} // namespace aegis
