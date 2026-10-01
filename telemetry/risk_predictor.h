#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>

#include "unified_telemetry.h"
#include "risk_fusion.h"

namespace aegis
{

constexpr int NUM_RISK_HORIZONS = 3;
constexpr std::size_t TCN_SEQUENCE_LENGTH = 20;

enum class RiskHorizon : uint8_t
{
    H250MS = 0,
    H750MS = 1,
    H2000MS = 2
};

struct RiskPrediction
{
    std::array<float, NUM_RISK_HORIZONS> p_exceed{};

    uint64_t timestamp_ns = 0;

    bool valid = false;

    // True when the TCN participated in fusion.
    bool tcn_used = false;

    std::string error;
};

class RiskPredictor
{
public:

    RiskPredictor();
    ~RiskPredictor();

    bool load_models(
        const std::string& model_250ms,
        const std::string& model_750ms,
        const std::string& model_2000ms,
        const std::string& tcn_onnx_path = ""
    );

    RiskPrediction predict(
        const FeatureVector& features,
        uint64_t timestamp_ns
    ) const;

    bool models_loaded() const;

    const std::string& last_error() const;

private:

    struct Model;

    std::array<
        std::unique_ptr<Model>,
        NUM_RISK_HORIZONS
    > models_{};

    bool models_loaded_ = false;

    std::string last_error_;

    // --------------------------------------------------------
    // TCN runtime
    // --------------------------------------------------------

    struct TcnRuntime;

    std::unique_ptr<TcnRuntime> tcn_;

    // Last 20 runtime feature vectors.
    mutable std::deque<
        std::array<float, 40>
    > tcn_history_;

    // --------------------------------------------------------
    // LightGBM
    // --------------------------------------------------------

    bool load_model(
        Model& model,
        const std::string& path
    );

    float predict_model(
        const Model& model,
        const FeatureVector& features
    ) const;

    // --------------------------------------------------------
    // TCN
    // --------------------------------------------------------

    bool load_tcn(
        const std::string& path
    );

    bool predict_tcn(
        const FeatureVector& features,
        std::array<float, NUM_RISK_HORIZONS>& output
    ) const;

    void update_tcn_history(
        const FeatureVector& features
    ) const;
};

} // namespace aegis