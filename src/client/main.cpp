#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

#include "client/tcpclient.h"

namespace {
    struct ClientOptions {
        std::uint16_t port = 27800;
        std::uint32_t holdMilliseconds = 0;
    };

    std::uint16_t parsePort(const std::string& value) {
        std::size_t parsedLength = 0;
        const unsigned long parsed = std::stoul(value, &parsedLength);
        if (parsedLength != value.size() || parsed == 0 || parsed > 65535) {
            throw std::invalid_argument("Port must be between 1 and 65535.");
        }
        return static_cast<std::uint16_t>(parsed);
    }

    ClientOptions parseOptions(int argc, char* argv[]) {
        ClientOptions options;
        for (int index = 1; index < argc; ++index) {
            const std::string argument(argv[index]);
            if (argument == "--help") {
                std::cout << "Usage: voxellancer-client [--port port] "
                    "[--hold-ms duration]\n";
                std::exit(0);
            }
            if (index + 1 >= argc) {
                throw std::invalid_argument("Missing value for " + argument + ".");
            }
            const std::string value(argv[++index]);
            if (argument == "--port") {
                options.port = parsePort(value);
            } else if (argument == "--hold-ms") {
                std::size_t parsedLength = 0;
                const unsigned long parsed = std::stoul(value, &parsedLength);
                if (parsedLength != value.size() || parsed > 60000) {
                    throw std::invalid_argument(
                        "--hold-ms must be between 0 and 60000.");
                }
                options.holdMilliseconds = static_cast<std::uint32_t>(parsed);
            } else {
                throw std::invalid_argument("Unknown option: " + argument);
            }
        }
        return options;
    }
}

int main(int argc, char* argv[]) {
    try {
        const ClientOptions options = parseOptions(argc, argv);
        TcpClient client;
        if (!client.connectLocal(options.port)) {
            throw std::runtime_error(client.lastError());
        }

        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::seconds(5);
        std::optional<std::chrono::steady_clock::time_point> holdDeadline;
        bool inputSent = false;
        bool inputAccepted = false;
        std::uint64_t initialSnapshotSequence = 0;
        std::uint64_t commandTick = 0;

        while (true) {
            const auto now = std::chrono::steady_clock::now();
            if (!holdDeadline && now >= deadline) {
                break;
            }
            if (holdDeadline && now >= *holdDeadline) {
                break;
            }
            if (!client.poll()) {
                throw std::runtime_error(client.lastError());
            }
            if (!inputSent && client.actor() && client.snapshotSequence() != 0) {
                initialSnapshotSequence = client.snapshotSequence();
                commandTick = client.snapshotTick() + 4;
                const voxellancer::protocol::InputCommand command{
                    1, commandTick, 8192
                };
                if (!client.sendInput(command)) {
                    throw std::runtime_error(client.lastError());
                }
                inputSent = true;
            }
            for (const auto& acknowledgement : client.drainAcknowledgements()) {
                if (acknowledgement.sequence != 1) {
                    continue;
                }
                if (acknowledgement.result !=
                    voxellancer::protocol::InputCommandResult::Accepted) {
                    throw std::runtime_error("Server rejected the demonstration input.");
                }
                inputAccepted = true;
            }
            if (inputAccepted &&
                client.snapshotSequence() > initialSnapshotSequence &&
                client.snapshotTick() >= commandTick) {
                if (options.holdMilliseconds == 0) {
                    break;
                }
                if (!holdDeadline) {
                    holdDeadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(options.holdMilliseconds);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!inputSent || !inputAccepted ||
            client.snapshotSequence() <= initialSnapshotSequence ||
            client.snapshotTick() < commandTick) {
            throw std::runtime_error(
                "Timed out waiting for authoritative input and snapshot updates.");
        }

        const auto actor = *client.actor();
        std::cout << std::fixed << std::setprecision(3)
            << "{\n"
            << "  \"actor\": {\"index\": " << actor.index
            << ", \"generation\": " << actor.generation << "},\n"
            << "  \"snapshot_sequence\": " << client.snapshotSequence() << ",\n"
            << "  \"snapshot_tick\": " << client.snapshotTick() << ",\n"
            << "  \"replicated_entities\": " << client.replicaState().size() << ",\n"
            << "  \"input_acknowledged\": true\n"
            << "}\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "voxellancer-client: " << exception.what() << '\n';
        return 1;
    }
}
