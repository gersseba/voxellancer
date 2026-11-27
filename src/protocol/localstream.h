#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

// Both endpoints must be polled on the same thread.
class LocalStream {
public:
    enum class Status { Done, NotReady, Disconnected, Error };

    static std::pair<std::unique_ptr<LocalStream>, std::unique_ptr<LocalStream>> createPair();
    ~LocalStream();

    LocalStream(const LocalStream&) = delete;
    LocalStream& operator=(const LocalStream&) = delete;

    Status send(const void* data, std::size_t size, std::size_t& sent);
    Status receive(void* data, std::size_t size, std::size_t& received);
    void disconnect();
    bool connected() const;

private:
    struct State;
    LocalStream(std::shared_ptr<State> state, std::size_t side);

    std::shared_ptr<State> m_state;
    std::size_t m_side;
};
