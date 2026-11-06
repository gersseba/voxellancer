#pragma once

#include <cstddef>

enum class BenchmarkMode {
    None,
    LargeCombat,
    Invalid
};

constexpr std::size_t BenchmarkSimulationIntervals = 1800;
constexpr int BenchmarkShipsPerTeam = 400;
