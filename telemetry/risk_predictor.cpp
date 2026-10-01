#include "risk_predictor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <memory>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "onnxruntime_cxx_api.h"

namespace aegis
{

namespace
{

// ============================================================
// EXACT C++ RUNTIME FEATURE SCHEMA
// ============================================================
//
// FeatureVector produced by ComputeFeatureVector() contains:
//
//  0-3   CPU
//  4-6   Memory
//  7-14  GPU
// 15-17  NPU
// 18-24  DRAM
// 25-31  Foreground QoS
// 32-34  Token rate
// 35-37  Token latency
// 38-39  TTFT
//
// The event-risk LightGBM models were trained on exactly
// these 40 features in this exact order.
//
// ============================================================

constexpr std::size_t RUNTIME_FEATURE_COUNT = 40;

const char* const kFeatureNames[RUNTIME_FEATURE_COUNT] =
{
    "cpu_mean",
    "cpu_max",
    "cpu_std",
    "cpu_trend",

    "memory_mean",
    "memory_max",
    "memory_trend",

    "gpu_mean",
    "gpu_max",
    "gpu_trend",
    "gpu_memory_ratio_mean",
    "gpu_memory_ratio_max",
    "gpu_memory_ratio_trend",
    "gpu_temperature_mean",
    "gpu_temperature_max",

    "npu_mean",
    "npu_max",
    "npu_trend",

    "dram_read_mean",
    "dram_read_max",
    "dram_write_mean",
    "dram_write_max",
    "dram_util_mean",
    "dram_util_max",
    "dram_util_trend",

    "frame_time_mean",
    "frame_time_max",
    "frame_time_std",
    "frame_time_trend",

    "fps_mean",
    "fps_min",
    "fps_trend",

    "token_rate_mean",
    "token_rate_min",
    "token_rate_trend",

    "token_latency_mean",
    "token_latency_max",
    "token_latency_trend",

    "ttft_mean",
    "ttft_max"
};


// ============================================================
// STRING HELPERS
// ============================================================

std::string trim(const std::string& value)
{
    const std::size_t first =
        value.find_first_not_of(" \t\r\n");

    if (first == std::string::npos)
        return {};

    const std::size_t last =
        value.find_last_not_of(" \t\r\n");

    return value.substr(
        first,
        last - first + 1
    );
}


bool starts_with(
    const std::string& value,
    const char* prefix)
{
    const std::size_t n =
        std::char_traits<char>::length(prefix);

    return value.size() >= n &&
           value.compare(0, n, prefix) == 0;
}


std::vector<std::string> split_whitespace(
    const std::string& value)
{
    std::vector<std::string> result;

    std::stringstream ss(value);
    std::string item;

    while (ss >> item)
        result.push_back(item);

    return result;
}


std::vector<int> parse_int_list(
    const std::string& value)
{
    std::vector<int> result;

    for (const std::string& item :
         split_whitespace(value))
    {
        result.push_back(
            std::stoi(item)
        );
    }

    return result;
}


std::vector<float> parse_float_list(
    const std::string& value)
{
    std::vector<float> result;

    for (const std::string& item :
         split_whitespace(value))
    {
        result.push_back(
            std::stof(item)
        );
    }

    return result;
}


std::string value_after_equals(
    const std::string& line)
{
    const std::size_t pos =
        line.find('=');

    if (pos == std::string::npos)
        return {};

    return trim(
        line.substr(pos + 1)
    );
}


// ============================================================
// SIGMOID
// ============================================================

float sigmoid(float x)
{
    if (x >= 0.0f)
    {
        const float z =
            std::exp(-x);

        return 1.0f / (1.0f + z);
    }

    const float z =
        std::exp(x);

    return z / (1.0f + z);
}

} // namespace


// ============================================================
// INTERNAL MODEL REPRESENTATION
// ============================================================

struct RiskPredictor::Model
{
    struct Tree
    {
        std::vector<int> split_feature;
        std::vector<float> threshold;

        std::vector<int> left_child;
        std::vector<int> right_child;

        std::vector<float> leaf_value;

        float shrinkage = 1.0f;
    };

    std::vector<std::string> feature_names;
    std::vector<Tree> trees;

    bool loaded = false;
};

// ============================================================
// TCN ONNX RUNTIME
// ============================================================

struct RiskPredictor::TcnRuntime
{
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "AEGIS_TCN"};
    std::unique_ptr<Ort::Session> session;
    Ort::MemoryInfo memory_info =
        Ort::MemoryInfo::CreateCpu(
            OrtArenaAllocator,
            OrtMemTypeDefault);

    std::string input_name;
    std::string output_name;

    bool loaded = false;
};


// ============================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================

RiskPredictor::RiskPredictor() = default;

RiskPredictor::~RiskPredictor() = default;


// ============================================================
// LOAD MODEL
// ============================================================

bool RiskPredictor::load_model(
    Model& model,
    const std::string& path)
{
    std::ifstream file(path);

    if (!file.is_open())
    {
        last_error_ =
            "Unable to open model: " + path;

        return false;
    }

    model = Model{};

    std::string line;

    Model::Tree* current_tree = nullptr;

    while (std::getline(file, line))
    {
        line = trim(line);

        if (line.empty())
            continue;


        // ----------------------------------------------------
        // FEATURE NAMES
        // ----------------------------------------------------

        if (starts_with(line, "feature_names="))
        {
            const std::string value =
                value_after_equals(line);

            model.feature_names =
                split_whitespace(value);

            continue;
        }


        // ----------------------------------------------------
        // NEW TREE
        // ----------------------------------------------------

        if (starts_with(line, "Tree="))
        {
            model.trees.emplace_back();

            current_tree =
                &model.trees.back();

            continue;
        }


        if (current_tree == nullptr)
            continue;


        // ----------------------------------------------------
        // TREE ARRAYS
        // ----------------------------------------------------

        if (starts_with(line, "split_feature="))
        {
            current_tree->split_feature =
                parse_int_list(
                    value_after_equals(line)
                );

            continue;
        }


        if (starts_with(line, "threshold="))
        {
            current_tree->threshold =
                parse_float_list(
                    value_after_equals(line)
                );

            continue;
        }


        if (starts_with(line, "left_child="))
        {
            current_tree->left_child =
                parse_int_list(
                    value_after_equals(line)
                );

            continue;
        }


        if (starts_with(line, "right_child="))
        {
            current_tree->right_child =
                parse_int_list(
                    value_after_equals(line)
                );

            continue;
        }


        if (starts_with(line, "leaf_value="))
        {
            current_tree->leaf_value =
                parse_float_list(
                    value_after_equals(line)
                );

            continue;
        }


        if (starts_with(line, "shrinkage="))
        {
            current_tree->shrinkage =
                std::stof(
                    value_after_equals(line)
                );

            continue;
        }
    }


    // ========================================================
    // VERIFY FEATURE SCHEMA
    // ========================================================

    if (model.feature_names.size() !=
        RUNTIME_FEATURE_COUNT)
    {
        last_error_ =
            "Model feature count is " +
            std::to_string(
                model.feature_names.size()
            ) +
            ", expected " +
            std::to_string(
                RUNTIME_FEATURE_COUNT
            );

        return false;
    }


    for (std::size_t i = 0;
         i < RUNTIME_FEATURE_COUNT;
         ++i)
    {
        if (model.feature_names[i] !=
            kFeatureNames[i])
        {
            last_error_ =
                "Model feature mismatch at index " +
                std::to_string(i) +
                ": expected '" +
                kFeatureNames[i] +
                "', got '" +
                model.feature_names[i] +
                "'";

            return false;
        }
    }


    // ========================================================
    // VERIFY TREES
    // ========================================================

    if (model.trees.empty())
    {
        last_error_ =
            "Model contains no trees: " + path;

        return false;
    }


    for (std::size_t tree_index = 0;
         tree_index < model.trees.size();
         ++tree_index)
    {
        const Model::Tree& tree =
            model.trees[tree_index];

        const std::size_t internal_nodes =
            tree.split_feature.size();

        if (tree.threshold.size() !=
            internal_nodes ||
            tree.left_child.size() !=
            internal_nodes ||
            tree.right_child.size() !=
            internal_nodes)
        {
            last_error_ =
                "Malformed tree " +
                std::to_string(tree_index) +
                " in model: " +
                path;

            return false;
        }


        if (tree.leaf_value.empty())
        {
            last_error_ =
                "Tree " +
                std::to_string(tree_index) +
                " has no leaf values";

            return false;
        }


        for (int feature_index :
             tree.split_feature)
        {
            if (feature_index < 0 ||
                feature_index >=
                    static_cast<int>(
                        RUNTIME_FEATURE_COUNT))
            {
                last_error_ =
                    "Invalid feature index " +
                    std::to_string(feature_index) +
                    " in tree " +
                    std::to_string(tree_index);

                return false;
            }
        }
    }


    model.loaded = true;

    return true;
}


// ============================================================
// LOAD THREE HORIZON MODELS + TCN
// ============================================================

bool RiskPredictor::load_models(
    const std::string& model_250ms,
    const std::string& model_750ms,
    const std::string& model_2000ms,
    const std::string& tcn_onnx_path)
{
    models_loaded_ = false;
    last_error_.clear();
    tcn_history_.clear();
    tcn_.reset();

    for (auto& model : models_)
        model.reset();

    const std::string paths[NUM_RISK_HORIZONS] =
    {
        model_250ms,
        model_750ms,
        model_2000ms
    };

    for (int i = 0; i < NUM_RISK_HORIZONS; ++i)
    {
        models_[i] = std::make_unique<Model>();

        if (!load_model(*models_[i], paths[i]))
        {
            models_[i].reset();
            models_loaded_ = false;
            return false;
        }
    }

    models_loaded_ = true;

    // main.cpp intentionally passes only the three LightGBM paths.
    // Use the project-relative ONNX path automatically in that case.
    std::string resolved_tcn_path = tcn_onnx_path;

    if (resolved_tcn_path.empty())
        resolved_tcn_path = "..\\models\\onnx\\tcn_event_risk.onnx";

    if (!load_tcn(resolved_tcn_path))
    {
        // TCN is an enhancement path. LightGBM remains usable.
        // Do not fail the whole predictor just because ONNX Runtime
        // or the TCN artifact is unavailable.
        tcn_.reset();
        tcn_history_.clear();
    }

    return true;
}

// ============================================================
// LOAD TCN ONNX MODEL
// ============================================================

bool RiskPredictor::load_tcn(const std::string& path)
{
    try
    {
        auto runtime = std::make_unique<TcnRuntime>();

        Ort::SessionOptions session_options;
        session_options.SetIntraOpNumThreads(1);
        session_options.SetInterOpNumThreads(1);
        session_options.SetGraphOptimizationLevel(
            GraphOptimizationLevel::ORT_ENABLE_ALL);

#ifdef _WIN32
        std::wstring wide_path(path.begin(), path.end());
        runtime->session =
            std::make_unique<Ort::Session>(
                runtime->env,
                wide_path.c_str(),
                session_options);
#else
        runtime->session =
            std::make_unique<Ort::Session>(
                runtime->env,
                path.c_str(),
                session_options);
#endif

        Ort::AllocatorWithDefaultOptions allocator;

        auto input_name =
            runtime->session->GetInputNameAllocated(
                0,
                allocator);

        auto output_name =
            runtime->session->GetOutputNameAllocated(
                0,
                allocator);

        if (!input_name || !output_name)
        {
            last_error_ =
                "TCN ONNX model has invalid input/output names: " +
                path;
            return false;
        }

        runtime->input_name = input_name.get();
        runtime->output_name = output_name.get();

        // The exported TCN contract is:
        // input  = [batch=1, sequence=20, features=40]
        // output = [batch=1, horizons=3]
        auto input_type_info =
            runtime->session->GetInputTypeInfo(0);

        auto input_tensor_info =
            input_type_info.GetTensorTypeAndShapeInfo();

        const std::vector<int64_t> input_shape =
            input_tensor_info.GetShape();

        if (input_shape.size() != 3 ||
            input_shape[1] !=
                static_cast<int64_t>(TCN_SEQUENCE_LENGTH) ||
            input_shape[2] !=
                static_cast<int64_t>(RUNTIME_FEATURE_COUNT))
        {
            last_error_ =
                "Unexpected TCN input shape; expected [1,20,40]";
            return false;
        }

        auto output_type_info =
            runtime->session->GetOutputTypeInfo(0);

        auto output_tensor_info =
            output_type_info.GetTensorTypeAndShapeInfo();

        const std::vector<int64_t> output_shape =
            output_tensor_info.GetShape();

        if (output_shape.size() != 2 ||
            output_shape[1] !=
                static_cast<int64_t>(NUM_RISK_HORIZONS))
        {
            last_error_ =
                "Unexpected TCN output shape; expected [1,3]";
            return false;
        }

        runtime->loaded = true;
        tcn_ = std::move(runtime);
        return true;
    }
        catch (const Ort::Exception&)
    {
        return false;
    }
    catch (const std::exception& ex)
    {
        last_error_ =
            std::string("Failed to load TCN ONNX model: ") +
            ex.what();
        return false;
    }
}

// ============================================================
// TCN HISTORY
// ============================================================

void RiskPredictor::update_tcn_history(
    const FeatureVector& features) const
{
    if (features.features.size() < RUNTIME_FEATURE_COUNT)
        return;

    std::array<float, RUNTIME_FEATURE_COUNT> row{};

    for (std::size_t i = 0;
         i < RUNTIME_FEATURE_COUNT;
         ++i)
    {
        const float value = features.features[i];

        row[i] =
            std::isfinite(value)
                ? value
                : 0.0f;
    }

    tcn_history_.push_back(row);

    while (tcn_history_.size() > TCN_SEQUENCE_LENGTH)
        tcn_history_.pop_front();
}

// ============================================================
// TCN INFERENCE
// ============================================================

bool RiskPredictor::predict_tcn(
    const FeatureVector& features,
    std::array<float, NUM_RISK_HORIZONS>& output) const
{
    output.fill(0.0f);

    if (!tcn_ || !tcn_->loaded || !tcn_->session)
        return false;

    update_tcn_history(features);

    if (tcn_history_.size() != TCN_SEQUENCE_LENGTH)
        return false;

    std::vector<float> input_data;
    input_data.reserve(
        TCN_SEQUENCE_LENGTH * RUNTIME_FEATURE_COUNT);

    for (const auto& row : tcn_history_)
    {
        input_data.insert(
            input_data.end(),
            row.begin(),
            row.end());
    }

    const std::array<int64_t, 3> input_shape =
    {
        1,
        static_cast<int64_t>(TCN_SEQUENCE_LENGTH),
        static_cast<int64_t>(RUNTIME_FEATURE_COUNT)
    };

    try
    {
        Ort::Value input_tensor =
            Ort::Value::CreateTensor<float>(
                tcn_->memory_info,
                input_data.data(),
                input_data.size(),
                input_shape.data(),
                input_shape.size());

        const char* input_names[] =
        {
            tcn_->input_name.c_str()
        };

        const char* output_names[] =
        {
            tcn_->output_name.c_str()
        };

        auto outputs =
            tcn_->session->Run(
                Ort::RunOptions{nullptr},
                input_names,
                &input_tensor,
                1,
                output_names,
                1);

        if (outputs.empty() ||
            !outputs[0].IsTensor())
        {
            return false;
        }

        auto tensor_info =
            outputs[0].GetTensorTypeAndShapeInfo();

        const std::vector<int64_t> shape =
            tensor_info.GetShape();

        if (shape.size() != 2 ||
            shape[0] != 1 ||
            shape[1] != NUM_RISK_HORIZONS)
        {
            return false;
        }

        const float* logits =
            outputs[0].GetTensorData<float>();

        if (logits == nullptr)
            return false;

        for (int h = 0;
             h < NUM_RISK_HORIZONS;
             ++h)
        {
            const float p = sigmoid(logits[h]);

            if (!std::isfinite(p) ||
                p < 0.0f ||
                p > 1.0f)
            {
                return false;
            }

            output[h] = p;
        }

        return true;
    }
    catch (const Ort::Exception&)
    {
        return false;
    }
}

// // ============================================================
// TREE PREDICTION
// ============================================================

float RiskPredictor::predict_model(
    const Model& model,
    const FeatureVector& features) const
{
    if (!model.loaded)
        return 0.5f;


    if (features.features.size() <
        RUNTIME_FEATURE_COUNT)
    {
        return 0.5f;
    }


    float score = 0.0f;


    // ========================================================
    // TRAVERSE EACH TREE
    // ========================================================

    for (const Model::Tree& tree :
         model.trees)
    {
        if (tree.split_feature.empty())
        {
            if (tree.leaf_value.size() == 1)
            {
                score +=
                    tree.leaf_value[0];
            }

            continue;
        }


        int node = 0;


        // ----------------------------------------------------
        // Traverse internal nodes.
        //
        // LightGBM uses negative child indices for leaves:
        //
        //   -1 -> leaf 0
        //   -2 -> leaf 1
        //   -3 -> leaf 2
        //
        // Non-negative indices refer to internal nodes.
        // ----------------------------------------------------

        while (node >= 0)
        {
            const int feature_index =
                tree.split_feature[
                    static_cast<std::size_t>(node)
                ];


            const float feature_value =
                features.features[
                    static_cast<std::size_t>(
                        feature_index)
                ];


            const float threshold =
                tree.threshold[
                    static_cast<std::size_t>(node)
                ];


            // C++ FeatureEngine converts non-finite
            // telemetry values to zero, so normally this
            // branch is not reached.
            //
            // For safety, route NaN to the left branch.
            const bool go_left =
                !std::isfinite(feature_value) ||
                feature_value <= threshold;


            node =
                go_left
                    ? tree.left_child[
                        static_cast<std::size_t>(node)]
                    : tree.right_child[
                        static_cast<std::size_t>(node)];
        }


        // ----------------------------------------------------
        // Negative child index -> leaf index
        // ----------------------------------------------------

        const int leaf_index =
            -node - 1;


        if (leaf_index < 0 ||
            leaf_index >=
                static_cast<int>(
                    tree.leaf_value.size()))
        {
            continue;
        }


        score +=
            tree.leaf_value[
                static_cast<std::size_t>(
                    leaf_index)
            ];
    }


    // ========================================================
    // BINARY LIGHTGBM OBJECTIVE
    // ========================================================

    return sigmoid(score);
}


// ============================================================
// PUBLIC PREDICTION
// ============================================================

RiskPrediction RiskPredictor::predict(
    const FeatureVector& features,
    uint64_t timestamp_ns) const
{
    RiskPrediction result;

    result.timestamp_ns = timestamp_ns;

    if (!models_loaded_)
    {
        result.valid = false;
        result.error =
            last_error_.empty()
                ? "Risk models are not loaded"
                : last_error_;
        return result;
    }

    if (features.features.size() < RUNTIME_FEATURE_COUNT)
    {
        result.valid = false;
        result.error =
            "FeatureVector contains " +
            std::to_string(features.features.size()) +
            " features; expected at least " +
            std::to_string(RUNTIME_FEATURE_COUNT);
        return result;
    }

    // ------------------------------------------------------------
    // 1. LightGBM predictions
    // ------------------------------------------------------------

    RiskFusionInput fusion_input{};

    for (int i = 0; i < NUM_RISK_HORIZONS; ++i)
    {
        if (!models_[i] || !models_[i]->loaded)
        {
            result.valid = false;
            result.error =
                "Risk model " +
                std::to_string(i) +
                " is not loaded";
            return result;
        }

        fusion_input.lgbm_p_exceed[i] =
            predict_model(
                *models_[i],
                features);
    }

    // ------------------------------------------------------------
    // 2. TCN prediction
    //
    // The first 19 calls intentionally use LightGBM fallback
    // because the TCN requires exactly 20 sequential feature rows.
    // ------------------------------------------------------------

    std::array<float, NUM_RISK_HORIZONS> tcn_output{};

    const bool tcn_valid =
        predict_tcn(
            features,
            tcn_output);

    if (tcn_valid)
    {
        for (int i = 0; i < NUM_RISK_HORIZONS; ++i)
            fusion_input.tcn_p_exceed[i] = tcn_output[i];

        // --------------------------------------------------------
        // 3. Risk Fusion
        //
        // Current validated fusion configuration:
        //   250ms  = 0% LGBM + 100% TCN
        //   750ms  = 0% LGBM + 100% TCN
        //   2000ms = 79% LGBM + 21% TCN
        // --------------------------------------------------------

        RiskFusion fusion;

        const bool weights_ok =
            fusion.SetWeights(
                std::array<float, kRiskHorizons>{
                    0.00f,
                    0.00f,
                    0.79f});

        if (!weights_ok)
        {
            result.valid = false;
            result.error =
                "Failed to configure risk-fusion weights";
            return result;
        }

        const RiskFusionOutput fused =
            fusion.Fuse(fusion_input);

        if (!fused.valid)
        {
            result.valid = false;
            result.error =
                "Risk fusion failed";
            return result;
        }

        for (int i = 0; i < NUM_RISK_HORIZONS; ++i)
            result.p_exceed[i] =
                fused.fused_p_exceed[i];

        result.tcn_used = true;
    }
    else
    {
        // --------------------------------------------------------
        // TCN unavailable / not warmed up:
        // use the already-loaded LightGBM predictions.
        // --------------------------------------------------------

        for (int i = 0; i < NUM_RISK_HORIZONS; ++i)
            result.p_exceed[i] =
                fusion_input.lgbm_p_exceed[i];

        result.tcn_used = false;
    }

    for (float p : result.p_exceed)
    {
        if (!std::isfinite(p) ||
            p < 0.0f ||
            p > 1.0f)
        {
            result.valid = false;
            result.error =
                "Risk prediction produced invalid probability";
            return result;
        }
    }

    result.valid = true;
    return result;
}

// ============================================================
// STATUS
// ============================================================

bool RiskPredictor::models_loaded() const
{
    return models_loaded_;
}


const std::string&
RiskPredictor::last_error() const
{
    return last_error_;
}

} // namespace aegis