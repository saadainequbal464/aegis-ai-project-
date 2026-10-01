#include <iostream>
#include <vector>
#include <string>
#include <cstring>

#include <onnxruntime_cxx_api.h>

int main() {
    try {
        Ort::Env env(
            ORT_LOGGING_LEVEL_VERBOSE,
            "AEGIS_QNN_TEST"
        );

        const char* qnn_ep_name =
            "QNNExecutionProvider";

        env.RegisterExecutionProviderLibrary(
            qnn_ep_name,
            ORT_TSTR("qnn_test_runtime\\onnxruntime_providers_qnn.dll")
        );

        std::cout << "QNN EP plugin registration: OK\n";

        auto ep_devices = env.GetEpDevices();

        bool found_qnn = false;

        for (const auto& device : ep_devices) {
            std::cout
                << "EP device: "
                << device.EpName()
                << "\n";

            if (std::strcmp(
                    device.EpName(),
                    qnn_ep_name) == 0) {
                found_qnn = true;
            }
        }

        if (!found_qnn) {
            std::cerr
                << "ERROR: QNNExecutionProvider device not found\n";

            env.UnregisterExecutionProviderLibrary(
                qnn_ep_name
            );

            return 1;
        }

        std::cout
            << "QNN EP device discovery: PASSED\n";

        std::vector<Ort::ConstEpDevice>
            selected_devices;

        for (const auto& device : ep_devices) {
            if (std::strcmp(
                    device.EpName(),
                    qnn_ep_name) == 0) {

                selected_devices.push_back(device);
                break;
            }
        }

        Ort::SessionOptions session_options;

        // IMPORTANT:
        // Do not silently fall back to CPU.
        session_options.AddConfigEntry(
            "session.disable_cpu_ep_fallback",
            "1"
        );

        Ort::KeyValuePairs ep_options;

        // QNN HTP backend = Qualcomm NPU backend.
        ep_options.Append(
            "backend_type",
            "htp"
        );

        session_options.AppendExecutionProvider_V2(
            env,
            selected_devices,
            ep_options
        );

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
            << "NOTE: This Intel x64 machine cannot prove "
               "physical Snapdragon Hexagon NPU execution.\n";

        env.UnregisterExecutionProviderLibrary(
            qnn_ep_name
        );

        return 0;
    }
    catch (const Ort::Exception& e) {
        std::cerr
            << "\nQNN/ONNX Runtime ERROR:\n"
            << e.what()
            << "\n";

        return 1;
    }
    catch (const std::exception& e) {
        std::cerr
            << "\nERROR:\n"
            << e.what()
            << "\n";

        return 1;
    }
}