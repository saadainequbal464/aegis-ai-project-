#pragma once

#include <array>
#include <cstddef>

namespace aegis {

constexpr std::size_t kRiskHorizons = 3;

enum class RiskHorizonIndex : std::size_t {
    H250 = 0,
    H750 = 1,
    H2000 = 2
};

struct RiskFusionInput {
    std::array<float, kRiskHorizons> lgbm_p_exceed{};
    std::array<float, kRiskHorizons> tcn_p_exceed{};

    // TCN is an asynchronous/best-effort second tier.
    bool tcn_valid = false;
};

struct RiskFusionOutput {
    std::array<float, kRiskHorizons> fused_p_exceed{};
    bool valid = false;

    // True when the output used both models.
    bool used_tcn = false;
};

class RiskFusion {
public:
    RiskFusion();

    bool SetWeights(
        const std::array<float, kRiskHorizons>& lgbm_weights);

    const std::array<float, kRiskHorizons>& weights() const;

    RiskFusionOutput Fuse(
        const RiskFusionInput& input) const;

private:
    std::array<float, kRiskHorizons> lgbm_weights_{};
};

}  // namespace aegis