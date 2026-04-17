// SPDX-License-Identifier: MIT
#include "cmd_counters.h"
#include "analysis/builtin_counters.h"
#include "analysis/derived_counter.h"
#include "counter_tensors.h"
#include "json_output.h"
#include "util/jsonfilereader.h"

namespace Headless
{

int cmdCounters(const HeadlessArgs& args, SessionCache& cache)
{
    bool listMode = hasFlag(args.options, "--list");
    bool noBuiltins = hasFlag(args.options, "--no-builtins");
    std::string exprStr = getOption(args.options, "--expr");
    std::string defsFile = getOption(args.options, "--definitions");

    DerivedCounter::DerivedCounterManager mgr;

    // Build raw counter tensors
    buildCounterTensors(cache, mgr);

    // Load builtin definitions (utilization, TFLOPS) unless suppressed
    if (!noBuiltins)
    {
        try
        {
            mgr.loadDefinitions(BuiltinCounters::getDefinitions());
        }
        catch (const std::exception& e)
        {
            // Builtins may fail if required counters are missing — not fatal
        }
    }

    // Load definitions if specified
    if (!defsFile.empty())
    {
        try
        {
            mgr.loadDefinitionsFromFile(defsFile);
        }
        catch (const std::exception& e)
        {
            writeError("Failed to load definitions from " + defsFile + ": " + e.what());
            return 1;
        }
    }

    if (listMode)
    {
        // List mode: show available raw and derived counters
        nlohmann::json data;
        data["raw_counters"] = mgr.context().rawCounterNames();
        data["derived_counters"] = mgr.derivedCounterNames();

        if (args.compact)
            writeJsonCompact("counters", args.uiOutputDir, data);
        else
            writeJson("counters", args.uiOutputDir, data);
        return 0;
    }

    if (!exprStr.empty())
    {
        // Evaluate a single expression
        try
        {
            DerivedCounter::Parser parser;
            auto expr = parser.parseExpression(exprStr);
            auto result = expr->evaluate(mgr.context());

            nlohmann::json data;
            data["expression"] = exprStr;
            data["shape"] = result.shape().toString();

            if (result.isScalar())
            {
                data["value"] = result.scalar();
            }
            else
            {
                nlohmann::json values = nlohmann::json::array();
                for (size_t i = 0; i < result.size(); ++i)
                    values.push_back(result[i]);
                data["values"] = std::move(values);
            }

            if (args.compact)
                writeJsonCompact("counters", args.uiOutputDir, data);
            else
                writeJson("counters", args.uiOutputDir, data);
        }
        catch (const std::exception& e)
        {
            writeError("Expression evaluation failed: " + std::string(e.what()));
            return 1;
        }
        return 0;
    }

    // Default: evaluate all derived counters
    auto derived = mgr.derivedCounterNames();
    if (derived.empty())
    {
        nlohmann::json data;
        data["message"] = "No derived counters available. Perfcounter data may be missing.";
        data["raw_counters"] = mgr.context().rawCounterNames();

        if (args.compact)
            writeJsonCompact("counters", args.uiOutputDir, data);
        else
            writeJson("counters", args.uiOutputDir, data);
        return 0;
    }

    nlohmann::json results = nlohmann::json::object();
    for (auto& name : derived)
    {
        try
        {
            auto result = mgr.evaluate(name);
            if (!result) continue;

            nlohmann::json entry;
            entry["shape"] = result->shape().toString();

            if (result->isScalar())
            {
                entry["value"] = result->scalar();
            }
            else
            {
                nlohmann::json values = nlohmann::json::array();
                for (size_t i = 0; i < result->size(); ++i)
                    values.push_back((*result)[i]);
                entry["values"] = std::move(values);
            }

            results[name] = std::move(entry);
        }
        catch (const std::exception& e)
        {
            results[name] = {{"error", e.what()}};
        }
    }

    nlohmann::json data;
    data["counters"] = std::move(results);

    if (args.compact)
        writeJsonCompact("counters", args.uiOutputDir, data);
    else
        writeJson("counters", args.uiOutputDir, data);

    return 0;
}

} // namespace Headless
