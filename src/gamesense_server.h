#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "json.h"

// One request of a GameSense client, parsed on the server's threads.
struct GameSenseRequest {
    std::wstring endpoint;  // The path without its slash, e.g. L"game_event".
    JsonValue body;
};

// A local HTTP server for SteelSeries GameSense clients, as the GameSense SDK describes the protocol
// (github.com/SteelSeries/gamesense-sdk). It listens on 127.0.0.1 only; requests are queued for the UI thread,
// which gets message whenever new ones arrive.
class GameSenseServer {
public:
    GameSenseServer() = default;
    ~GameSenseServer();
    GameSenseServer(const GameSenseServer&) = delete;
    GameSenseServer& operator=(const GameSenseServer&) = delete;

    // Listens on a free port and returns it; 0 when it could not.
    int Start(HWND notify, UINT message);
    void Stop();
    bool IsRunning() const { return port_ != 0; }
    int Port() const { return port_; }
    std::vector<GameSenseRequest> TakeRequests();

private:
    struct Worker {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> done;
    };

    void AcceptLoop();
    void Serve(uintptr_t client, const std::shared_ptr<std::atomic<bool>>& done);

    HWND notify_ = nullptr;
    UINT message_ = 0;
    int port_ = 0;
    bool winsock_ = false;
    uintptr_t listener_ = 0;
    std::thread acceptor_;
    std::mutex mutex_;
    std::vector<uintptr_t> clients_;          // Open connections; under mutex_.
    std::vector<Worker> workers_;             // Under mutex_.
    std::vector<GameSenseRequest> requests_;  // Under mutex_.
};
