/*
    AEGIS AI - Controller A-to-Z Audit
    ----------------------------------
    Purpose:
      Deterministically audit AegisController policy behavior without
      running the 5-hour experiment.

    This verifies:
      1. Configuration / thresholds
      2. Weighted risk aggregation
      3. CONTINUE under normal risk
      4. YIELD transition
      5. Yield-intensity scaling
      6. PAUSE confirmation requirement
      7. Minimum dwell / hysteresis behavior
      8. PAUSE_HOLD behavior
      9. RESUME confirmation requirement
     10. RESUME -> CONTINUE completion
     11. stale forecast rejection
     12. invalid probability rejection
     13. future timestamp rejection
     14. duplicate/out-of-order forecast rejection
     15. NaN probability rejection
     16. risk-score bounds

    IMPORTANT:
      This file audits the controller policy itself.
      It does NOT claim to prove that llama-server physically stops/resumes.
      That runtime actuation must be verified separately from the live AEGIS run.
*/

#include "aegis_controller.h"

#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>

namespace {

using aegis::AegisController;
using aegis::ControlDecision;
using aegis::ControllerConfig;
using aegis::LLMAction;
using aegis::RiskForecast;

int passed = 0;
int failed = 0;

const char* ActionName(LLMAction action)
{
    switch (action)
    {
    case LLMAction::CONTINUE: return "CONTINUE";
    case LLMAction::YIELD:    return "YIELD";
    case LLMAction::PAUSE:    return "PAUSE";
    case LLMAction::RESUME:   return "RESUME";
    default:                  return "UNKNOWN";
    }
}

void Check(
    bool condition,
    const std::string& name)
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

RiskForecast Forecast(
    float p250,
    float p750,
    float p2000,
    uint64_t timestamp_ns)
{
    RiskForecast f;
    f.p_exceed = {p250, p750, p2000};
    f.timestamp_ns = timestamp_ns;
    return f;
}

ControlDecision Update(
    AegisController& controller,
    float risk,
    uint64_t t)
{
    return controller.update(
        Forecast(risk, risk, risk, t),
        t);
}

} // namespace

int main()
{
    std::cout
        << "============================================================\n"
        << "        AEGIS AI CONTROLLER A-TO-Z AUDIT\n"
        << "============================================================\n\n";

    ControllerConfig cfg;
    AegisController controller(cfg);

    // ---------------------------------------------------------
    // A. Configuration audit
    // ---------------------------------------------------------
    std::cout << "[A] CONFIGURATION\n";

    Check(
        cfg.yield_threshold == 0.35f,
        "Yield threshold = 0.35");

    Check(
        cfg.pause_threshold == 0.70f,
        "Pause threshold = 0.70");

    Check(
        cfg.resume_threshold == 0.20f,
        "Resume threshold = 0.20");

    Check(
        cfg.pause_confirmations == 2,
        "Pause confirmations = 2");

    Check(
        cfg.resume_confirmations == 3,
        "Resume confirmations = 3");

    Check(
        cfg.minimum_dwell_ns == 500000000ULL,
        "Minimum dwell = 500 ms");

    Check(
        cfg.stale_forecast_ns == 2000000000ULL,
        "Forecast stale window = 2 s");

    // ---------------------------------------------------------
    // B. Normal operation
    // ---------------------------------------------------------
    std::cout << "\n[B] NORMAL OPERATION\n";

    controller.reset();

    auto d = Update(controller, 0.10f, 1000000000ULL);

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

    // ---------------------------------------------------------
    // C. Weighted aggregation
    // ---------------------------------------------------------
    std::cout << "\n[C] WEIGHTED RISK AGGREGATION\n";

    controller.reset();

    RiskForecast weighted = Forecast(
        0.10f,
        0.40f,
        0.80f,
        2000000000ULL);

    d = controller.update(
        weighted,
        2000000000ULL);

    const float expected =
        (0.20f * 0.10f +
         0.30f * 0.40f +
         0.50f * 0.80f) /
        (0.20f + 0.30f + 0.50f);

    Check(
        std::fabs(d.risk_score - expected) < 1e-5f,
        "Weighted horizon aggregation is correct");

    Check(
        d.action == LLMAction::YIELD,
        "Aggregated risk in yield band -> YIELD");

    // ---------------------------------------------------------
    // D. Yield scaling
    // ---------------------------------------------------------
    std::cout << "\n[D] YIELD INTENSITY\n";

    controller.reset();

    d = Update(controller, 0.35f, 3000000000ULL);

    Check(
        d.action == LLMAction::YIELD,
        "Risk at yield threshold -> YIELD");

    Check(
        d.yield_intensity >= 0.0f &&
        d.yield_intensity <= 1.0f,
        "Yield intensity bounded to [0,1]");

    d = Update(controller, 0.525f, 3600000000ULL);

    const float expected_yield =
        (0.525f - 0.35f) /
        (0.70f - 0.35f);

    Check(
        std::fabs(d.yield_intensity - expected_yield) < 1e-5f,
        "Yield intensity scales linearly between thresholds");

    // ---------------------------------------------------------
    // E. YIELD -> PAUSE confirmation + dwell
    // ---------------------------------------------------------
    std::cout << "\n[E] YIELD -> PAUSE CONFIRMATION\n";

    controller.reset();

    d = Update(controller, 0.50f, 5000000000ULL);
    Check(
        d.action == LLMAction::YIELD,
        "Medium risk enters YIELD");

    // First high-risk confirmation arrives before 500 ms dwell.
    d = Update(controller, 0.90f, 5200000000ULL);

    Check(
        d.action == LLMAction::YIELD,
        "First high-risk sample does not immediately PAUSE");

    Check(
        d.reason_code == "PAUSE_CONFIRMATION_PENDING",
        "First PAUSE confirmation is pending");

    // Second confirmation, dwell has elapsed.
    d = Update(controller, 0.90f, 5600000000ULL);

    Check(
        d.action == LLMAction::PAUSE,
        "Second confirmed high-risk sample -> PAUSE");

    Check(
        d.reason_code == "PAUSE_RISK_CONFIRMED",
        "PAUSE reason = PAUSE_RISK_CONFIRMED");

    // ---------------------------------------------------------
    // F. PAUSE hold
    // ---------------------------------------------------------
    std::cout << "\n[F] PAUSE HOLD\n";

    d = Update(controller, 0.90f, 5700000000ULL);

    Check(
        d.action == LLMAction::PAUSE,
        "High risk while paused -> remain PAUSE");

    Check(
        d.reason_code == "PAUSE_HOLD" ||
        d.reason_code == "MIN_DWELL_HOLD",
        "Paused controller does not oscillate");

    // ---------------------------------------------------------
    // G. PAUSE -> RESUME confirmation
    // ---------------------------------------------------------
    std::cout << "\n[G] PAUSE -> RESUME CONFIRMATION\n";

    // Dwell has elapsed; low risk begins recovery.
    d = Update(controller, 0.10f, 6200000000ULL);

    Check(
        d.action == LLMAction::PAUSE,
        "First low-risk sample does not immediately RESUME");

    Check(
        d.reason_code == "RESUME_CONFIRMATION_PENDING",
        "First RESUME confirmation is pending");

    d = Update(controller, 0.10f, 6800000000ULL);

    Check(
        d.action == LLMAction::PAUSE,
        "Second low-risk sample still held");

    Check(
        d.reason_code == "RESUME_CONFIRMATION_PENDING",
        "Second RESUME confirmation is pending");

    d = Update(controller, 0.10f, 7400000000ULL);

    Check(
        d.action == LLMAction::RESUME,
        "Third confirmed low-risk sample -> RESUME");

    Check(
        d.reason_code == "RESUME_CONFIRMED",
        "RESUME reason = RESUME_CONFIRMED");

    // ---------------------------------------------------------
    // H. RESUME -> CONTINUE
    // ---------------------------------------------------------
    std::cout << "\n[H] RESUME COMPLETION\n";

    d = Update(controller, 0.10f, 7500000000ULL);

    Check(
        d.action == LLMAction::CONTINUE,
        "Low risk after RESUME -> CONTINUE");

    Check(
        d.reason_code == "RESUME_COMPLETE",
        "Resume completion reason is correct");

    // ---------------------------------------------------------
    // I. Hysteresis / no oscillation
    // ---------------------------------------------------------
    std::cout << "\n[I] HYSTERESIS\n";

    controller.reset();

    d = Update(controller, 0.50f, 8000000000ULL);
    Check(
        d.action == LLMAction::YIELD,
        "Risk above yield threshold -> YIELD");

    d = Update(controller, 0.30f, 8600000000ULL);
    Check(
        d.action == LLMAction::YIELD,
        "Risk between resume/yield thresholds -> hold YIELD");

    Check(
        d.reason_code == "YIELD_HOLD",
        "Hysteresis prevents premature recovery");

    // ---------------------------------------------------------
    // J. Invalid probability
    // ---------------------------------------------------------
    std::cout << "\n[J] INVALID PROBABILITY HANDLING\n";

    controller.reset();

    d = controller.update(
        Forecast(
            std::numeric_limits<float>::quiet_NaN(),
            0.2f,
            0.2f,
            9000000000ULL),
        9000000000ULL);

    Check(
        d.reason_code == "INVALID_FORECAST",
        "NaN probability rejected");

    d = controller.update(
        Forecast(
            1.2f,
            0.2f,
            0.2f,
            10000000000ULL),
        10000000000ULL);

    Check(
        d.reason_code == "INVALID_FORECAST",
        "Probability > 1 rejected");

    d = controller.update(
        Forecast(
            -0.1f,
            0.2f,
            0.2f,
            11000000000ULL),
        11000000000ULL);

    Check(
        d.reason_code == "INVALID_FORECAST",
        "Probability < 0 rejected");

    // ---------------------------------------------------------
    // K. Timestamp validation
    // ---------------------------------------------------------
    std::cout << "\n[K] TIMESTAMP VALIDATION\n";

    controller.reset();

    d = controller.update(
        Forecast(0.1f, 0.1f, 0.1f, 0),
        12000000000ULL);

    Check(
        d.reason_code == "STALE_FORECAST",
        "Zero/old forecast timestamp rejected as stale");

    d = controller.update(
        Forecast(0.1f, 0.1f, 0.1f, 13000000000ULL),
        12000000000ULL);

    Check(
        d.reason_code == "INVALID_FORECAST",
        "Future forecast timestamp rejected");

    // Valid first forecast.
    d = controller.update(
        Forecast(0.1f, 0.1f, 0.1f, 14000000000ULL),
        14000000000ULL);

    Check(
        d.reason_code == "NORMAL_OPERATION",
        "Valid forecast accepted");

    // Duplicate timestamp.
    d = controller.update(
        Forecast(0.2f, 0.2f, 0.2f, 14000000000ULL),
        14000000000ULL);

    Check(
        d.reason_code == "STALE_FORECAST",
        "Duplicate forecast rejected");

    // Old forecast beyond stale window.
    d = controller.update(
        Forecast(0.2f, 0.2f, 0.2f, 14000000001ULL),
        17000000001ULL);

    Check(
        d.reason_code == "STALE_FORECAST",
        "Forecast older than stale window rejected");

    // ---------------------------------------------------------
    // L. Final report
    // ---------------------------------------------------------
    std::cout
        << "\n============================================================\n"
        << "                 CONTROLLER AUDIT RESULT\n"
        << "============================================================\n"
        << "PASS: " << passed << "\n"
        << "FAIL: " << failed << "\n";

    if (failed == 0)
    {
        std::cout
            << "\nRESULT: CONTROLLER POLICY AUDIT PASSED\n"
            << "All deterministic controller invariants passed.\n"
            << "Next: validate live Llama actuation + telemetry response.\n";
    }
    else
    {
        std::cout
            << "\nRESULT: CONTROLLER POLICY AUDIT FAILED\n"
            << "Do NOT modify thresholds blindly; inspect the failed case(s).\n";
    }

    std::cout
        << "============================================================\n";

    return failed == 0 ? 0 : 1;
}
