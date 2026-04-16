// SPDX-License-Identifier: MIT
#include "cmd_counters.h"
#include "analysis/derived_counter.h"
#include "json_output.h"
#include "util/jsonfilereader.h"

namespace Headless
{

/// Build 4D counter tensors from perfcounter JSON files.
/// Each counter_names[i] maps to column (i%4)+1 in bank i/4.
/// Tensor shape: (1, num_SEs, num_CUs, num_samples).
static void buildCounterTensors(
    SessionCache& cache,
    DerivedCounter::DerivedCounterManager& mgr
)
{
    auto counterNames = cache.counterNames();
    int numSE = cache.numShaderEngines();
    if (counterNames.empty() || numSE == 0) return;

    // First pass: determine dimensions (max CU, max samples per bank)
    int maxCU = 0;
    // Per-SE, per-bank: map<(cu), vector<values>>
    // We need to figure out the shape first

    struct SampleEntry
    {
        int64_t timestamp;
        int counters[4];
        int cu;
        int bank;
    };

    std::vector<std::vector<SampleEntry>> seData(numSE);

    for (int se = 0; se < numSE; ++se)
    {
        std::string perfFile = cache.baseDir() + "se" + std::to_string(se) + "_perfcounter.json";
        JsonFileReader reader(perfFile, false);
        if (!reader.bValid) continue;

        if (!reader.data.contains("data")) continue;

        for (auto& row : reader.data["data"])
        {
            if (row.size() < 7) continue;
            SampleEntry e;
            e.timestamp = row[0].get<int64_t>();
            e.counters[0] = row[1].get<int>();
            e.counters[1] = row[2].get<int>();
            e.counters[2] = row[3].get<int>();
            e.counters[3] = row[4].get<int>();
            e.cu = row[5].get<int>();
            e.bank = row[6].get<int>();
            seData[se].push_back(e);
            if (e.cu + 1 > maxCU) maxCU = e.cu + 1;
        }
    }

    if (maxCU == 0) return;

    // Count samples per CU/bank/SE to determine time dimension
    // For simplicity, count samples for first encountered CU in bank 0
    int numSamples = 0;
    for (int se = 0; se < numSE; ++se)
    {
        std::map<std::pair<int, int>, int> cuBankCount;  // (cu, bank) -> count
        for (auto& e : seData[se])
            cuBankCount[{e.cu, e.bank}]++;

        for (auto& [key, count] : cuBankCount)
            if (count > numSamples) numSamples = count;
    }

    if (numSamples == 0) return;

    // Build tensor for each counter
    // counter_names[i] -> column (i%4) in bank (i/4)
    for (size_t ci = 0; ci < counterNames.size(); ++ci)
    {
        int bank = static_cast<int>(ci) / 4;
        int col = static_cast<int>(ci) % 4;

        DerivedCounter::Shape shape(1, numSE, maxCU, numSamples);
        auto tensor = std::make_shared<DerivedCounter::Tensor>(shape, 0.0f);

        for (int se = 0; se < numSE; ++se)
        {
            // Group entries by CU for this bank
            std::map<int, int> cuSampleIdx;  // cu -> next sample index

            for (auto& e : seData[se])
            {
                if (e.bank != bank) continue;

                int sIdx = cuSampleIdx[e.cu]++;
                if (sIdx >= numSamples) continue;

                tensor->at(0, se, e.cu, sIdx) = static_cast<float>(e.counters[col]);
            }
        }

        mgr.context().setCounter(counterNames[ci], tensor);
    }
}

int cmdCounters(const HeadlessArgs& args, SessionCache& cache)
{
    bool listMode = hasFlag(args.options, "--list");
    std::string exprStr = getOption(args.options, "--expr");
    std::string defsFile = getOption(args.options, "--definitions");

    DerivedCounter::DerivedCounterManager mgr;

    // Build raw counter tensors
    buildCounterTensors(cache, mgr);

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
        data["message"] = "No derived counters defined. Use --definitions FILE or --expr EXPR.";
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
