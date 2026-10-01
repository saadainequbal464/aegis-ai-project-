// aegis_api.cpp
#include "aegis_api.h"
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <algorithm>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#pragma comment(lib, "ws2_32.lib")
namespace aegis::api {
namespace {
std::string JsonEscape(const std::string& value)
{
    std::string out;
    out.reserve(value.size() + 16);
    for (char c : value) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out += c;
            break;
        }
    }
    return out;
}
std::string BoolJson(bool value)
{
    return value ? "true" : "false";
}
std::string RuntimeStateJson(const RuntimeState& state)
{
    std::ostringstream json;
    json
        << "{"
        << "\"connected\":" << BoolJson(state.connected) << ","
        << "\"aegis_running\":" << BoolJson(state.aegis_running) << ","
        << "\"server_running\":" << BoolJson(state.server_running) << ","
        << "\"llm_started\":" << BoolJson(state.llm_started) << ","
        << "\"llm_level_id\":" << state.llm_level_id << ","
        << "\"llm_level\":\"" << JsonEscape(state.llm_level) << "\","
        << "\"tokens_per_sec\":" << state.tokens_per_sec << ","
        << "\"frame_time_ms\":" << state.frame_time_ms << ","
        << "\"fps\":" << state.fps << ","
        << "\"risk_250ms\":" << state.risk_250ms << ","
        << "\"risk_750ms\":" << state.risk_750ms << ","
        << "\"risk_2000ms\":" << state.risk_2000ms << ","
        << "\"controller_action\":\"" << JsonEscape(state.controller_action) << "\","
        << "\"controller_reason\":\"" << JsonEscape(state.controller_reason) << "\""
        << "}";
    return json.str();
}
void SendResponse(
    SOCKET client,
    int statusCode,
    const char* statusText,
    const std::string& body)
{
    std::ostringstream response;
    response
        << "HTTP/1.1 " << statusCode << " " << statusText << "\r\n"
        << "Content-Type: application/json; charset=utf-8\r\n"
        << "Access-Control-Allow-Origin: *\r\n"
        << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        << "Access-Control-Allow-Headers: Content-Type\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n"
        << "\r\n"
        << body;
    const std::string data = response.str();
    send(
        client,
        data.c_str(),
        static_cast<int>(data.size()),
        0
    );
}
void SendJson(
    SOCKET client,
    int statusCode,
    const char* statusText,
    const std::string& body)
{
    SendResponse(
        client,
        statusCode,
        statusText,
        body
    );
}
void SendError(
    SOCKET client,
    int statusCode,
    const char* statusText,
    const char* message)
{
    std::ostringstream json;
    json
        << "{"
        << "\"ok\":false,"
        << "\"error\":\""
        << JsonEscape(message)
        << "\""
        << "}";
    SendJson(
        client,
        statusCode,
        statusText,
        json.str()
    );
}
} // namespace
// ============================================================
// Implementation
// ============================================================
class AegisApi::Impl {
public:
    SOCKET listenSocket = INVALID_SOCKET;
    std::thread serverThread;
    RuntimeCallbacks callbacks;
    std::string host = "127.0.0.1";
    std::uint16_t port = 8787;
    std::atomic<bool> stopRequested{ false };
    bool winsockStarted = false;
};
// ============================================================
// Destructor
// ============================================================
AegisApi::~AegisApi()
{
    Stop();
}
// ============================================================
// Set runtime callbacks
// ============================================================
void AegisApi::SetCallbacks(RuntimeCallbacks callbacks)
{
    if (!impl_) {
        impl_ = new Impl();
    }
    impl_->callbacks = std::move(callbacks);
}
// ============================================================
// Handle one HTTP client
// ============================================================
static void HandleClient(
    SOCKET client,
    RuntimeCallbacks& callbacks)
{
    char buffer[8192];
    const int received = recv(
        client,
        buffer,
        static_cast<int>(sizeof(buffer) - 1),
        0
    );
    if (received <= 0) {
        closesocket(client);
        return;
    }
    buffer[received] = '\0';
    std::string request(buffer);
    // --------------------------------------------------------
    // Parse first HTTP request line
    // --------------------------------------------------------
    const std::size_t lineEnd = request.find("\r\n");
    if (lineEnd == std::string::npos) {
        SendError(
            client,
            400,
            "Bad Request",
            "Invalid HTTP request"
        );
        closesocket(client);
        return;
    }
    const std::string requestLine =
        request.substr(0, lineEnd);
    std::istringstream line(requestLine);
    std::string method;
    std::string path;
    std::string version;
    line >> method >> path >> version;
    if (method.empty() || path.empty()) {
        SendError(
            client,
            400,
            "Bad Request",
            "Invalid request line"
        );
        closesocket(client);
        return;
    }
    // --------------------------------------------------------
    // CORS preflight
    // --------------------------------------------------------
    if (method == "OPTIONS") {
        SendJson(
            client,
            204,
            "No Content",
            ""
        );
        closesocket(client);
        return;
    }
    // --------------------------------------------------------
    // GET /api/health
    // --------------------------------------------------------
    if (method == "GET" &&
        path == "/api/health") {
        SendJson(
            client,
            200,
            "OK",
            "{\"ok\":true,\"service\":\"aegis-api\"}"
        );
        closesocket(client);
        return;
    }
    // --------------------------------------------------------
    // GET /api/runtime/status
    // --------------------------------------------------------
    if (method == "GET" &&
        path == "/api/runtime/status") {
        if (!callbacks.get_state) {
            SendError(
                client,
                503,
                "Service Unavailable",
                "Runtime state callback is not configured"
            );
            closesocket(client);
            return;
        }
        try {
            RuntimeState state =
                callbacks.get_state();
            state.connected = true;
            SendJson(
                client,
                200,
                "OK",
                RuntimeStateJson(state)
            );
        }
        catch (...) {
            SendError(
                client,
                500,
                "Internal Server Error",
                "Failed to read runtime state"
            );
        }
        closesocket(client);
        return;
    }
    // --------------------------------------------------------
    // POST /api/runtime/start
    // --------------------------------------------------------
    if (method == "POST" &&
        path == "/api/runtime/start") {
        if (!callbacks.start_aegis) {
            SendError(
                client,
                503,
                "Service Unavailable",
                "AEGIS start callback is not configured"
            );
            closesocket(client);
            return;
        }
        try {
            const bool success =
                callbacks.start_aegis();
            if (success) {
                SendJson(
                    client,
                    200,
                    "OK",
                    "{\"ok\":true,\"action\":\"start_aegis\"}"
                );
            }
            else {
                SendError(
                    client,
                    409,
                    "Conflict",
                    "AEGIS runtime could not be started"
                );
            }
        }
        catch (...) {
            SendError(
                client,
                500,
                "Internal Server Error",
                "AEGIS start operation failed"
            );
        }
        closesocket(client);
        return;
    }
    // --------------------------------------------------------
    // POST /api/llm/increase
    // --------------------------------------------------------
    if (method == "POST" &&
        path == "/api/llm/increase") {
        if (!callbacks.increase_llm) {
            SendError(
                client,
                503,
                "Service Unavailable",
                "LLM increase callback is not configured"
            );
            closesocket(client);
            return;
        }
        try {
            const bool success =
                callbacks.increase_llm();
            if (success) {
                SendJson(
                    client,
                    200,
                    "OK",
                    "{\"ok\":true,\"action\":\"increase_llm\"}"
                );
            }
            else {
                SendError(
                    client,
                    409,
                    "Conflict",
                    "LLM level could not be increased"
                );
            }
        }
        catch (...) {
            SendError(
                client,
                500,
                "Internal Server Error",
                "LLM increase operation failed"
            );
        }
        closesocket(client);
        return;
    }
    // --------------------------------------------------------
    // POST /api/runtime/stop
    // --------------------------------------------------------
    if (method == "POST" &&
        path == "/api/runtime/stop") {
        if (!callbacks.stop_aegis) {
            SendError(
                client,
                503,
                "Service Unavailable",
                "AEGIS stop callback is not configured"
            );
            closesocket(client);
            return;
        }
        try {
            const bool success =
                callbacks.stop_aegis();
            if (success) {
                SendJson(
                    client,
                    200,
                    "OK",
                    "{\"ok\":true,\"action\":\"stop_aegis\"}"
                );
            }
            else {
                SendError(
                    client,
                    409,
                    "Conflict",
                    "AEGIS runtime could not be stopped"
                );
            }
        }
        catch (...) {
            SendError(
                client,
                500,
                "Internal Server Error",
                "AEGIS stop operation failed"
            );
        }
        closesocket(client);
        return;
    }
    // --------------------------------------------------------
    // Unknown endpoint
    // --------------------------------------------------------
    SendError(
        client,
        404,
        "Not Found",
        "Unknown API endpoint"
    );
    closesocket(client);
}
// ============================================================
// Start API server
// ============================================================
bool AegisApi::Start(
    const std::string& host,
    std::uint16_t port)
{
    if (running_) {
        return true;
    }
    if (!impl_) {
        impl_ = new Impl();
    }
    impl_->host = host;
    impl_->port = port;
    impl_->stopRequested = false;
    // --------------------------------------------------------
    // Start WinSock
    // --------------------------------------------------------
    WSADATA wsaData{};
    const int wsaResult =
        WSAStartup(
            MAKEWORD(2, 2),
            &wsaData
        );
    if (wsaResult != 0) {
        return false;
    }
    impl_->winsockStarted = true;
    // --------------------------------------------------------
    // Create TCP socket
    // --------------------------------------------------------
    impl_->listenSocket =
        socket(
            AF_INET,
            SOCK_STREAM,
            IPPROTO_TCP
        );
    if (impl_->listenSocket == INVALID_SOCKET) {
        WSACleanup();
        impl_->winsockStarted = false;
        return false;
    }
    // --------------------------------------------------------
    // Allow quick restart
    // --------------------------------------------------------
    BOOL reuse = TRUE;
    setsockopt(
        impl_->listenSocket,
        SOL_SOCKET,
        SO_REUSEADDR,
        reinterpret_cast<const char*>(&reuse),
        sizeof(reuse)
    );
    // --------------------------------------------------------
    // Bind
    // --------------------------------------------------------
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (inet_pton(
            AF_INET,
            host.c_str(),
            &address.sin_addr) != 1) {
        closesocket(impl_->listenSocket);
        impl_->listenSocket = INVALID_SOCKET;
        WSACleanup();
        impl_->winsockStarted = false;
        return false;
    }
    if (bind(
            impl_->listenSocket,
            reinterpret_cast<sockaddr*>(&address),
            sizeof(address)) == SOCKET_ERROR) {
        closesocket(impl_->listenSocket);
        impl_->listenSocket = INVALID_SOCKET;
        WSACleanup();
        impl_->winsockStarted = false;
        return false;
    }
    // --------------------------------------------------------
    // Listen
    // --------------------------------------------------------
    if (listen(
            impl_->listenSocket,
            SOMAXCONN) == SOCKET_ERROR) {
        closesocket(impl_->listenSocket);
        impl_->listenSocket = INVALID_SOCKET;
        WSACleanup();
        impl_->winsockStarted = false;
        return false;
    }
    running_ = true;
    // --------------------------------------------------------
    // Server thread
    // --------------------------------------------------------
    impl_->serverThread =
        std::thread(
            [this]() {
                while (
                    !impl_->stopRequested &&
                    running_) {
                    sockaddr_in clientAddress{};
                    int clientLength =
                        sizeof(clientAddress);
                    SOCKET client =
                        accept(
                            impl_->listenSocket,
                            reinterpret_cast<sockaddr*>(
                                &clientAddress),
                            &clientLength
                        );
                    if (client == INVALID_SOCKET) {
                        if (
                            impl_->stopRequested ||
                            !running_) {
                            break;
                        }
                        continue;
                    }
                    HandleClient(
                        client,
                        impl_->callbacks
                    );
                }
            }
        );
    return true;
}
// ============================================================
// Stop API server
// ============================================================
void AegisApi::Stop()
{
    if (!impl_) {
        return;
    }
    if (!running_) {
        return;
    }
    impl_->stopRequested = true;
    running_ = false;
    // Closing the listening socket releases accept().
    if (impl_->listenSocket != INVALID_SOCKET) {
        shutdown(
            impl_->listenSocket,
            SD_BOTH
        );
        closesocket(
            impl_->listenSocket
        );
        impl_->listenSocket =
            INVALID_SOCKET;
    }
    if (
        impl_->serverThread.joinable() &&
        impl_->serverThread.get_id() !=
            std::this_thread::get_id()) {
        impl_->serverThread.join();
    }
    if (impl_->winsockStarted) {
        WSACleanup();
        impl_->winsockStarted = false;
    }
}
// ============================================================
// Is running
// ============================================================
bool AegisApi::IsRunning() const
{
    return running_.load();
}
} // namespace aegis::api
