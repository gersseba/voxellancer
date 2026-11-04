#pragma once

#include "benchmark/benchmarkmode.h"

class Game;

class BenchmarkRunner {
public:
    explicit BenchmarkRunner(BenchmarkMode mode);

    int run(Game& game) const;

private:
    BenchmarkMode m_mode;
};
