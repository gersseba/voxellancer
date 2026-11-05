#include "localserverprocess.h"

#include <chrono>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <cstring>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {
    std::filesystem::path serverExecutablePath(
        const std::filesystem::path& clientExecutable)
    {
        const std::string clientStem = clientExecutable.stem().string();
        const std::string clientPrefix = "voxellancer";
        if (clientStem.compare(0, clientPrefix.size(), clientPrefix) != 0) {
            return {};
        }

        const std::string configurationSuffix =
            clientStem.substr(clientPrefix.size());
        return clientExecutable.parent_path() /
            (clientPrefix + "-server" + configurationSuffix +
                clientExecutable.extension().string());
    }
}

LocalServerProcess::~LocalServerProcess() {
    stop();
}

bool LocalServerProcess::start(
    const std::filesystem::path& clientExecutable,
    std::uint16_t port)
{
    if (isRunning()) {
        m_lastError = "A local simulation server is already running.";
        return false;
    }
    if (port == 0) {
        m_lastError = "A local simulation server requires a valid port.";
        return false;
    }

    const std::filesystem::path absoluteClient =
        std::filesystem::absolute(clientExecutable);
    const std::filesystem::path serverExecutable =
        serverExecutablePath(absoluteClient);
    if (serverExecutable.empty() || !std::filesystem::exists(serverExecutable)) {
        m_lastError = "Could not find the matching voxellancer-server executable.";
        return false;
    }

#ifdef _WIN32
    std::wstring commandLine = L"\"" + serverExecutable.wstring() +
        L"\" --listen --listen-port " +
        std::to_wstring(port) + L" --scenario battle";
    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};
    if (!CreateProcessW(
            serverExecutable.c_str(),
            commandLine.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startupInfo,
            &processInfo)) {
        m_lastError = "Could not start voxellancer-server (Windows error " +
            std::to_string(GetLastError()) + ").";
        return false;
    }
    CloseHandle(processInfo.hThread);
    m_processHandle = processInfo.hProcess;
#else
    const std::string portArgument = std::to_string(port);
    const std::string executable = serverExecutable.string();
    const pid_t processId = fork();
    if (processId < 0) {
        m_lastError = "Could not start voxellancer-server: " +
            std::string(std::strerror(errno));
        return false;
    }
    if (processId == 0) {
        execl(
            executable.c_str(),
            executable.c_str(),
            "--listen",
            "--listen-port",
            portArgument.c_str(),
            "--scenario",
            "battle",
            static_cast<char*>(nullptr));
        _exit(127);
    }
    m_processId = static_cast<int>(processId);
#endif
    m_lastError.clear();
    return true;
}

bool LocalServerProcess::isRunning() {
#ifdef _WIN32
    if (m_processHandle == nullptr) {
        return false;
    }
    DWORD exitCode = 0;
    if (!GetExitCodeProcess(
            static_cast<HANDLE>(m_processHandle),
            &exitCode)) {
        m_lastError = "Could not query the local simulation server process.";
        return false;
    }
    return exitCode == STILL_ACTIVE;
#else
    if (m_processId < 0) {
        return false;
    }
    int status = 0;
    const pid_t result = waitpid(
        static_cast<pid_t>(m_processId),
        &status,
        WNOHANG);
    if (result == 0 || (result < 0 && errno == EINTR)) {
        return true;
    }
    m_processId = -1;
    if (result < 0) {
        m_lastError = "Could not query the local simulation server process: " +
            std::string(std::strerror(errno));
    }
    return false;
#endif
}

void LocalServerProcess::stop() {
#ifdef _WIN32
    if (m_processHandle == nullptr) {
        return;
    }
    HANDLE processHandle = static_cast<HANDLE>(m_processHandle);
    if (WaitForSingleObject(processHandle, 0) == WAIT_TIMEOUT) {
        TerminateProcess(processHandle, 0);
        WaitForSingleObject(processHandle, 5000);
    }
    CloseHandle(processHandle);
    m_processHandle = nullptr;
#else
    if (m_processId < 0) {
        return;
    }
    const pid_t processId = static_cast<pid_t>(m_processId);
    if (kill(processId, SIGTERM) == 0) {
        for (int attempt = 0; attempt < 100; ++attempt) {
            if (!isRunning()) {
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        kill(processId, SIGKILL);
    }
    int status = 0;
    while (waitpid(processId, &status, 0) < 0 && errno == EINTR) {
    }
    m_processId = -1;
#endif
}

const std::string& LocalServerProcess::lastError() const {
    return m_lastError;
}
