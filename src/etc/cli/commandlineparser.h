#pragma once

#include <string>

#include <unordered_map>

#include "benchmark/benchmarkmode.h"


class CommandLineParser {
public:
    CommandLineParser();

    void parse(int argc, char* argv[]);

    bool stereoView() const;
    bool fullScreen() const;
    bool showIntro() const;
    bool serverPreview() const;
    bool localSingleplayer() const;
    BenchmarkMode benchmarkMode() const;

private:
    std::unordered_map<std::string, std::string> m_options;

    bool isSet(const std::string& arg) const;
    void checkHelp();

};
