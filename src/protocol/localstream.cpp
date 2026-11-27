#include "localstream.h"

#include <algorithm>
#include <array>
#include <deque>

namespace {
    constexpr std::size_t BufferCapacity = 1024 * 1024;
}

struct LocalStream::State {
    std::array<std::deque<std::uint8_t>, 2> incoming;
    std::array<bool, 2> open{{true, true}};
};

LocalStream::LocalStream(std::shared_ptr<State> state, std::size_t side):
    m_state(std::move(state)),
    m_side(side)
{
}

LocalStream::~LocalStream() {
    disconnect();
}

std::pair<std::unique_ptr<LocalStream>, std::unique_ptr<LocalStream>>
LocalStream::createPair() {
    auto state = std::make_shared<State>();
    return {
        std::unique_ptr<LocalStream>(new LocalStream(state, 0)),
        std::unique_ptr<LocalStream>(new LocalStream(state, 1))
    };
}

bool LocalStream::connected() const {
    return m_state->open[m_side] && m_state->open[1 - m_side];
}

void LocalStream::disconnect() {
    m_state->open[m_side] = false;
}

LocalStream::Status LocalStream::send(const void* data, std::size_t size, std::size_t& sent) {
    sent = 0;
    if (!connected()) {
        return Status::Disconnected;
    }
    auto& incoming = m_state->incoming[1 - m_side];
    sent = std::min(size, BufferCapacity - incoming.size());
    if (sent == 0) {
        return size == 0 ? Status::Done : Status::NotReady;
    }
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    incoming.insert(incoming.end(), bytes, bytes + sent);
    return Status::Done;
}

LocalStream::Status LocalStream::receive(void* data, std::size_t size, std::size_t& received) {
    received = 0;
    if (!m_state->open[m_side]) {
        return Status::Disconnected;
    }
    auto& incoming = m_state->incoming[m_side];
    received = std::min(size, incoming.size());
    auto* bytes = static_cast<std::uint8_t*>(data);
    for (std::size_t i = 0; i < received; ++i) {
        bytes[i] = incoming.front();
        incoming.pop_front();
    }
    if (received != 0 || size == 0) {
        return Status::Done;
    }
    return m_state->open[1 - m_side] ? Status::NotReady : Status::Disconnected;
}
