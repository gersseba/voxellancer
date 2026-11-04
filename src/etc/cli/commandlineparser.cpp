#include "commandlineparser.h"

#include <string>
#include <glow/logging.h>


CommandLineParser::CommandLineParser():
    m_options()
{

}

void CommandLineParser::parse(int argc, char* argv[]) {
    for(int i = 1; i < argc; i++) {
        std::string arg(argv[i]);
        if (arg[0] == '-') {
            std::string arg(argv[i]+1);
            if (i + 1 < argc) {
                m_options[arg] = argv[i + 1];
            } else {
                m_options[arg] = "";
            }
        }
    }
    checkHelp();
}

bool CommandLineParser::stereoView() const {
    return isSet("stereo");
}

bool CommandLineParser::fullScreen() const {
    return isSet("fullscreen");
}

bool CommandLineParser::showIntro() const {
    return !isSet("nointro");
}

BenchmarkMode CommandLineParser::benchmarkMode() const {
    const bool rendering = isSet("benchmark-rendering");
    const bool combat = isSet("benchmark-combat");
    const bool largeCombatVsync = isSet("benchmark-large-combat");

    if (static_cast<int>(rendering) + static_cast<int>(combat) +
        static_cast<int>(largeCombatVsync) > 1) {
        return BenchmarkMode::Invalid;
    }
    if (rendering) {
        return BenchmarkMode::Rendering;
    }
    if (combat) {
        return BenchmarkMode::Combat;
    }
    if (largeCombatVsync) {
        return BenchmarkMode::LargeCombatVsync;
    }
    return BenchmarkMode::None;
}

bool CommandLineParser::isSet(const std::string& arg) const {
    return m_options.count(arg) > 0;
}

void CommandLineParser::checkHelp() {
    if (isSet("help") || isSet("-help") || isSet("h")) {
        glow::debug("Usage: voxellancer [-fullscreen] [-stereo] [-benchmark-rendering | -benchmark-combat | -benchmark-large-combat]\n");
        glow::debug("  -fullscreen  fullscreen mode");
        glow::debug("  -stereo      stereo rendering");
        glow::debug("  -nointro     [dev] don't show the intro");
        glow::debug("  -benchmark-rendering  render a fixed high-voxel scene and write benchmark results");
        glow::debug("  -benchmark-combat     simulate AI ships fighting and write benchmark results");
        glow::debug("  -benchmark-large-combat  run 800 ships with VSync and count frames below 55 FPS");
        exit(0);
    }
}
