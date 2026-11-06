#pragma once

#include "benchmark/benchmarkmode.h"

#include <string>

class Game;

class BenchmarkRunner {
public:
    BenchmarkRunner(
        BenchmarkMode mode,
        std::string runId = {},
        std::string clientId = {}
    );

    int run(Game& game) const;

private:
    BenchmarkMode m_mode;
    std::string m_runId;
    std::string m_clientId;
};
