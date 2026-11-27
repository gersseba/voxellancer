#pragma once

#include <stdexcept>
#include <string>

enum class ShieldType {
    None,
    Hull,
    Sphere
};

inline ShieldType shieldShapeFromString(const std::string& value) {
    if (value == "SPHERE") return ShieldType::Sphere;
    if (value == "HULL") return ShieldType::Hull;
    throw std::invalid_argument("Unknown global shield shape: " + value);
}
