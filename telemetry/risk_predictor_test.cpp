#include "risk_predictor.h"
#include "unified_telemetry.h"

#include <fstream>
#include <iostream>
#include <sstream>

int main()
{
    aegis::RiskPredictor predictor;

    if (!predictor.load_models(
        "..\\models\\event_risk_lightgbm\\risk_250ms.txt",
        "..\\models\\event_risk_lightgbm\\risk_750ms.txt",
        "..\\models\\event_risk_lightgbm\\risk_2000ms.txt"))
    {
        std::cout << "LOAD FAILED: "
                  << predictor.last_error() << "\n";
        return 1;
    }

    std::ifstream file("risk_input.csv");

    if (!file)
    {
        std::cout << "INPUT FILE FAILED\n";
        return 1;
    }

    std::string line;

    std::getline(file, line);
    std::getline(file, line);

    std::stringstream ss(line);
    std::string cell;

    FeatureVector features;

    while (std::getline(ss, cell, ','))
        features.features.push_back(std::stof(cell));

    std::cout << "Features: "
              << features.features.size() << "\n";

    if (features.features.size() != 40)
    {
        std::cout << "WRONG FEATURE COUNT\n";
        return 1;
    }

    const auto result =
        predictor.predict(features, 0);

    if (!result.valid)
    {
        std::cout << "PREDICT FAILED: "
                  << result.error << "\n";
        return 1;
    }

    std::cout << "250ms  = " << result.p_exceed[0] << "\n";
    std::cout << "750ms  = " << result.p_exceed[1] << "\n";
    std::cout << "2000ms = " << result.p_exceed[2] << "\n";
    std::cout << "PASS\n";

    return 0;
}