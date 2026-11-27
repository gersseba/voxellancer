#include "configurationmenu.h"

#include <algorithm>
#include <cmath>

void ConfigurationMenu::slide(std::size_t index, float fraction) const {
    if (!std::isfinite(fraction)) throw std::invalid_argument("Invalid slider position.");
    const auto& field = fields.at(index);
    float next = field.minimum + std::clamp(fraction,0.0f,1.0f)*(field.maximum-field.minimum);
    if (field.integer) next = std::round(next);
    set(index,next);
}

void ConfigurationMenu::nudge(std::size_t index, int direction) const {
    if (direction != -1 && direction != 1)
        throw std::invalid_argument("Configuration adjustment requires a direction.");
    const auto& field = fields.at(index);
    const float step = field.integer ? 1 : (field.maximum-field.minimum)/1000;
    set(index,std::clamp(value(index)+direction*step,field.minimum,field.maximum));
}

int ConfigurationMenu::rowAt(const ConfigurationLayout& layout, glm::vec2 pointer) const {
    if (!std::isfinite(pointer.x) || !std::isfinite(pointer.y) ||
        pointer.x < layout.sliderLeft || pointer.x > layout.sliderLeft+layout.sliderWidth ||
        pointer.y < layout.top || pointer.y >= layout.top+fields.size()*layout.step)
        return -1;
    return static_cast<int>((pointer.y-layout.top)/layout.step);
}
