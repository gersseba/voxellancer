#include "settingsstore.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <random>
#include <sstream>
#include <stdexcept>
#include <glow/logging.h>

#include "property/propertyregex.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#elif defined(WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

SettingsStore::SettingsStore(std::filesystem::path path): m_path(std::move(path)) {
    if (!std::filesystem::exists(m_path)) return;
    std::ifstream input(m_path);
    if (!input) throw std::runtime_error("Could not read settings: " + m_path.string());
    std::string line, section;
    std::size_t row = 0;
    while (std::getline(input,line)) {
        ++row;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#') continue;
        regexns::smatch match;
        if (regexns::regex_match(line,match,title_regex())) section = match[1];
        else if (!section.empty() && regexns::regex_match(line,match,line_regex()) &&
                 !match[1].str().empty()) {
            if (!m_groups[section].emplace(match[1],match[2]).second)
                throw std::runtime_error("Duplicate setting at " + m_path.string() +
                    ":" + std::to_string(row));
        } else {
            throw std::runtime_error("Invalid settings at " + m_path.string() +
                ":" + std::to_string(row));
        }
    }
    if (input.bad()) throw std::runtime_error("Could not read settings: " + m_path.string());
}

const SettingsStore::Values& SettingsStore::group(const std::string& name) const {
    static const Values empty;
    const auto found = m_groups.find(name);
    return found == m_groups.end() ? empty : found->second;
}

void SettingsStore::save(const Groups& groups) {
    if (status() == Status::Saving)
        throw std::runtime_error("A settings save is still in progress.");
    auto next = m_groups;
    for (const auto& group : groups) {
        const auto validKey = [](const std::string& key, bool dots) {
            return !key.empty() && std::all_of(key.begin(),key.end(),[dots](char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '_' || (dots && c == '.');
            });
        };
        if (!validKey(group.first,false)) throw std::invalid_argument("Invalid settings group.");
        for (const auto& value : group.second)
            if (!validKey(value.first,true) || value.second.empty() ||
                value.second.find_first_of("\r\n#") != std::string::npos)
                throw std::invalid_argument("Invalid setting: " + value.first);
        next[group.first] = group.second;
    }
    auto temporary = m_path;
    temporary += "." + std::to_string(std::random_device{}()) + ".tmp";
    try {
        std::ofstream output(temporary,std::ios::trunc);
        output.imbue(std::locale::classic());
        for (const auto& group : next) {
            output << '[' << group.first << "]\n";
            for (const auto& value : group.second)
                output << value.first << '=' << value.second << '\n';
        }
        output.close();
        if (!output) throw std::runtime_error("Could not write settings: " + temporary.string());
#if defined(WIN32) && !defined(__EMSCRIPTEN__)
        if (!MoveFileExW(temporary.c_str(),m_path.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::system_error(GetLastError(),std::system_category(),"Replacing settings");
#else
        std::filesystem::rename(temporary,m_path);
#endif
    } catch (const std::exception&) {
        m_status = Status::Failed;
        std::error_code error;
        std::filesystem::remove(temporary,error);
        if (error) glow::warning("Could not remove temporary settings: %;",error.message());
        throw;
    }
    m_groups = std::move(next);
#ifdef __EMSCRIPTEN__
    m_status = Status::Saving;
    EM_ASM({
        Module.settingsPersistenceState = 0;
        Module.persistChanges(ok => { Module.settingsPersistenceState = ok ? 1 : -1; });
    });
#else
    m_status = Status::Saved;
#endif
}

SettingsStore::Status SettingsStore::status() const {
#ifdef __EMSCRIPTEN__
    if (m_status == Status::Saving) {
        const int result = EM_ASM_INT({ return Module.settingsPersistenceState || 0; });
        return result > 0 ? Status::Saved : result < 0 ? Status::Failed : Status::Saving;
    }
#endif
    return m_status;
}

std::string SettingsStore::number(float value) {
    if (!std::isfinite(value)) throw std::invalid_argument("Non-finite saved setting.");
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(std::numeric_limits<float>::max_digits10) << value;
    return stream.str();
}

float SettingsStore::number(const std::string& value) {
    std::istringstream stream(value);
    stream.imbue(std::locale::classic());
    float result;
    if (!(stream >> result) || !std::isfinite(result))
        throw std::invalid_argument("Invalid numeric setting: " + value);
    stream >> std::ws;
    if (!stream.eof()) throw std::invalid_argument("Invalid numeric setting: " + value);
    return result;
}
