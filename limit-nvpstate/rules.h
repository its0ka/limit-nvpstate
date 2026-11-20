#pragma once

#include <string>
#include <vector>
#include <Windows.h>

// a per-process override of the global P-state limit
struct ProcessRule {
    // what the user entered: an executable name (game.exe) or an absolute path (C:\Games\game.exe)
    std::string pattern;
    // false: compare the process name only, true: compare the absolute path of the executable
    bool matchPath = false;
    // P-State to apply while the process matches (0 = unlimited / P0)
    int pstate = 0;
};

// normalizes user input for the given match mode
// returns false and sets error if the input can't be used
bool normalizeRulePattern(const std::string& input, bool matchPath, std::string& output, std::string& error);

// replaces the active rules (thread safe)
void setActiveRules(const std::vector<ProcessRule>& rules);

// returns the P-State of the best matching rule for a process (lowest number wins, 0 = unlimited)
// or -1 if no rule matches
int findRulePState(const std::string& processName, DWORD pid);

// config.json (de)serialization, old configs with a plain list of process names are migrated
// migrated is set to true if the config should be written back
std::vector<ProcessRule> loadRulesFromConfig(bool& migrated);
void storeRulesToConfig(const std::vector<ProcessRule>& rules);

std::string describeRule(const ProcessRule& rule);
