#include "aegis_controller.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>

namespace
{

int passed = 0;
int failed = 0;

void Check(bool condition, const std::string& name)
{
    if (condition)
    {
        ++passed;
        std::cout << "[PASS] " << name << "\n";
    }
    else
    {
        ++failed;
        std::cout << "[FAIL] " << name << "\n";
    }
}

bool Near(float a, float b, float eps = 1.0e-5f)
{
    return std::fabs(a - b) <= eps;
}

aegis::RiskForecast Forecast(
    float p250,
    float p750,
    float p2000,
    std::uint64_t timestamp)
{
    aegis::RiskForecast f;
    f.p_exceed = {p250, p750, p2000};
    f.timestamp_ns = timestamp;
    return f;
}

} // namespace

int main()
{
    using namespace aegis;

    // ------------------------------------------------------------
    // A. Default configuration
    // ------------------------------------------------------------
    {
        AegisController c;

        Check(
            Near(c.config().horizon_weights[0], 0.20f) &&
            Near(c.config().horizon_weights[1], 0.30f) &&
            Near(c.config().horizon_weights[2], 0.50f),
            "Default horizon weights = 0.20 / 0.30 / 0.50");

        Check(
            Near(c.config().yield_threshold, 0.35f) &&
            Near(c.config().pause_threshold, 0.70f) &&
            Near(c.config().resume_threshold, 0.20f),
            "Default thresholds = yield 0.35 / pause 0.70 / resume 0.20");

        Check(
            c.config().pause_confirmations == 2 &&
            c.config().resume_confirmations == 3,
            "Default confirmations = pause 2 / resume 3");

        Check(
            c.config().minimum_dwell_ns == 500000000ULL &&
            c.config().stale_forecast_ns == 2000000000ULL,
            "Default dwell = 500 ms / stale window = 2 s");
    }

    // ------------------------------------------------------------
    // B. Configuration validation
    // ------------------------------------------------------------
    {
        ControllerConfig cfg;
        cfg.resume_threshold = -0.1f;

        AegisController c(cfg);

        Check(
            Near(c.config().resume_threshold, 0.20f) &&
            Near(c.config().yield_threshold, 0.35f) &&
            Near(c.config().pause_threshold, 0.70f),
            "Invalid negative threshold falls back to safe defaults");
    }

    {
        ControllerConfig cfg;
        cfg.yield_threshold = -0.1f;

        AegisController c(cfg);

        Check(
            Near(c.config().resume_threshold, 0.20f) &&
            Near(c.config().yield_threshold, 0.35f) &&
            Near(c.config().pause_threshold, 0.70f),
            "Invalid yield threshold below 0 is rejected");
    }

    {
        ControllerConfig cfg;
        cfg.pause_threshold = 1.1f;

        AegisController c(cfg);

        Check(
            Near(c.config().resume_threshold, 0.20f) &&
            Near(c.config().yield_threshold, 0.35f) &&
            Near(c.config().pause_threshold, 0.70f),
            "Invalid pause threshold above 1 is rejected");
    }

    {
        ControllerConfig cfg;
        cfg.resume_threshold = 0.50f;
        cfg.yield_threshold = 0.40f;
        cfg.pause_threshold = 0.70f;

        AegisController c(cfg);

        Check(
            Near(c.config().resume_threshold, 0.20f) &&
            Near(c.config().yield_threshold, 0.35f) &&
            Near(c.config().pause_threshold, 0.70f),
            "Invalid threshold ordering is rejected");
    }

    // ------------------------------------------------------------
    // C. Normal operation
    // ------------------------------------------------------------
    {
        AegisController c;

        auto d = c.update(
            Forecast(0.05f, 0.05f, 0.05f, 1000000000ULL),
            1000000000ULL);

        Check(
            d.action == LLMAction::CONTINUE,
            "Low risk -> CONTINUE");

        Check(
            d.reason_code == "NORMAL_OPERATION",
            "Low risk reason = NORMAL_OPERATION");

        Check(
            d.risk_score >= 0.0f &&
            d.risk_score <= 1.0f,
            "Risk score bounded to [0,1]");
    }

    // ------------------------------------------------------------
    // D. Weighted aggregation
    // Expected:
    // 0.20*0.10 + 0.30*0.50 + 0.50*0.70 = 0.52
    // ------------------------------------------------------------
    {
        AegisController c;

        auto d = c.update(
            Forecast(0.10f, 0.50f, 0.70f, 1000000000ULL),
            1000000000ULL);

        Check(
            Near(d.risk_score, 0.52f),
            "Weighted horizon aggregation is correct");

        Check(
            d.action == LLMAction::YIELD,
            "Aggregated risk in yield band -> YIELD");
    }

    // ------------------------------------------------------------
    // E. Yield intensity
    // ------------------------------------------------------------
    {
        AegisController c;

        auto d = c.update(
            Forecast(0.35f, 0.35f, 0.35f, 1000000000ULL),
            1000000000ULL);

        Check(
            d.action == LLMAction::CONTINUE,
            "Risk exactly at yield threshold stays CONTINUE");

        Check(
            Near(d.yield_intensity, 0.0f),
            "Yield intensity at yield threshold = 0");
    }

    {
        AegisController c;

        auto d = c.update(
            Forecast(0.525f, 0.525f, 0.525f, 1000000000ULL),
            1000000000ULL);

        Check(
            d.action == LLMAction::YIELD,
            "Mid-band risk -> YIELD");

        Check(
            Near(d.yield_intensity, 0.5f),
            "Yield intensity scales linearly");
    }

    {
        AegisController c;

        auto d = c.update(
            Forecast(0.70f, 0.70f, 0.70f, 1000000000ULL),
            1000000000ULL);

        Check(
            d.action == LLMAction::YIELD,
            "First high-risk sample does not immediately PAUSE");

        Check(
            d.reason_code == "PAUSE_CONFIRMATION_PENDING",
            "First PAUSE confirmation is pending");
    }

    // ------------------------------------------------------------
    // F. YIELD -> PAUSE confirmation
    // Need two high-risk samples. Dwell is already elapsed because
    // the first transition into YIELD occurred at t=1s.
    // ------------------------------------------------------------
    {
        AegisController c;

        c.update(
            Forecast(0.50f, 0.50f, 0.50f, 1000000000ULL),
            1000000000ULL);

        auto d1 = c.update(
            Forecast(0.70f, 0.70f, 0.70f, 1600000000ULL),
            1600000000ULL);

        Check(
            d1.action == LLMAction::YIELD &&
            d1.reason_code == "PAUSE_CONFIRMATION_PENDING",
            "First PAUSE confirmation remains pending");

        auto d2 = c.update(
            Forecast(0.70f, 0.70f, 0.70f, 2200000000ULL),
            2200000000ULL);

        Check(
            d2.action == LLMAction::PAUSE,
            "Second confirmed high-risk sample -> PAUSE");

        Check(
            d2.reason_code == "PAUSE_RISK_CONFIRMED",
            "PAUSE reason = PAUSE_RISK_CONFIRMED");
    }

    // ------------------------------------------------------------
    // G. PAUSE hold and recovery confirmations
    // ------------------------------------------------------------
    {
        AegisController c;

        c.update(
            Forecast(0.70f, 0.70f, 0.70f, 1000000000ULL),
            1000000000ULL);

        c.update(
            Forecast(0.70f, 0.70f, 0.70f, 1600000000ULL),
            1600000000ULL);

        auto pause = c.update(
            Forecast(0.70f, 0.70f, 0.70f, 2200000000ULL),
            2200000000ULL);

        Check(
            pause.action == LLMAction::PAUSE,
            "Controller enters PAUSE");

        auto hold = c.update(
            Forecast(0.80f, 0.80f, 0.80f, 2800000000ULL),
            2800000000ULL);

        Check(
            hold.action == LLMAction::PAUSE,
            "High risk while paused -> remain PAUSE");

        Check(
            hold.reason_code == "PAUSE_HOLD",
            "High risk while paused -> PAUSE_HOLD");
    }

    {
        AegisController c;

        c.update(
            Forecast(0.70f, 0.70f, 0.70f, 1000000000ULL),
            1000000000ULL);

        c.update(
            Forecast(0.70f, 0.70f, 0.70f, 1600000000ULL),
            1600000000ULL);

        c.update(
            Forecast(0.70f, 0.70f, 0.70f, 2200000000ULL),
            2200000000ULL);

        auto r1 = c.update(
            Forecast(0.10f, 0.10f, 0.10f, 2800000000ULL),
            2800000000ULL);

        Check(
            r1.action == LLMAction::PAUSE &&
            r1.reason_code == "RESUME_CONFIRMATION_PENDING",
            "First RESUME confirmation is pending");

        auto r2 = c.update(
            Forecast(0.10f, 0.10f, 0.10f, 3400000000ULL),
            3400000000ULL);

        Check(
            r2.action == LLMAction::PAUSE &&
            r2.reason_code == "RESUME_CONFIRMATION_PENDING",
            "Second RESUME confirmation is pending");

        auto r3 = c.update(
            Forecast(0.10f, 0.10f, 0.10f, 4000000000ULL),
            4000000000ULL);

        Check(
            r3.action == LLMAction::RESUME,
            "Third confirmed low-risk sample -> RESUME");

        Check(
            r3.reason_code == "RESUME_CONFIRMED",
            "RESUME reason = RESUME_CONFIRMED");
    }

    // ------------------------------------------------------------
    // H. RESUME -> CONTINUE is a separate controller step.
    // ------------------------------------------------------------
    {
        AegisController c;

        c.update(
            Forecast(0.70f, 0.70f, 0.70f, 1000000000ULL),
            1000000000ULL);

        c.update(
            Forecast(0.70f, 0.70f, 0.70f, 1600000000ULL),
            1600000000ULL);

        c.update(
            Forecast(0.70f, 0.70f, 0.70f, 2200000000ULL),
            2200000000ULL);

        c.update(
            Forecast(0.10f, 0.10f, 0.10f, 2800000000ULL),
            2800000000ULL);

        c.update(
            Forecast(0.10f, 0.10f, 0.10f, 3400000000ULL),
            3400000000ULL);

        auto resume = c.update(
            Forecast(0.10f, 0.10f, 0.10f, 4000000000ULL),
            4000000000ULL);

        Check(
            resume.action == LLMAction::RESUME,
            "Recovery confirmation produces RESUME state");

        auto complete = c.update(
            Forecast(0.10f, 0.10f, 0.10f, 4600000000ULL),
            4600000000ULL);

        Check(
            complete.action == LLMAction::CONTINUE &&
            complete.reason_code == "RESUME_COMPLETE",
            "Next low-risk update after RESUME -> CONTINUE");
    }

    // ------------------------------------------------------------
    // I. Hysteresis
    // ------------------------------------------------------------
    {
        AegisController c;

        auto y = c.update(
            Forecast(0.50f, 0.50f, 0.50f, 1000000000ULL),
            1000000000ULL);

        Check(
            y.action == LLMAction::YIELD,
            "Risk above yield threshold -> YIELD");

        auto h = c.update(
            Forecast(0.30f, 0.30f, 0.30f, 1600000000ULL),
            1600000000ULL);

        Check(
            h.action == LLMAction::YIELD &&
            h.reason_code == "YIELD_HOLD",
            "Risk between resume/yield thresholds -> hold YIELD");

        auto cleared = c.update(
            Forecast(0.20f, 0.20f, 0.20f, 2200000000ULL),
            2200000000ULL);

        Check(
            cleared.action == LLMAction::CONTINUE &&
            cleared.reason_code == "RISK_CLEARED",
            "Risk at resume threshold -> recovery to CONTINUE");
    }

    // ------------------------------------------------------------
    // J. Invalid probabilities
    // ------------------------------------------------------------
    {
        AegisController c;

        auto d = c.update(
            Forecast(
                std::nanf(""),
                0.1f,
                0.1f,
                1000000000ULL),
            1000000000ULL);

        Check(
            d.reason_code == "INVALID_FORECAST",
            "NaN probability rejected");
    }

    {
        AegisController c;

        auto d = c.update(
            Forecast(
                1.1f,
                0.1f,
                0.1f,
                1000000000ULL),
            1000000000ULL);

        Check(
            d.reason_code == "INVALID_FORECAST",
            "Probability > 1 rejected");
    }

    {
        AegisController c;

        auto d = c.update(
            Forecast(
                -0.1f,
                0.1f,
                0.1f,
                1000000000ULL),
            1000000000ULL);

        Check(
            d.reason_code == "INVALID_FORECAST",
            "Probability < 0 rejected");
    }

    // ------------------------------------------------------------
    // K. Timestamp validation
    // ------------------------------------------------------------
    {
        AegisController c;

        auto zero = c.update(
            Forecast(0.1f, 0.1f, 0.1f, 0),
            1000000000ULL);

        Check(
            zero.reason_code == "INVALID_FORECAST",
            "Zero forecast timestamp rejected");
    }

    {
        AegisController c;

        auto future = c.update(
            Forecast(0.1f, 0.1f, 0.1f, 2000000000ULL),
            1000000000ULL);

        Check(
            future.reason_code == "INVALID_FORECAST",
            "Future forecast timestamp rejected");
    }

    {
        AegisController c;

        auto valid = c.update(
            Forecast(0.1f, 0.1f, 0.1f, 1000000000ULL),
            1000000000ULL);

        Check(
            valid.reason_code == "NORMAL_OPERATION",
            "Valid forecast accepted");
    }

    {
        AegisController c;

        c.update(
            Forecast(0.1f, 0.1f, 0.1f, 1000000000ULL),
            1000000000ULL);

        auto duplicate = c.update(
            Forecast(0.1f, 0.1f, 0.1f, 1000000000ULL),
            1000000000ULL);

        Check(
            duplicate.reason_code == "INVALID_FORECAST",
            "Duplicate forecast timestamp rejected");
    }

    {
        AegisController c;

        c.update(
            Forecast(0.1f, 0.1f, 0.1f, 1000000000ULL),
            1000000000ULL);

        auto old = c.update(
            Forecast(0.1f, 0.1f, 0.1f, 1000000001ULL),
            4000000001ULL);

        Check(
            old.reason_code == "STALE_FORECAST",
            "Forecast older than stale window rejected");
    }

    std::cout << "\n========================================\n";
    std::cout << "AEGIS CONTROLLER AUDIT V2\n";
    std::cout << "PASS: " << passed << "\n";
    std::cout << "FAIL: " << failed << "\n";

    if (failed == 0)
    {
        std::cout << "RESULT: CONTROLLER POLICY AUDIT PASSED\n";
        return 0;
    }

    std::cout << "RESULT: CONTROLLER POLICY AUDIT FAILED\n";
    return 1;
}
