#include <iostream>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include <onnxruntime_cxx_api.h>

int main() {
    try {
        Ort::Env env(
            ORT_LOGGING_LEVEL_VERBOSE,
            "AEGIS_QNN_TEST"
        );

        // Load QNN Execution Provider plugin.
        env.RegisterExecutionProviderLibrary(
            "QNNExecutionProvider",
            ORT_TSTR(
                "third_party\\qnn_test_runtime\\onnxruntime_providers_qnn.dll"
            )
        );

        std::cout
            << "QNN EP plugin registration: OK\n";

        // Discover available EP devices.
        auto devices = env.GetEpDevices();

        bool found_qnn = false;

        std::cout
            << "Discovered EP devices:\n";

        for (const auto& device : devices) {
            std::cout
                << "  EP: "
                << device.EpName()
                << "\n";

            if (std::strcmp(
                    device.EpName(),
                    "QNNExecutionProvider") == 0) {
                found_qnn = true;
            }
        }

        if (!found_qnn) {
            std::cerr
                << "ERROR: QNNExecutionProvider device not found\n";

            return 1;
        }

        std::cout
            << "QNN EP device discovery: PASSED\n";

        // Select the QNN device.
        std::vector<Ort::ConstEpDevice> selected_devices;

        for (const auto& device : devices) {
            if (std::strcmp(
                    device.EpName(),
                    "QNNExecutionProvider") == 0) {

                selected_devices.push_back(device);
                break;
            }
        }

        if (selected_devices.empty()) {
            std::cerr
                << "ERROR: Could not select QNN EP device\n";

            return 1;
        }

        // Session configuration.
        Ort::SessionOptions session_options;

        // Do NOT silently fall back to CPU.
        session_options.AddConfigEntry(
            "session.disable_cpu_ep_fallback",
            "1"
        );

        // QNN backend configuration.
        Ort::KeyValuePairs qnn_options(
            std::unordered_map<std::string, std::string>{
                {"backend_type", "htp"}
            }
        );

        // Attach QNN EP.
        session_options.AppendExecutionProvider_V2(
            env,
            selected_devices,
            qnn_options
        );

        std::cout
            << "QNN HTP EP configured: OK\n";

        // Load AEGIS TCN ONNX model.
        const wchar_t* model_path =
            L"models\\onnx\\tcn_event_risk.onnx";

        Ort::Session session(
            env,
            model_path,
            session_options
        );

        std::cout
            << "QNN HTP session creation: PASSED\n";

        std::cout
            << "AEGIS TCN QNN test completed.\n";

        return 0;
    }
    catch (const Ort::Exception& e) {
        std::cerr
            << "\nONNX Runtime / QNN ERROR:\n"
            << e.what()
            << "\n";

        return 1;
    }
    catch (const std::exception& e) {
        std::cerr
            << "\nSTANDARD C++ ERROR:\n"
            << e.what()
            << "\n";

        return 1;
    }
}