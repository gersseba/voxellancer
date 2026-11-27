#pragma once

#include <filesystem>
#include <memory>

class SimulationSession;
class TcpClient;
class TcpServer;
class World;

class LocalContractSession {
public:
    explicit LocalContractSession(const std::filesystem::path& checkpoint);
    ~LocalContractSession();

    std::unique_ptr<TcpClient> takeClient();
    void advance(double elapsedSeconds);

private:
    std::unique_ptr<World> m_world;
    std::unique_ptr<SimulationSession> m_simulation;
    std::unique_ptr<TcpServer> m_server;
    std::unique_ptr<TcpClient> m_client;
};
