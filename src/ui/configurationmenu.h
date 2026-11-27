#pragma once

#include <functional>
#include <vector>
#include <glm/glm.hpp>
#include "settings/configurationfield.h"

struct ConfigurationLayout {
    float top, step, sliderLeft, sliderWidth, valueLeft;
    float labelWidth, valueWidth, fontHeight;
};

struct ConfigurationMenu {
    const std::vector<ConfigurationField>& fields;
    std::function<float(std::size_t)> value;
    std::function<void(std::size_t,float)> set;
    std::function<void()> reset, save;

    void slide(std::size_t field, float fraction) const;
    void nudge(std::size_t field, int direction) const;
    int rowAt(const ConfigurationLayout& layout, glm::vec2 pointer) const;
};
