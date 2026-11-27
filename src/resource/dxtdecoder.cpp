#include "dxtdecoder.h"

#include <array>
#include <limits>
#include <stdexcept>

namespace {
using Color = std::array<std::uint8_t, 4>;

std::uint16_t read16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(data[0] | (data[1] << 8));
}

Color unpack565(std::uint16_t value) {
    const auto r = (value >> 11) & 31;
    const auto g = (value >> 5) & 63;
    const auto b = value & 31;
    return {static_cast<std::uint8_t>((r << 3) | (r >> 2)),
        static_cast<std::uint8_t>((g << 2) | (g >> 4)),
        static_cast<std::uint8_t>((b << 3) | (b >> 2)), 255};
}
}

std::vector<std::uint8_t> decodeDxt(
    DxtFormat format, unsigned int width, unsigned int height,
    const std::uint8_t* data, std::size_t size)
{
    const std::size_t blocksX = (static_cast<std::size_t>(width) + 3) / 4;
    const std::size_t blocksY = (static_cast<std::size_t>(height) + 3) / 4;
    const std::size_t blockSize = format == DxtFormat::Dxt1 ? 8 : 16;
    if (!width || !height || !data ||
        blocksX > std::numeric_limits<std::size_t>::max() / blocksY / blockSize ||
        size < blocksX * blocksY * blockSize ||
        static_cast<std::size_t>(width) > std::numeric_limits<std::size_t>::max() / height / 4) {
        throw std::invalid_argument("Invalid or truncated DXT texture.");
    }

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
    for (std::size_t by = 0; by < blocksY; ++by) {
        for (std::size_t bx = 0; bx < blocksX; ++bx) {
            const auto* block = data + (by * blocksX + bx) * blockSize;
            const auto* colorBlock = block + (format == DxtFormat::Dxt1 ? 0 : 8);
            const auto c0 = read16(colorBlock);
            const auto c1 = read16(colorBlock + 2);
            std::array<Color, 4> colors{unpack565(c0), unpack565(c1), {}, {}};
            const bool fourColors = format != DxtFormat::Dxt1 || c0 > c1;
            for (int channel = 0; channel < 3; ++channel) {
                colors[2][channel] = static_cast<std::uint8_t>(fourColors
                    ? (2 * colors[0][channel] + colors[1][channel]) / 3
                    : (colors[0][channel] + colors[1][channel]) / 2);
                colors[3][channel] = static_cast<std::uint8_t>(fourColors
                    ? (colors[0][channel] + 2 * colors[1][channel]) / 3 : 0);
            }
            colors[2][3] = 255;
            colors[3][3] = fourColors ? 255 : 0;
            std::array<std::uint8_t, 8> alpha{};
            std::uint64_t alphaBits = 0;
            if (format == DxtFormat::Dxt5) {
                alpha[0] = block[0];
                alpha[1] = block[1];
                if (alpha[0] > alpha[1]) {
                    for (int i = 1; i <= 6; ++i)
                        alpha[i + 1] = static_cast<std::uint8_t>(((7 - i) * alpha[0] + i * alpha[1]) / 7);
                } else {
                    for (int i = 1; i <= 4; ++i)
                        alpha[i + 1] = static_cast<std::uint8_t>(((5 - i) * alpha[0] + i * alpha[1]) / 5);
                    alpha[6] = 0;
                    alpha[7] = 255;
                }
                for (int i = 0; i < 6; ++i)
                    alphaBits |= static_cast<std::uint64_t>(block[i + 2]) << (8 * i);
            }
            for (unsigned int p = 0; p < 16; ++p) {
                const std::size_t x = bx * 4 + p % 4;
                const std::size_t y = by * 4 + p / 4;
                if (x >= width || y >= height) continue;
                Color color = colors[(colorBlock[4 + p / 4] >> (2 * (p % 4))) & 3];
                if (format == DxtFormat::Dxt3)
                    color[3] = static_cast<std::uint8_t>(((block[p / 2] >> (4 * (p % 2))) & 15) * 17);
                else if (format == DxtFormat::Dxt5)
                    color[3] = alpha[(alphaBits >> (3 * p)) & 7];
                const auto offset = (y * width + x) * 4;
                for (int channel = 0; channel < 4; ++channel)
                    pixels[offset + channel] = color[channel];
            }
        }
    }
    return pixels;
}
