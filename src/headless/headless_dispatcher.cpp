// SPDX-License-Identifier: MIT
#include "headless_dispatcher.h"
#include "cmd_counters.h"
#include "cmd_info.h"
#include "cmd_isa.h"
#include "cmd_latency.h"
#include "cmd_occupancy.h"
#include "cmd_perfcounters.h"
#include "cmd_summary.h"
#include "cmd_waves.h"
#include "json_output.h"
#include "session_cache.h"
#include "util/version.h"

#include <iostream>
#include <set>
#include <sstream>

namespace Headless
{

HeadlessArgs parseArgs(int argc, char* argv[])
{
    HeadlessArgs args;

    // First pass: extract global flags
    std::vector<std::string> remaining;
    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        if (a == "--version" || a == "-v")
        {
            args.version = true;
        }
        else if (a == "--help" || a == "-h")
        {
            args.help = true;
        }
        else if (a == "--interactive" || a == "-i")
        {
            args.interactive = true;
        }
        else if (a == "--compact")
        {
            args.compact = true;
        }
        else if (a == "--limit" && i + 1 < argc)
        {
            args.limit = std::stoi(argv[++i]);
        }
        else if (a == "--offset" && i + 1 < argc)
        {
            args.offset = std::stoi(argv[++i]);
        }
        else
        {
            remaining.push_back(a);
        }
    }

    // Second pass: parse remaining args
    if (!remaining.empty())
    {
        if (args.interactive)
        {
            // Interactive mode: remaining args are just the ui_output_dir
            args.uiOutputDir = remaining.back();
        }
        else
        {
            // Non-interactive: first remaining arg is the command
            args.command = remaining[0];

            // Identify which non-option args are values of --flag options
            // (they should NOT be treated as ui_output_dir)
            std::set<int> optionValues;
            for (int i = 1; i + 1 < static_cast<int>(remaining.size()); ++i)
            {
                if (remaining[i].substr(0, 2) == "--")
                    optionValues.insert(i + 1);
            }

            // Find the last arg that is a standalone non-option (not a --flag value)
            int lastNonOption = -1;
            for (int i = static_cast<int>(remaining.size()) - 1; i >= 1; --i)
            {
                if (remaining[i].substr(0, 2) != "--" && optionValues.find(i) == optionValues.end())
                {
                    lastNonOption = i;
                    break;
                }
            }

            if (lastNonOption > 0)
            {
                args.uiOutputDir = remaining[lastNonOption];
            }

            // Everything else between command and ui_output_dir is options
            for (int i = 1; i < static_cast<int>(remaining.size()); ++i)
            {
                if (i == lastNonOption) continue;
                args.options.push_back(remaining[i]);
            }
        }
    }

    // Interactive mode implies compact output
    if (args.interactive) args.compact = true;

    return args;
}

HeadlessArgs parseInteractiveLine(const std::string& line, const std::string& uiOutputDir)
{
    HeadlessArgs args;
    args.uiOutputDir = uiOutputDir;
    args.compact = true;

    // Tokenize the line
    std::istringstream iss(line);
    std::vector<std::string> tokens;
    std::string tok;
    while (iss >> tok) tokens.push_back(tok);

    if (tokens.empty()) return args;

    // First token is command
    args.command = tokens[0];

    // Rest are options, parse limit/offset inline
    for (size_t i = 1; i < tokens.size(); ++i)
    {
        if (tokens[i] == "--limit" && i + 1 < tokens.size())
        {
            args.limit = std::stoi(tokens[++i]);
        }
        else if (tokens[i] == "--offset" && i + 1 < tokens.size())
        {
            args.offset = std::stoi(tokens[++i]);
        }
        else
        {
            args.options.push_back(tokens[i]);
        }
    }

    return args;
}

std::string getOption(const std::vector<std::string>& options, const std::string& flag, const std::string& defaultVal)
{
    for (size_t i = 0; i < options.size(); ++i)
    {
        if (options[i] == flag && i + 1 < options.size())
            return options[i + 1];
    }
    return defaultVal;
}

bool hasFlag(const std::vector<std::string>& options, const std::string& flag)
{
    for (auto& opt : options)
        if (opt == flag) return true;
    return false;
}

void printHelp()
{
    std::cerr
        << "Usage: rcv [global-options] <command> [command-options] <ui_output_dir>\n"
        << "\n"
        << "Global options:\n"
        << "  --version, -v         Print version and exit\n"
        << "  --help, -h            Print this help and exit\n"
        << "  --interactive, -i     Interactive REPL mode\n"
        << "  --compact             Compact (single-line) JSON output\n"
        << "  --limit N             Limit number of items in output\n"
        << "  --offset N            Skip first N items in output\n"
        << "\n"
        << "Commands:\n"
        << "  info                  Show session metadata (gfxip, version, counters, etc.)\n"
        << "  summary               Show performance summary (activity, utilization, TFLOPS, hotspots)\n"
        << "  isa                   Show ISA instructions with hitcounts and cycles\n"
        << "  waves                 Show wave execution data (instructions, timeline, info)\n"
        << "  occupancy             Show occupancy events\n"
        << "  latency               Show per-instruction latency statistics\n"
        << "  perfcounters          Show raw performance counter samples\n"
        << "  counters              Evaluate derived counter expressions\n"
        << "\n"
        << "Command-specific options:\n"
        << "  summary:\n"
        << "    --top N             Number of hotspot instructions (default: 10)\n"
        << "  isa:\n"
        << "    --min-cycles N      Filter instructions with cycles >= N\n"
        << "    --sort FIELD        Sort by: cycles, hitcount, stall, idle, pcsamples, pcstalls\n"
        << "    --top N             Keep only top N instructions (after sort)\n"
        << "  waves:\n"
        << "    --se N              Filter by shader engine\n"
        << "    --cu N              Filter by compute unit\n"
        << "    --simd N            Filter by SIMD\n"
        << "  occupancy:\n"
        << "    --se N              Filter by shader engine\n"
        << "    --cu N              Filter by compute unit\n"
        << "    --level N           Aggregation level\n"
        << "  latency:\n"
        << "    --type TYPE         Counter type: vmem, lds, smem (default: vmem)\n"
        << "    --cu N              Target CU (default: 1)\n"
        << "    --perf-interval N   Perf interval (default: 40)\n"
        << "    --se N,M,...        Shader engines (default: all)\n"
        << "  perfcounters:\n"
        << "    --se N              Filter by shader engine\n"
        << "  counters:\n"
        << "    --list              List available derived counters\n"
        << "    --expr EXPR         Evaluate an expression\n"
        << "    --definitions FILE  Load definitions from file\n"
        << "    --no-builtins       Skip builtin counter definitions\n"
        << "\n"
        << "Note: --top is applied before --limit/--offset pagination.\n"
        << std::endl;
}

int dispatch(const HeadlessArgs& args)
{
    if (args.version)
    {
        auto& v = Version::Get();
        std::cout << "rcv " << v.viewer_major << "." << v.viewer_minor << "." << v.viewer_rev
                  << std::endl;
        return 0;
    }

    if (args.help || args.command.empty())
    {
        printHelp();
        return args.help ? 0 : 1;
    }

    if (args.uiOutputDir.empty())
    {
        writeError("ui_output_dir is required");
        return 1;
    }

    SessionCache cache(args.uiOutputDir);

    try
    {
        if (args.command == "summary") return cmdSummary(args, cache);
        if (args.command == "info") return cmdInfo(args, cache);
        if (args.command == "isa") return cmdIsa(args, cache);
        if (args.command == "waves") return cmdWaves(args, cache);
        if (args.command == "occupancy") return cmdOccupancy(args, cache);
        if (args.command == "latency") return cmdLatency(args, cache);
        if (args.command == "perfcounters") return cmdPerfcounters(args, cache);
        if (args.command == "counters") return cmdCounters(args, cache);

        writeError("Unknown command: " + args.command);
        return 1;
    }
    catch (const std::exception& e)
    {
        writeError(std::string("Command '") + args.command + "' failed: " + e.what());
        return 1;
    }
}

int runInteractive(const std::string& uiOutputDir)
{
    SessionCache cache(uiOutputDir);
    std::string line;

    while (std::getline(std::cin, line))
    {
        // Trim whitespace
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        size_t end = line.find_last_not_of(" \t\r\n");
        line = line.substr(start, end - start + 1);

        if (line.empty()) continue;
        if (line == "quit" || line == "exit") break;

        auto cmdArgs = parseInteractiveLine(line, uiOutputDir);

        try
        {
            if (cmdArgs.command == "summary") cmdSummary(cmdArgs, cache);
            else if (cmdArgs.command == "info") cmdInfo(cmdArgs, cache);
            else if (cmdArgs.command == "isa") cmdIsa(cmdArgs, cache);
            else if (cmdArgs.command == "waves") cmdWaves(cmdArgs, cache);
            else if (cmdArgs.command == "occupancy") cmdOccupancy(cmdArgs, cache);
            else if (cmdArgs.command == "latency") cmdLatency(cmdArgs, cache);
            else if (cmdArgs.command == "perfcounters") cmdPerfcounters(cmdArgs, cache);
            else if (cmdArgs.command == "counters") cmdCounters(cmdArgs, cache);
            else writeError("Unknown command: " + cmdArgs.command);
        }
        catch (const std::exception& e)
        {
            writeError(std::string("Command '") + cmdArgs.command + "' failed: " + e.what());
        }
    }

    return 0;
}

} // namespace Headless
