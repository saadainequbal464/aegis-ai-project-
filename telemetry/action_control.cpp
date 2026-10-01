#include "action_control.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace aegis
{

namespace
{
bool finite_value(double value)
{
    return std::isfinite(value);
}
}  // namespace

ActionControl::ActionControl(
    LlmWorkloadActuator actuator,
    const ActionControlConfig& config)
    : config_(config),
      actuator_(std::move(actuator))
{
    if (!finite_value(config_.minimum_intensity) ||
        !finite_value(config_.maximum_intensity) ||
        config_.minimum_intensity < 0.0 ||
        config_.maximum_intensity > 1.0 ||
        config_.minimum_intensity > config_.maximum_intensity)
    {
        config_.minimum_intensity = 0.0;
        config_.maximum_intensity = 1.0;
    }
}

double ActionControl::clamp_intensity(double value) const
{
    if (!finite_value(value))
        return config_.minimum_intensity;

    return std::clamp(
        value,
        config_.minimum_intensity,
        config_.maximum_intensity
    );
}

ActionCommand ActionControl::make_command(
    LLMAction action,
    double normal_llm_intensity,
    double yield_intensity) const
{
    ActionCommand command{};
    command.action = action;

    const double normal =
        clamp_intensity(normal_llm_intensity);

    switch (action)
    {
    case LLMAction::PAUSE:
        command.llm_level = 0;
        command.intensity = 0.0;
        break;

    case LLMAction::YIELD:
    {
        const double yield =
            std::clamp(
                finite_value(yield_intensity)
                    ? yield_intensity
                    : 0.0,
                0.0,
                1.0
            );

        command.llm_level = 0;
        command.intensity =
            clamp_intensity(normal * (1.0 - yield));
        break;
    }

    case LLMAction::RESUME:
    case LLMAction::CONTINUE:
    default:
        command.llm_level = 1;
        command.intensity = normal;
        break;
    }

    return command;
}

ActionControlResult ActionControl::apply(
    const ControlDecision& decision,
    double normal_llm_intensity)
{
    ActionControlResult result{};

    result.command =
        make_command(
            decision.action,
            normal_llm_intensity,
            decision.yield_intensity
        );

    result.valid = true;
    last_command_ = result.command;

    if (!actuator_)
        return result;

    result.applied =
        actuator_(
            result.command.llm_level,
            result.command.intensity
        );

    return result;
}

const ActionCommand&
ActionControl::last_command() const
{
    return last_command_;
}

}  // namespace aegis
