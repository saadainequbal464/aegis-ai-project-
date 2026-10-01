#include "risk_fusion.h"

#include <algorithm>
#include <cmath>

namespace aegis {

namespace {

bool ValidProbability(float x)
{
    return std::isfinite(x) &&
           x >= 0.0f &&
           x <= 1.0f;
}

float ClampProbability(float x)
{
    return std::clamp(x, 0.0f, 1.0f);
}

}  // namespace

RiskFusion::RiskFusion()
    : lgbm_weights_{
          0.0f,   // 250 ms: TCN only
          0.0f,   // 750 ms: TCN only
          0.79f   // 2000 ms: 79% LGBM + 21% TCN
      }
{
}

bool RiskFusion::SetWeights(
    const std::array<float, kRiskHorizons>& lgbm_weights)
{
    for (float w : lgbm_weights)
    {
        if (!std::isfinite(w) ||
            w < 0.0f ||
            w > 1.0f)
        {
            return false;
        }
    }

    lgbm_weights_ = lgbm_weights;
    return true;
}

const std::array<float, kRiskHorizons>&
RiskFusion::weights() const
{
    return lgbm_weights_;
}

RiskFusionOutput RiskFusion::Fuse(
    const RiskFusionInput& input) const
{
    RiskFusionOutput output{};

    // LightGBM must always be valid because it is Tier-1.
    for (std::size_t h = 0;
         h < kRiskHorizons;
         ++h)
    {
        if (!ValidProbability(
                input.lgbm_p_exceed[h]))
        {
            return output;
        }
    }

    // --------------------------------------------------------
    // Normal path: LGBM + TCN fusion
    // --------------------------------------------------------
    if (input.tcn_valid)
    {
        for (std::size_t h = 0;
             h < kRiskHorizons;
             ++h)
        {
            if (!ValidProbability(
                    input.tcn_p_exceed[h]))
            {
                return output;
            }

            const float w =
                lgbm_weights_[h];

            output.fused_p_exceed[h] =
                ClampProbability(
                    w * input.lgbm_p_exceed[h] +
                    (1.0f - w) *
                        input.tcn_p_exceed[h]
                );
        }

        output.valid = true;
        output.used_tcn = true;
        return output;
    }

    // --------------------------------------------------------
    // TCN unavailable:
    // Tier-1 LightGBM remains the safe hot-path output.
    // --------------------------------------------------------
    output.fused_p_exceed =
        input.lgbm_p_exceed;

    output.valid = true;
    output.used_tcn = false;

    return output;
}

}  // namespace aegis