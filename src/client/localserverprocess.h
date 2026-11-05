#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

class LocalServerProcess {
public:
    LocalServerProcess() = default;
    ~LocalServerProcess();

    LocalServerProcess(const LocalServerProcess&) = delete;
    LocalServerProcess& operator=(const LocalServerProcess&) = delete;

    bool start(
        const std::filesystem::path& clientExecutable,
        std::uint16_t port);
    bool isRunning();
    void stop();
    const std::string& lastError() const;

private:
#ifdef _WIN32
    void* m_processHandle = nullptr;
#else
    int m_processId = -1;
#endif
    std::string m_lastError;
};
