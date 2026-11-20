#include <rules.h>
#include <config.h>
#include <utils.h>
#include <algorithm>
#include <mutex>
#include <cctype>
#include <cwchar>

namespace {
    // pre-processed form of a rule used for matching
    struct CompiledRule {
        std::string key;     // lowercase name or lowercase normalized absolute path
        std::string baseKey; // lowercase executable name (also set for path rules, used as a cheap pre-filter)
        bool matchPath;
        int pstate;
    };

    std::mutex rulesMutex;
    std::vector<CompiledRule> activeRules;

    std::string trim(const std::string& str) {
        const char* whitespace = " \t\r\n";
        size_t start = str.find_first_not_of(whitespace);

        if (start == std::string::npos) {
            return "";
        }

        size_t end = str.find_last_not_of(whitespace);
        return str.substr(start, end - start + 1);
    }

    std::string expandEnvironmentVariables(const std::string& str) {
        if (str.find('%') == std::string::npos) {
            return str;
        }

        std::wstring input = stringToWString(str);
        DWORD size = ExpandEnvironmentStringsW(input.c_str(), nullptr, 0);

        if (size == 0) {
            return str;
        }

        std::wstring expanded(size, L'\0');

        if (ExpandEnvironmentStringsW(input.c_str(), &expanded[0], size) == 0) {
            return str;
        }

        expanded.resize(wcslen(expanded.c_str()));
        return wStringToString(expanded);
    }

    // lowercase + backslashes only
    std::string normalizePathKey(std::string path) {
        std::replace(path.begin(), path.end(), '/', '\\');
        return toLower(path);
    }

    std::string baseNameOf(const std::string& path) {
        size_t pos = path.find_last_of("\\/");
        return pos == std::string::npos ? path : path.substr(pos + 1);
    }
}

bool normalizeRulePattern(const std::string& input, bool matchPath, std::string& output, std::string& error) {
    std::string value = trim(input);

    // "Copy as path" in explorer wraps the path in quotes
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = trim(value.substr(1, value.size() - 2));
    }

    if (value.empty()) {
        error = "Empty entry";
        return false;
    }

    if (matchPath) {
        value = expandEnvironmentVariables(value);
        std::replace(value.begin(), value.end(), '/', '\\');

        // require a drive letter (C:\...) or UNC (\\server\...) path
        bool isDrivePath = value.size() >= 3 && isalpha(static_cast<unsigned char>(value[0])) && value[1] == ':' && value[2] == '\\';
        bool isUncPath = value.size() >= 3 && value[0] == '\\' && value[1] == '\\';

        if (!isDrivePath && !isUncPath) {
            error = "\"" + input + "\" is not an absolute path (expected something like C:\\Games\\game.exe)";
            return false;
        }

        if (value.back() == '\\') {
            error = "\"" + input + "\" is a folder, not an executable";
            return false;
        }

        output = value;
        return true;
    }

    // name mode: if a path was given, only use the executable name
    value = baseNameOf(value);

    if (value.empty()) {
        error = "\"" + input + "\" does not contain an executable name";
        return false;
    }

    // process names always include the extension
    if (value.find('.') == std::string::npos) {
        value += ".exe";
    }

    output = value;
    return true;
}

void setActiveRules(const std::vector<ProcessRule>& rules) {
    std::vector<CompiledRule> compiled;

    for (const ProcessRule& rule : rules) {
        CompiledRule c;
        c.matchPath = rule.matchPath;
        c.pstate = rule.pstate;

        if (rule.matchPath) {
            c.key = normalizePathKey(rule.pattern);
            c.baseKey = baseNameOf(c.key);
        } else {
            c.key = toLower(rule.pattern);
            c.baseKey = c.key;
        }

        compiled.push_back(c);
    }

    std::lock_guard<std::mutex> lock(rulesMutex);
    activeRules = std::move(compiled);
}

int findRulePState(const std::string& processName, DWORD pid) {
    std::vector<CompiledRule> rules;

    {
        std::lock_guard<std::mutex> lock(rulesMutex);
        rules = activeRules;
    }

    const std::string name = toLower(processName);

    // the path is only queried if a path rule has a matching executable name
    bool isPathResolved = false;
    std::string processPath;

    int best = -1;

    for (const CompiledRule& rule : rules) {
        if (rule.baseKey != name) {
            continue;
        }

        bool isMatch;

        if (rule.matchPath) {
            if (!isPathResolved) {
                processPath = normalizePathKey(getProcessImagePath(pid));
                isPathResolved = true;
            }

            isMatch = !processPath.empty() && processPath == rule.key;
        } else {
            isMatch = true;
        }

        // lowest P-State number = highest performance, 0 = unlimited
        if (isMatch && (best < 0 || rule.pstate < best)) {
            best = rule.pstate;
        }
    }

    return best;
}

std::vector<ProcessRule> loadRulesFromConfig(bool& migrated) {
    std::vector<ProcessRule> rules;
    migrated = false;

    if (!config.contains("process_exceptions") || !config["process_exceptions"].is_array()) {
        return rules;
    }

    for (const auto& entry : config["process_exceptions"]) {
        ProcessRule rule;

        if (entry.is_string()) {
            // old format: plain process name that unlimits the P-State
            rule.pattern = entry.get<std::string>();
            rule.matchPath = false;
            rule.pstate = 0;
            migrated = true;
        } else if (entry.is_object() && entry.contains("process") && entry["process"].is_string()) {
            rule.pattern = entry["process"].get<std::string>();
            rule.matchPath = entry.value("match_path", false);
            rule.pstate = entry.value("pstate", 0);
        } else {
            migrated = true; // drop invalid entries
            continue;
        }

        if (rule.pstate < 0) {
            rule.pstate = 0;
        }

        if (!rule.pattern.empty()) {
            rules.push_back(rule);
        }
    }

    return rules;
}

void storeRulesToConfig(const std::vector<ProcessRule>& rules) {
    nlohmann::json list = nlohmann::json::array();

    for (const ProcessRule& rule : rules) {
        list.push_back({
            { "process", rule.pattern },
            { "match_path", rule.matchPath },
            { "pstate", rule.pstate }
            });
    }

    config["process_exceptions"] = list;
}

std::string describeRule(const ProcessRule& rule) {
    std::string state = rule.pstate == 0 ? "P0 (unlimited)" : "P" + std::to_string(rule.pstate);
    return rule.pattern + "   ->   " + state + (rule.matchPath ? "   [full path]" : "");
}
