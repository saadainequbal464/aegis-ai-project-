#include <iostream>
#include "risk_fusion.h"

int main()
{
    aegis::RiskFusion fusion;

    aegis::RiskFusionInput input{};

    input.lgbm_p_exceed = {
        0.10f,
        0.20f,
        0.80f
    };

    input.tcn_p_exceed = {
        0.70f,
        0.60f,
        0.40f
    };

    input.tcn_valid = true;

    const auto output = fusion.Fuse(input);

    std::cout << "valid=" << output.valid << "\n";
    std::cout << "used_tcn=" << output.used_tcn << "\n";

    for (float p : output.fused_p_exceed)
        std::cout << p << "\n";

    return output.valid ? 0 : 1;
}