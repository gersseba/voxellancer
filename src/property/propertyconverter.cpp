#include "propertyconverter.h"
#include <sstream>
#include <stdexcept>

#include "propertyregex.h"


float PropertyConverter::floatConverter(const std::string& s) {
    regexns::smatch matches;
    regexns::regex_match(s, matches, float_regex());
    
    if (matches[2] == "DEG") {
        return glm::radians(std::stof(matches[1]));
    } else {
        return std::stof(s);
    }
}

glm::vec2 PropertyConverter::vec2Converter(const std::string& s) {
    regexns::smatch matches;
    regexns::regex_match(s, matches, vec2_regex());

    float x = std::stof(matches[1]);
    float y = std::stof(matches[2]);

    return glm::vec2(x, y);
}

glm::vec3 PropertyConverter::vec3Converter(const std::string& s) {
    regexns::smatch matches;
    regexns::regex_match(s, matches, vec3_regex());

    float x = std::stof(matches[1]);
    float y = std::stof(matches[2]);
    float z = std::stof(matches[3]);

    return glm::vec3(x, y, z);
}

glm::vec4 PropertyConverter::vec4Converter(const std::string& s) {
    regexns::smatch matches;
    regexns::regex_match(s, matches, vec4_regex());

    float x = std::stof(matches[1]);
    float y = std::stof(matches[2]);
    float z = std::stof(matches[3]);
    float w = std::stof(matches[4]);

    return glm::vec4(x, y, z, w);
}

std::list<std::string> PropertyConverter::listConverter(const std::string& s) {
    if (!regexns::regex_match(s, list_regex()))
        throw std::invalid_argument("Invalid list property: " + s);
    std::list<std::string> result;
    std::istringstream stream(s);
    std::string entry;
    while (std::getline(stream, entry, ',')) {
        entry.erase(0, entry.find_first_not_of(' '));
        result.push_back(entry);
    }
    return result;
}

InputMapping PropertyConverter::inputMappingConverter(const std::string& s) {
    regexns::smatch matches;
    regexns::regex_match(s, matches, input_mapping_regex());

    InputType type = static_cast<InputType>(std::stoi(matches[1]));
    int index = std::stoi(matches[2]);
    float maxValue = std::stof(matches[3]);
    float idleValue = std::stof(matches[4]);

    return InputMapping(type, index, maxValue, idleValue);
}