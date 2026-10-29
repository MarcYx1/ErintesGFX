#pragma once
#include <map>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

using Json = nlohmann::ordered_json;

struct GenResult {
    std::map<std::string, std::string> files; // path relative to the TouchGFX/ folder -> content
    std::vector<std::string> warnings;        // things the generator does not support yet
};

// Generates generated/gui_generated/* and generated/simulator SimConstants from a .touchgfx project.
GenResult generate(const Json& project);

// Writes the files, only touching ones whose content changed. Returns the number written.
int writeFiles(const std::string& touchgfxDir, const GenResult& r);
