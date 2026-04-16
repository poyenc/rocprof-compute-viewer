// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <vector>

namespace Headless
{

/// Parsed command-line arguments for the headless CLI.
struct HeadlessArgs
{
    std::string command;                              ///< Subcommand name (info, isa, waves, etc.)
    std::string uiOutputDir;                          ///< Path to the ui_output directory
    std::vector<std::string> options;                  ///< Command-specific options (--flag value pairs)
    int limit = -1;                                    ///< Pagination limit (-1 = no limit)
    int offset = 0;                                    ///< Pagination offset
    bool interactive = false;                          ///< Interactive (REPL) mode
    bool compact = false;                              ///< Compact JSON output
    bool version = false;                              ///< Print version and exit
    bool help = false;                                 ///< Print help and exit
};

/// Parse command-line arguments into HeadlessArgs.
HeadlessArgs parseArgs(int argc, char* argv[]);

/// Parse a single interactive-mode line into HeadlessArgs.
/// The uiOutputDir is inherited from the session.
HeadlessArgs parseInteractiveLine(const std::string& line, const std::string& uiOutputDir);

/// Dispatch a parsed command to the appropriate handler.
/// Returns the process exit code.
int dispatch(const HeadlessArgs& args);

/// Run the interactive REPL loop, reading commands from stdin.
int runInteractive(const std::string& uiOutputDir);

/// Get an option value from the options vector. Returns defaultVal if not found.
std::string getOption(const std::vector<std::string>& options, const std::string& flag, const std::string& defaultVal = "");

/// Check if a flag is present in the options vector.
bool hasFlag(const std::vector<std::string>& options, const std::string& flag);

/// Print usage/help text.
void printHelp();

} // namespace Headless
