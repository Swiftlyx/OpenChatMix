#include <winsock2.h>
#include <ws2tcpip.h>

#include "gamesense_server.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <utility>

#include "log.h"

namespace {

constexpr size_t kMaxHeaderBytes = 16 * 1024;
constexpr size_t kMaxBodyBytes = 1024 * 1024;  // Image data for a whole screen is far smaller.
constexpr size_t kMaxQueued = 256;             // Older requests are dropped if the UI thread falls behind.
constexpr size_t kMaxClients = 32;

// Value of a header in the head of a request, matched without regard to case; empty when it is missing.
std::string HeaderValue(const std::string& head, const char* name) {
    const size_t length = strlen(name);
    for (size_t line = head.find("\r\n"); line != std::string::npos; line = head.find("\r\n", line + 2)) {
        const size_t start = line + 2;
        if (head.size() - start > length && head[start + length] == ':' &&
            _strnicmp(head.c_str() + start, name, length) == 0) {
            size_t begin = start + length + 1;
            const size_t end = std::min(head.find("\r\n", begin), head.size());
            while (begin < end && head[begin] == ' ') {
                ++begin;
            }
            return head.substr(begin, end - begin);
        }
    }
    return {};
}

bool SendAll(SOCKET socket, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        const int result = send(socket, data.data() + sent, static_cast<int>(data.size() - sent), 0);
        if (result <= 0) {
            return false;
        }
        sent += static_cast<size_t>(result);
    }
    return true;
}

std::string Response(int status, const char* reason, const std::string& body) {
    return "HTTP/1.1 " + std::to_string(status) + " " + reason +
           "\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
}

}  // namespace

GameSenseServer::~GameSenseServer() {
    Stop();
}

int GameSenseServer::Start(HWND notify, UINT message) {
    Stop();
    notify_ = notify;
    message_ = message;
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        Log("GameSense: Winsock failed");
        return 0;
    }
    winsock_ = true;
    const SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        Stop();
        return 0;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;  // A free port.
    int size = sizeof(address);
    if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || listen(listener, 8) != 0 ||
        getsockname(listener, reinterpret_cast<sockaddr*>(&address), &size) != 0) {
        Log("GameSense: listening failed, error %d", WSAGetLastError());
        closesocket(listener);
        Stop();
        return 0;
    }
    listener_ = listener;
    port_ = ntohs(address.sin_port);
    acceptor_ = std::thread(&GameSenseServer::AcceptLoop, this);
    Log("GameSense: listening on 127.0.0.1:%d", port_);
    return port_;
}

void GameSenseServer::Stop() {
    if (port_) {
        // Closing the listener ends accept(); shutting the connections down ends their recv().
        closesocket(static_cast<SOCKET>(listener_));
        if (acceptor_.joinable()) {
            acceptor_.join();
        }
        std::vector<Worker> workers;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const uintptr_t client : clients_) {
                shutdown(static_cast<SOCKET>(client), SD_BOTH);
            }
            workers = std::move(workers_);
            workers_.clear();
        }
        for (Worker& worker : workers) {
            worker.thread.join();
        }
        port_ = 0;
        listener_ = 0;
        Log("GameSense: stopped");
    }
    if (winsock_) {
        WSACleanup();
        winsock_ = false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    requests_.clear();
}

std::vector<GameSenseRequest> GameSenseServer::TakeRequests() {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::exchange(requests_, {});
}

void GameSenseServer::AcceptLoop() {
    for (;;) {
        const SOCKET client = accept(static_cast<SOCKET>(listener_), nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        // Threads of closed connections are joined here, so they do not pile up.
        std::erase_if(workers_, [](Worker& worker) {
            if (!worker.done->load()) {
                return false;
            }
            worker.thread.join();
            return true;
        });
        if (clients_.size() >= kMaxClients) {
            closesocket(client);
            continue;
        }
        clients_.push_back(client);
        auto done = std::make_shared<std::atomic<bool>>(false);
        workers_.push_back({std::thread(&GameSenseServer::Serve, this, static_cast<uintptr_t>(client), done), done});
    }
}

void GameSenseServer::Serve(uintptr_t handle, const std::shared_ptr<std::atomic<bool>>& done) {
    const SOCKET client = static_cast<SOCKET>(handle);
    std::string buffer;
    char chunk[4096];
    auto receive = [&]() {
        const int received = recv(client, chunk, sizeof(chunk), 0);
        if (received <= 0) {
            return false;
        }
        buffer.append(chunk, static_cast<size_t>(received));
        return true;
    };
    // One request after another on the same connection, as HTTP/1.1 clients keep it open.
    for (bool open = true; open;) {
        size_t headEnd;
        while ((headEnd = buffer.find("\r\n\r\n")) == std::string::npos) {
            if (buffer.size() > kMaxHeaderBytes || !receive()) {
                open = false;
                break;
            }
        }
        if (!open) {
            break;
        }
        const std::string head = buffer.substr(0, headEnd);
        buffer.erase(0, headEnd + 4);
        const size_t methodEnd = head.find(' ');
        const size_t targetEnd = methodEnd == std::string::npos ? std::string::npos : head.find(' ', methodEnd + 1);
        if (targetEnd == std::string::npos) {
            SendAll(client, Response(400, "Bad Request", R"({"error":"bad request"})"));
            break;
        }
        // Browsers send Origin with requests a web page makes; GameSense clients are programs and send none.
        if (!HeaderValue(head, "Origin").empty()) {
            SendAll(client, Response(403, "Forbidden", R"({"error":"forbidden"})"));
            break;
        }
        std::string target = head.substr(methodEnd + 1, targetEnd - methodEnd - 1);
        target = target.substr(0, target.find('?'));
        if (!HeaderValue(head, "Transfer-Encoding").empty()) {
            SendAll(client, Response(411, "Length Required", R"({"error":"length required"})"));
            break;
        }
        const size_t length = static_cast<size_t>(strtoull(HeaderValue(head, "Content-Length").c_str(), nullptr, 10));
        if (length > kMaxBodyBytes) {
            SendAll(client, Response(413, "Payload Too Large", R"({"error":"too large"})"));
            break;
        }
        while (buffer.size() < length) {
            if (!receive()) {
                open = false;
                break;
            }
        }
        if (!open) {
            break;
        }
        const std::string body = buffer.substr(0, length);
        buffer.erase(0, length);
        std::optional<JsonValue> json = body.empty() ? std::optional<JsonValue>(JsonValue{}) : ParseJson(body);
        if (!json) {
            SendAll(client, Response(400, "Bad Request", R"({"error":"invalid JSON"})"));
            continue;
        }
        GameSenseRequest request;
        request.endpoint.assign(target.begin() + (target.starts_with("/") ? 1 : 0), target.end());
        request.body = std::move(*json);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (requests_.size() >= kMaxQueued) {
                requests_.erase(requests_.begin());
            }
            requests_.push_back(std::move(request));
        }
        PostMessageW(notify_, message_, 0, 0);
        const std::string connection = HeaderValue(head, "Connection");
        open = SendAll(client, Response(200, "OK", "{}")) && _stricmp(connection.c_str(), "close") != 0;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::erase(clients_, handle);
    }
    closesocket(client);
    done->store(true);
}
