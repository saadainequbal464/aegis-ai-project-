#pragma once

#include "aegis_controller.h"

#include <functional>

namespace aegis
{

struct ActionControlConfig
{
    double minimum_intensity = 0.0;
    double maximum_intensity = 1.0;
};

struct ActionCommand
{
    LLMAction action = LLMAction::CONTINUE;
    // Existing ExperimentLlmLevel mapping:
    // 0 = LOW, 1 = MEDIUM.
    int llm_level = 1;
    double intensity = 0.0;
};

struct ActionControlResult
{
    ActionCommand command{};
    bool valid = false;
    bool applied = false;
};

using LlmWorkloadActuator =
    std::function<bool(int llm_level, double intensity)>;

class ActionControl
{
public:
    explicit ActionControl(
        LlmWorkloadActuator actuator,
        const ActionControlConfig& config = {}
    );

    ActionControlResult apply(
        const ControlDecision& decision,
        double normal_llm_intensity
    );

    const ActionCommand& last_command() const;

private:
    ActionControlConfig config_{};
    LlmWorkloadActuator actuator_{};
    ActionCommand last_command_{};

    double clamp_intensity(double value) const;

    ActionCommand make_command(
        LLMAction action,
        double normal_llm_intensity,
        double yield_intensity
    ) const;
};

}  // namespace aegis
