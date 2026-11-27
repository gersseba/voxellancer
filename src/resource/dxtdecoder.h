#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

enum class DxtFormat { Dxt1, Dxt3, Dxt5 };

std::vector<std::uint8_t> decodeDxt(
    DxtFormat format, unsigned int width, unsigned int height,
    const std::uint8_t* data, std::size_t size);
