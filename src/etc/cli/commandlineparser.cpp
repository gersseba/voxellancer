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

bool CommandLineParser::serverPreview() const {
    return isSet("server-preview");
}

bool CommandLineParser::multiplayerClient() const {
    return isSet("multiplayer-client");
}

bool CommandLineParser::localSingleplayer() const {
    return isSet("local-singleplayer");
}

std::string CommandLineParser::scenario() const {
    const auto option = m_options.find("scenario");
    return option == m_options.end() ? std::string() : option->second;
}

BenchmarkMode CommandLineParser::benchmarkMode() const {
    if (isSet("benchmark-rendering") || isSet("benchmark-combat") ||
        isSet("benchmark-multiplayer")) {
        return BenchmarkMode::Invalid;
    }
    return isSet("benchmark-large-combat")
        ? BenchmarkMode::LargeCombat
        : BenchmarkMode::None;
}

std::string CommandLineParser::benchmarkRunId() const {
    const auto option = m_options.find("benchmark-run-id");
    return option == m_options.end() ? std::string() : option->second;
}

std::string CommandLineParser::benchmarkClientId() const {
    const auto option = m_options.find("benchmark-client-id");
    return option == m_options.end() ? std::string() : option->second;
}

bool CommandLineParser::isSet(const std::string& arg) const {
    return m_options.count(arg) > 0;
}

void CommandLineParser::checkHelp() {
    if (isSet("help") || isSet("-help") || isSet("h")) {
        glow::debug("Usage: voxellancer [-fullscreen] [-stereo] [-server-preview] [-multiplayer-client] [-local-singleplayer -benchmark-large-combat | -multiplayer-client -benchmark-large-combat] [-scenario shield-showcase]\n");
        glow::debug("  -fullscreen  fullscreen mode");
        glow::debug("  -stereo      stereo rendering");
        glow::debug("  -nointro     [dev] don't show the intro");
        glow::debug("  -server-preview  show interpolated snapshots from 127.0.0.1:27800");
        glow::debug("  -multiplayer-client  connect a visible client to 127.0.0.1:27801");
        glow::debug("  Default: main menu with an automatically started local contracts server");
        glow::debug("  -local-singleplayer  start a local authoritative server (contracts unless benchmarking)");
        glow::debug("  -benchmark-large-combat  benchmark client rendering and replication during a server-side 800-ship battle");
        glow::debug("  -benchmark-run-id <id>   group server and client benchmark reports");
        glow::debug("  -benchmark-client-id <id> identify this client in its benchmark report");
        glow::debug("  -scenario shield-showcase  start the shield showcase scenario");
        glow::debug("  -scenario ai-comparison  observe ten legacy-versus-interceptor AI duels");
        glow::debug("  -scenario contracts  open the main menu for station contracts with a local server");
        exit(0);
    }
}
