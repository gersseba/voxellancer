#pragma once

#include <filesystem>
#include <map>
#include <string>

class SettingsStore {
public:
    using Values = std::map<std::string, std::string>;
    using Groups = std::map<std::string, Values>;
    enum class Status { Idle, Saving, Saved, Failed };

    explicit SettingsStore(std::filesystem::path path);
    const Values& group(const std::string& name) const;
    void save(const Groups& groups);
    Status status() const;
    static std::string number(float value);
    static float number(const std::string& value);

private:
    std::filesystem::path m_path;
    Groups m_groups;
    Status m_status = Status::Idle;
};
