#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "gamesense_server.h"
#include "settings.h"

// Runs the user's GameSense add-ons with OpenChatMix as their GameSense server. GameSense clients find
// SteelSeries GG through the address in coreProps.json; while the add-ons run, the file holds the server's
// address instead, and it is put back once they stop. The add-ons are the user's own programs: OpenChatMix starts
// the files the user chose and ends them with itself (a job object closes them if it exits unexpectedly).
class AddOnHost {
public:
    AddOnHost() = default;
    ~AddOnHost();
    AddOnHost(const AddOnHost&) = delete;
    AddOnHost& operator=(const AddOnHost&) = delete;

    // run: SteelSeries GG is closed and a headset display is there to show on. Starts the server and the enabled
    // add-ons, or stops everything.
    void Update(bool run, const std::vector<AddOnSettings>& addOns, HWND notify, UINT message);
    void Stop();
    bool IsServing() const { return server_.IsRunning(); }
    // Starts again an add-on that exited, a few times at most; returns true when something changed.
    bool CheckProcesses();
    bool IsRunning(const std::wstring& path) const;
    std::vector<GameSenseRequest> TakeRequests() { return server_.TakeRequests(); }

private:
    struct Process {
        std::wstring path;
        HANDLE handle = nullptr;
        ULONGLONG started = 0;
        int restarts = 0;
    };

    bool Launch(Process& process);
    static void End(Process& process);
    bool WriteCoreProps(int port);
    void RestoreCoreProps();

    GameSenseServer server_;
    HANDLE job_ = nullptr;
    std::vector<Process> processes_;
    std::wstring propsPath_;
    std::string propsBefore_;   // The file as it was, put back afterwards.
    bool propsExisted_ = false;
    std::string propsWritten_;  // What this host wrote; empty when it wrote nothing.
};
