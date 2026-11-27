#include <bandit/bandit.h>

#include <array>
#include <stdexcept>

#include "resource/dxtdecoder.h"

using namespace bandit;

go_bandit([]() {
    describe("DXT decoding", []() {
        it("decodes opaque and transparent DXT1 colors and clips edge blocks", []() {
            const std::array<std::uint8_t, 8> block = {0x00, 0x00, 0xff, 0xff, 0x0c, 0, 0, 0};
            const auto pixels = decodeDxt(DxtFormat::Dxt1, 2, 1, block.data(), block.size());
            AssertThat(pixels, Equals(std::vector<std::uint8_t>{0, 0, 0, 255, 0, 0, 0, 0}));
        });

        it("decodes explicit and interpolated alpha", []() {
            const std::array<std::uint8_t, 16> dxt3 = {
                0xf1, 0, 0, 0, 0, 0, 0, 0, 0x00, 0xf8, 0, 0, 0, 0, 0, 0};
            const auto pixels3 = decodeDxt(DxtFormat::Dxt3, 2, 1, dxt3.data(), dxt3.size());
            AssertThat(pixels3, Equals(std::vector<std::uint8_t>{255, 0, 0, 17, 255, 0, 0, 255}));
            const std::array<std::uint8_t, 16> dxt5 = {
                255, 0, 0x02, 0, 0, 0, 0, 0, 0x00, 0xf8, 0, 0, 0, 0, 0, 0};
            const auto pixels5 = decodeDxt(DxtFormat::Dxt5, 1, 1, dxt5.data(), dxt5.size());
            AssertThat(pixels5, Equals(std::vector<std::uint8_t>{255, 0, 0, 218}));
        });

        it("rejects truncated blocks", []() {
            const std::array<std::uint8_t, 7> block{};
            AssertThrows(std::invalid_argument,
                decodeDxt(DxtFormat::Dxt1, 4, 4, block.data(), block.size()));
        });
    });
});
