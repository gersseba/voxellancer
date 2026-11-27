#pragma once

#include <cmath>
#include <stdexcept>
#include <string>

struct ConfigurationField {
    std::string label;
    float minimum, maximum, initial;
    bool integer = false;
    std::string property;

    void validate(float value) const {
        if (!std::isfinite(value) || value < minimum || value > maximum ||
            (integer && value != std::round(value)))
            throw std::invalid_argument("Configuration value outside allowed range: " + label);
    }
};
