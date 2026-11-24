#pragma once

#include <array>

struct ScenarioMenuEntry {
    int id;
    const char* name;
};

inline constexpr std::array<ScenarioMenuEntry, 10> ScenarioMenuEntries{{
    {0, "Scripted Demo"},
    {1, "Legacy Fleet"},
    {2, "Battle"},
    {3, "Frozen Scene"},
    {4, "Pirates"},
    {5, "Friendly Fire"},
    {6, "Shield Showcase"},
    {8, "Large Combat (800 Ships)"},
    {9, "AI Dogfight Comparison"},
    {10, "Weapon Test Range"}
}};
