/*
 * XSigma: High-Performance Computational Library
 *
 * SPDX-License-Identifier: GPL-3.0-or-later OR Commercial
 *
 * Benchmark suite for the Graph module's executor: measures the per-run
 * overhead of graph_executor (thread-pool reuse, shared results, ready
 * propagation) on graph shapes a realistic caller would build.
 */

#include <benchmark/benchmark.h>

#include <any>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "dependency_graph.h"
#include "graph_builder.h"
#include "graph_executor.h"
#include "graph_types.h"

namespace graph
{

namespace
{

// Builds a layered diamond graph: `layers` levels of `width` nodes each.
// Layer 0 are sources; each node in layer L depends on every node in layer
// L-1 (a dense bipartite edge set between adjacent layers). This is the
// shape a batched pipeline (e.g. per-tenor curve nodes feeding a per-tenor
// pricing node) takes, and it stresses fan-in/fan-out and ready propagation.
std::shared_ptr<dependency_graph> build_layered_graph(int layers, int width)
{
    graph_builder builder;

    std::vector<std::vector<node_id>> layer_ids(layers);
    for (int l = 0; l < layers; ++l)
    {
        layer_ids[l].reserve(width);
        for (int w = 0; w < width; ++w)
        {
            const node_id id = builder.add_node(
                "L" + std::to_string(l) + "_N" + std::to_string(w),
                [](const std::vector<std::any>& inputs) -> std::any
                {
                    int sum = 0;
                    for (const auto& in : inputs)
                    {
                        sum += std::any_cast<int>(in);
                    }
                    return sum + 1;
                });
            layer_ids[l].push_back(id);
        }
    }

    for (int l = 1; l < layers; ++l)
    {
        for (const node_id node : layer_ids[l])
        {
            for (const node_id dep : layer_ids[l - 1])
            {
                builder.depends_on(node, dep);
            }
        }
    }

    std::shared_ptr<dependency_graph> g;
    if (!builder.build(g).ok())
    {
        return nullptr;
    }
    return g;
}

// Builds a wide graph of `count` fully independent nodes -- the worst case
// for scheduling overhead (maximal parallelism, no dependency structure to
// prune the ready set).
std::shared_ptr<dependency_graph> build_independent_graph(int count)
{
    graph_builder builder;
    for (int i = 0; i < count; ++i)
    {
        builder.add_node(
            "n" + std::to_string(i),
            [i](const std::vector<std::any>&) -> std::any { return i * i; });
    }
    std::shared_ptr<dependency_graph> g;
    if (!builder.build(g).ok())
    {
        return nullptr;
    }
    return g;
}

void run_graph(benchmark::State& state, const dependency_graph& g, int threads)
{
    graph_executor                        executor(graph_executor_options{threads});
    std::unordered_map<node_id, std::any> results;

    for (auto _ : state)
    {
        const graph_execution_status status = executor.run(g, results);
        // Consume the status so the run is not optimized away.
        if (!status.ok())
        {
            state.SkipWithError("graph run failed");
            break;
        }
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(g.node_count()));
    state.counters["nodes"] = static_cast<double>(g.node_count());
}

}  // namespace

// Small graph, single worker: isolates per-run executor overhead on a
// shape small enough that thread startup would dominate if the pool were
// not persistent.
static void BM_Graph_SmallDiamond_SingleThread(benchmark::State& state)
{
    static std::shared_ptr<dependency_graph> g = build_layered_graph(4, 4);
    run_graph(state, *g, 1);
}
BENCHMARK(BM_Graph_SmallDiamond_SingleThread);

// Same small graph, multi-threaded: measures the marginal cost of pushing
// work across the persistent pool.
static void BM_Graph_SmallDiamond_MultiThread(benchmark::State& state)
{
    static std::shared_ptr<dependency_graph> g = build_layered_graph(4, 4);
    run_graph(state, *g, 4);
}
BENCHMARK(BM_Graph_SmallDiamond_MultiThread);

// Wide independent graph: maximal parallelism, stresses the ready queue and
// per-node scheduling cost with no dependency pruning.
static void BM_Graph_WideIndependent_MultiThread(benchmark::State& state)
{
    static std::shared_ptr<dependency_graph> g = build_independent_graph(64);
    run_graph(state, *g, 4);
}
BENCHMARK(BM_Graph_WideIndependent_MultiThread);

// Deeper pipeline: more layers of dependency propagation, stresses the
// ready-propagation path (push_dependent's continuation chaining).
static void BM_Graph_DeepPipeline_MultiThread(benchmark::State& state)
{
    static std::shared_ptr<dependency_graph> g = build_layered_graph(8, 8);
    run_graph(state, *g, 4);
}
BENCHMARK(BM_Graph_DeepPipeline_MultiThread);

// run_to on a graph where half the nodes are dead for the requested sink:
// measures the DCE win (only the live branch is scheduled) plus liveness
// slot reuse, against running the whole graph.
static void BM_Graph_RunTo_PrunedHalf(benchmark::State& state)
{
    // Two independent 4x4 layered halves; only the first feeds the sink.
    static std::shared_ptr<dependency_graph> g = []()
    {
        graph_builder                     builder;
        std::vector<std::vector<node_id>> layers(4);
        for (int l = 0; l < 4; ++l)
        {
            for (int w = 0; w < 4; ++w)
            {
                const node_id id = builder.add_node(
                    "L" + std::to_string(l) + "_N" + std::to_string(w),
                    [](const std::vector<std::any>& inputs) -> std::any
                    {
                        int sum = 0;
                        for (const auto& in : inputs)
                        {
                            sum += std::any_cast<int>(in);
                        }
                        return sum + 1;
                    });
                layers[l].push_back(id);
            }
        }
        for (int l = 1; l < 4; ++l)
        {
            for (const node_id node : layers[l])
            {
                for (const node_id dep : layers[l - 1])
                {
                    builder.depends_on(node, dep);
                }
            }
        }
        // A second, fully independent 4x4 half that no sink needs.
        std::vector<std::vector<node_id>> dead(4);
        for (int l = 0; l < 4; ++l)
        {
            for (int w = 0; w < 4; ++w)
            {
                const node_id id = builder.add_node(
                    "D" + std::to_string(l) + "_N" + std::to_string(w),
                    [](const std::vector<std::any>& inputs) -> std::any
                    {
                        int sum = 0;
                        for (const auto& in : inputs)
                        {
                            sum += std::any_cast<int>(in);
                        }
                        return sum + 1;
                    });
                dead[l].push_back(id);
            }
        }
        for (int l = 1; l < 4; ++l)
        {
            for (const node_id node : dead[l])
            {
                for (const node_id dep : dead[l - 1])
                {
                    builder.depends_on(node, dep);
                }
            }
        }
        std::shared_ptr<dependency_graph> out;
        if (!builder.build(out).ok())
        {
            return std::shared_ptr<dependency_graph>(nullptr);
        }
        return out;
    }();

    if (!g)
    {
        state.SkipWithError("failed to build graph");
        return;
    }

    // Sink = last node of the live half (id 15).
    const node_id                         sink = 15;
    graph_executor                        executor(graph_executor_options{4});
    std::unordered_map<node_id, std::any> results;

    for (auto _ : state)
    {
        const graph_execution_status status = executor.run_to(*g, {sink}, results);
        if (!status.ok())
        {
            state.SkipWithError("run_to failed");
            break;
        }
        benchmark::ClobberMemory();
    }
    // Only the 16 live-half nodes ran, not all 32.
    state.SetItemsProcessed(state.iterations() * 16);
    state.counters["nodes_total"] = 32.0;
    state.counters["nodes_run"]   = 16.0;
}
BENCHMARK(BM_Graph_RunTo_PrunedHalf);

// Incremental re-run: a wide graph where only ONE source's version moved.
// Measures the dirty-marking + cache-serve win against a full re-run -- the
// "bump one quote, only the dependent slice recomputes" case.
static void BM_Graph_Incremental_OneDirtySource(benchmark::State& state)
{
    // 64 independent sources, each stamped, each feeding its own sink.
    static std::shared_ptr<dependency_graph> g = []()
    {
        graph_builder builder;
        for (int i = 0; i < 64; ++i)
        {
            const node_id src = builder.add_node(
                "src" + std::to_string(i),
                [](const std::vector<std::any>&) -> std::any { return 1; });
            const node_id sink = builder.add_node(
                "sink" + std::to_string(i),
                [](const std::vector<std::any>& in) -> std::any
                { return std::any_cast<int>(in[0]) + 1; });
            builder.depends_on(sink, src);
            builder.with_node_version(src, 1);  // all sources at version 1
        }
        std::shared_ptr<dependency_graph> out;
        if (!builder.build(out).ok())
        {
            return std::shared_ptr<dependency_graph>(nullptr);
        }
        return out;
    }();

    if (!g)
    {
        state.SkipWithError("failed to build graph");
        return;
    }

    // Baseline: every node at its built stamp, so with a warm cache nothing
    // is dirty. To force exactly one source dirty per iteration we pass a
    // baseline that differs on that one node (version differs -> dirty ->
    // it and its sink recompute).
    std::vector<std::uint64_t> baseline(g->node_count(), 0);
    for (node_id id = 0; id < g->node_count(); ++id)
    {
        baseline[id] = g->version(id);
    }

    graph_executor                        executor(graph_executor_options{4});
    std::unordered_map<node_id, std::any> cache;
    // Warm the cache once (full compute).
    if (!executor.run_incremental(*g, {}, cache).ok())
    {
        state.SkipWithError("warm-up run_incremental failed");
        return;
    }

    for (auto _ : state)
    {
        baseline[0]                         = 2;  // source 0's version "moved"
        const graph_execution_status status = executor.run_incremental(*g, baseline, cache);
        baseline[0]                         = 1;  // restore for the next iteration
        if (!status.ok())
        {
            state.SkipWithError("run_incremental failed");
            break;
        }
        benchmark::ClobberMemory();
    }
    // 128 nodes total, but only 2 (source 0 + its sink) recomputed.
    state.SetItemsProcessed(state.iterations() * 2);
    state.counters["nodes_total"]      = 128.0;
    state.counters["nodes_recomputed"] = 2.0;
}
BENCHMARK(BM_Graph_Incremental_OneDirtySource);

}  // namespace graph
