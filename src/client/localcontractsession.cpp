#include "localcontractsession.h"

#include <stdexcept>
#include <utility>

#include "client/tcpclient.h"
#include "protocol/localstream.h"
#include "server/tcpserver.h"
#include "simulation/contractrun.h"
#include "simulation/simulationsession.h"
#include "world/world.h"
#include "property/property.h"

LocalContractSession::LocalContractSession(const std::filesystem::path& checkpoint):
    m_world(std::make_unique<World>(World::Mode::Server)),
    m_simulation(std::make_unique<SimulationSession>(*m_world))
{
    m_world->setShieldShape(Property<std::string>("gameplay.shieldShape","SPHERE").get());
    auto streams = LocalStream::createPair();
    m_server = std::make_unique<TcpServer>(
        *m_world, *m_simulation, 0, ContractInterestRadius,
        std::nullopt, 1, std::move(streams.second));
    m_client = std::make_unique<TcpClient>();
    if (!m_client->connectInProcess(std::move(streams.first))) {
        throw std::runtime_error(m_client->lastError());
    }
    m_server->poll();
    if (m_server->clientCount() != 1) {
        throw std::runtime_error("Could not initialize the local contracts connection.");
    }
    const auto players = ContractRun::createPlayers(*m_world, 1);
    m_server->enableContracts(players, checkpoint, true);
    m_server->startWorldForPlayers(players);
}

LocalContractSession::~LocalContractSession() = default;

std::unique_ptr<TcpClient> LocalContractSession::takeClient() {
    if (!m_client) {
        throw std::logic_error("The local contracts client was already taken.");
    }
    return std::move(m_client);
}

void LocalContractSession::advance(double elapsedSeconds) {
    m_server->poll();
    m_simulation->advance(elapsedSeconds);
    m_server->poll();
}
