/*
 * XSigma: High-Performance Computational Library
 *
 * SPDX-License-Identifier: GPL-3.0-or-later OR Commercial
 *
 * This file is part of XSigma and is licensed under a dual-license model:
 *
 *   - Open-source License (GPLv3):
 *       Free for personal, academic, and research use under the terms of
 *       the GNU General Public License v3.0 or later.
 *
 *   - Commercial License:
 *       A commercial license is required for proprietary, closed-source,
 *       or SaaS usage. Contact us to obtain a commercial agreement.
 *
 * Contact: licensing@xsigma.co.uk
 * Website: https://www.xsigma.co.uk
 */

#include <any>
#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "GraphTest.h"
#include "dependency_graph.h"
#include "graph_builder.h"
#include "graph_executor.h"
#include "graph_passes.h"
#include "graph_types.h"
#include "keyed_graph_builder.h"

using namespace graph;

namespace
{

// Builds a graph with a live branch (source -> mid -> sink) and a dead
// branch (dead_source -> dead_mid) that no requested sink depends on.
// Returns the graph and the node ids by role.
struct branch_graph
{
    std::shared_ptr<dependency_graph> g;
    node_id                           source;
    node_id                           mid;
    node_id                           sink;
    node_id                           dead_source;
    node_id                           dead_mid;
    std::atomic<int>*                 dead_ran;  // owned by caller
};

branch_graph build_branch_graph(std::atomic<int>& dead_ran)
{
    branch_graph out;
    out.dead_ran = &dead_ran;

    graph_builder builder;
    out.source =
        builder.add_node("source", [](const std::vector<std::any>&) -> std::any { return 2; });
    out.mid = builder.add_node(
        "mid",
        [](const std::vector<std::any>& in) -> std::any { return std::any_cast<int>(in[0]) * 10; });
    out.sink = builder.add_node(
        "sink",
        [](const std::vector<std::any>& in) -> std::any { return std::any_cast<int>(in[0]) + 1; });
    out.dead_source = builder.add_node(
        "dead_source",
        [&dead_ran](const std::vector<std::any>&) -> std::any
        {
            ++dead_ran;
            return 99;
        });
    out.dead_mid = builder.add_node(
        "dead_mid",
        [&dead_ran](const std::vector<std::any>& in) -> std::any
        {
            ++dead_ran;
            return std::any_cast<int>(in[0]);
        });

    builder.depends_on(out.mid, out.source)
        .depends_on(out.sink, out.mid)
        .depends_on(out.dead_mid, out.dead_source);

    if (!builder.build(out.g).ok())
    {
        out.g = nullptr;
    }
    return out;
}

}  // namespace

// ancestor_set marks the sink and its transitive dependencies, and nothing
// from the dead branch.
TEST(GraphPasses, ancestor_set_marks_only_live_branch)
{
    std::atomic<int>   dead_ran{0};
    const branch_graph bg = build_branch_graph(dead_ran);
    ASSERT_NE(bg.g, nullptr);

    const std::vector<char> keep = graph_passes::ancestor_set(*bg.g, {bg.sink});

    ASSERT_EQ(keep.size(), bg.g->node_count());
    EXPECT_TRUE(keep[bg.sink]);
    EXPECT_TRUE(keep[bg.mid]);
    EXPECT_TRUE(keep[bg.source]);
    EXPECT_FALSE(keep[bg.dead_source]);
    EXPECT_FALSE(keep[bg.dead_mid]);
}

// prune_to_sinks drops the dead branch and remaps ids densely; the pruned
// graph still computes the sink correctly.
TEST(GraphPasses, prune_to_sinks_drops_dead_branch)
{
    std::atomic<int>   dead_ran{0};
    const branch_graph bg = build_branch_graph(dead_ran);
    ASSERT_NE(bg.g, nullptr);

    std::vector<node_id>              old_to_new;
    std::shared_ptr<dependency_graph> pruned =
        graph_passes::prune_to_sinks(*bg.g, {bg.sink}, old_to_new);

    ASSERT_NE(pruned, nullptr);
    EXPECT_EQ(pruned->node_count(), 3U);  // source, mid, sink -- dead branch gone

    // Dropped nodes map to npos; kept nodes map densely.
    EXPECT_EQ(old_to_new[bg.dead_source], graph_passes::npos);
    EXPECT_EQ(old_to_new[bg.dead_mid], graph_passes::npos);
    const node_id new_sink = old_to_new[bg.sink];
    ASSERT_NE(new_sink, graph_passes::npos);

    // The pruned graph executes to the same sink value, and the dead branch
    // never runs.
    graph_executor                        executor;
    std::unordered_map<node_id, std::any> results;
    ASSERT_TRUE(executor.run(*pruned, results).ok());
    EXPECT_EQ(std::any_cast<int>(results.at(new_sink)), 21);
    EXPECT_EQ(dead_ran.load(), 0);
}

// An out-of-range sink id is a graceful failure, not a crash.
TEST(GraphPasses, prune_to_sinks_rejects_out_of_range_sink)
{
    std::atomic<int>   dead_ran{0};
    const branch_graph bg = build_branch_graph(dead_ran);
    ASSERT_NE(bg.g, nullptr);

    std::vector<node_id>              old_to_new;
    std::shared_ptr<dependency_graph> pruned =
        graph_passes::prune_to_sinks(*bg.g, {bg.g->node_count() + 10}, old_to_new);
    EXPECT_EQ(pruned, nullptr);
    EXPECT_TRUE(old_to_new.empty());
}

// run_to only schedules the sink's ancestors: the dead branch's work never
// runs, and only the sink is reported.
TEST(GraphExecutor, run_to_skips_dead_branch_and_reports_only_sinks)
{
    std::atomic<int>   dead_ran{0};
    const branch_graph bg = build_branch_graph(dead_ran);
    ASSERT_NE(bg.g, nullptr);

    graph_executor                        executor;
    std::unordered_map<node_id, std::any> results;
    const graph_execution_status          status = executor.run_to(*bg.g, {bg.sink}, results);

    ASSERT_TRUE(status.ok());
    EXPECT_EQ(dead_ran.load(), 0);  // dead branch never scheduled
    ASSERT_EQ(results.size(), 1U);  // only the sink is reported
    EXPECT_EQ(std::any_cast<int>(results.at(bg.sink)), 21);
}

// run_to with multiple sinks runs the union of their ancestors and reports
// each sink.
TEST(GraphExecutor, run_to_multiple_sinks)
{
    std::atomic<int>   dead_ran{0};
    const branch_graph bg = build_branch_graph(dead_ran);
    ASSERT_NE(bg.g, nullptr);

    graph_executor                        executor;
    std::unordered_map<node_id, std::any> results;
    const graph_execution_status status = executor.run_to(*bg.g, {bg.sink, bg.dead_mid}, results);

    ASSERT_TRUE(status.ok());
    // Now the "dead" branch IS a requested sink, so it runs.
    EXPECT_EQ(dead_ran.load(), 2);
    ASSERT_EQ(results.size(), 2U);
    EXPECT_EQ(std::any_cast<int>(results.at(bg.sink)), 21);
    EXPECT_EQ(std::any_cast<int>(results.at(bg.dead_mid)), 99);
}

// run_to rejects an out-of-range sink id.
TEST(GraphExecutor, run_to_rejects_out_of_range_sink)
{
    std::atomic<int>   dead_ran{0};
    const branch_graph bg = build_branch_graph(dead_ran);
    ASSERT_NE(bg.g, nullptr);

    graph_executor                        executor;
    std::unordered_map<node_id, std::any> results;
    const graph_execution_status status = executor.run_to(*bg.g, {bg.g->node_count() + 5}, results);
    EXPECT_FALSE(status.ok());
}

// Liveness: a non-sink intermediate result is released once its last
// consumer has read it. Observed via a payload holding a shared control
// block; the block's use_count returns to baseline only if the slot (and
// every copy of the payload) is truly freed.
TEST(GraphExecutor, run_to_releases_intermediate_slots)
{
    auto control = std::make_shared<int>(0);

    graph_builder builder;
    // source produces a payload holding the shared control block.
    const node_id source = builder.add_node(
        "source", [control](const std::vector<std::any>&) -> std::any { return control; });
    // mid consumes it (the slot can be freed once mid has read its input).
    const node_id mid = builder.add_node(
        "mid",
        [](const std::vector<std::any>& in) -> std::any
        {
            (void)in;
            return 7;
        });
    builder.depends_on(mid, source);

    std::shared_ptr<dependency_graph> g;
    ASSERT_TRUE(builder.build(g).ok());

    // Baseline: only the test's own reference.
    const long baseline = control.use_count();

    graph_executor                        executor;
    std::unordered_map<node_id, std::any> results;
    ASSERT_TRUE(executor.run_to(*g, {mid}, results).ok());

    // The intermediate `source` slot (and the transient input copy) were
    // released by liveness, so no extra reference to the control block
    // remains.
    EXPECT_EQ(control.use_count(), baseline);
    EXPECT_EQ(std::any_cast<int>(results.at(mid)), 7);
}

// Liveness under fan-out: an intermediate with TWO scheduled consumers must
// be freed only after BOTH have read it. This exercises the concurrent
// decrement path (siblings run on different workers).
TEST(GraphExecutor, run_to_releases_fanout_intermediate_after_both_consumers)
{
    auto control = std::make_shared<int>(0);

    graph_builder builder;
    const node_id source = builder.add_node(
        "source", [control](const std::vector<std::any>&) -> std::any { return control; });
    // Two independent consumers of the same intermediate.
    const node_id left = builder.add_node(
        "left",
        [](const std::vector<std::any>& in) -> std::any
        {
            (void)in;
            return 1;
        });
    const node_id right = builder.add_node(
        "right",
        [](const std::vector<std::any>& in) -> std::any
        {
            (void)in;
            return 2;
        });
    builder.depends_on(left, source).depends_on(right, source);

    std::shared_ptr<dependency_graph> g;
    ASSERT_TRUE(builder.build(g).ok());

    const long baseline = control.use_count();

    // Multi-threaded so the two consumers can run concurrently.
    graph_executor                        executor(graph_executor_options{4});
    std::unordered_map<node_id, std::any> results;
    ASSERT_TRUE(executor.run_to(*g, {left, right}, results).ok());

    // source's slot was freed after both consumers read it; no extra
    // reference to the control block remains.
    EXPECT_EQ(control.use_count(), baseline);
    EXPECT_EQ(std::any_cast<int>(results.at(left)), 1);
    EXPECT_EQ(std::any_cast<int>(results.at(right)), 2);
}

// ---------------------------------------------------------------------------
// Incremental re-run (run_incremental)
// ---------------------------------------------------------------------------

namespace
{

// A small market-style graph: two independent quotes feed a combined node.
// Each quote node counts how many times its work actually ran (via shared
// counters captured by value), so a test can tell "recomputed" apart from
// "served from cache".
struct quote_graph
{
    std::shared_ptr<dependency_graph> g;
    node_id                           quote_a;
    node_id                           quote_b;
    node_id                           combined;
    std::shared_ptr<std::atomic<int>> ran_a        = std::make_shared<std::atomic<int>>(0);
    std::shared_ptr<std::atomic<int>> ran_b        = std::make_shared<std::atomic<int>>(0);
    std::shared_ptr<std::atomic<int>> ran_combined = std::make_shared<std::atomic<int>>(0);
};

quote_graph build_quote_graph(std::uint64_t version_a, std::uint64_t version_b)
{
    quote_graph qg;

    graph_builder builder;
    qg.quote_a = builder.add_node(
        "quote_a",
        [ran = qg.ran_a](const std::vector<std::any>&) -> std::any
        {
            ++(*ran);
            return 10;
        });
    qg.quote_b = builder.add_node(
        "quote_b",
        [ran = qg.ran_b](const std::vector<std::any>&) -> std::any
        {
            ++(*ran);
            return 20;
        });
    qg.combined = builder.add_node(
        "combined",
        [ran = qg.ran_combined](const std::vector<std::any>& in) -> std::any
        {
            ++(*ran);
            return std::any_cast<int>(in[0]) + std::any_cast<int>(in[1]);
        });
    builder.depends_on(qg.combined, qg.quote_a).depends_on(qg.combined, qg.quote_b);

    // Stamp only the input quotes; `combined` stays unstamped (version 0) and
    // recomputes whenever an input is dirty -- the correct default.
    builder.with_node_version(qg.quote_a, version_a);
    builder.with_node_version(qg.quote_b, version_b);

    if (!builder.build(qg.g).ok())
    {
        qg.g = nullptr;
    }
    return qg;
}

// Snapshot the per-node version stamps of a built graph (the baseline a
// caller diffs the next run against).
std::vector<std::uint64_t> snapshot_versions(const dependency_graph& g)
{
    std::vector<std::uint64_t> v(g.node_count(), 0);
    for (node_id id = 0; id < g.node_count(); ++id)
    {
        v[id] = g.version(id);
    }
    return v;
}

}  // namespace

// First incremental run with an empty cache: everything is dirty (no cached
// results to serve), so all nodes compute.
TEST(GraphExecutor, run_incremental_first_run_computes_everything)
{
    quote_graph qg = build_quote_graph(1, 1);
    ASSERT_NE(qg.g, nullptr);

    graph_executor                        executor;
    std::unordered_map<node_id, std::any> cache;  // empty -> all dirty
    ASSERT_TRUE(executor.run_incremental(*qg.g, {}, cache).ok());

    EXPECT_EQ(qg.ran_a->load(), 1);
    EXPECT_EQ(qg.ran_b->load(), 1);
    EXPECT_EQ(qg.ran_combined->load(), 1);
    EXPECT_EQ(std::any_cast<int>(cache.at(qg.combined)), 30);
}

// The quant win: bump ONE quote's version, and only it plus its downstream
// dependent recompute; the untouched quote is served from cache.
TEST(GraphExecutor, run_incremental_bump_one_quote_recomputes_only_dependents)
{
    quote_graph qg = build_quote_graph(1, 1);
    ASSERT_NE(qg.g, nullptr);

    graph_executor                        executor;
    std::unordered_map<node_id, std::any> cache;
    ASSERT_TRUE(executor.run_incremental(*qg.g, {}, cache).ok());
    const std::vector<std::uint64_t> baseline = snapshot_versions(*qg.g);
    EXPECT_EQ(qg.ran_a->load(), 1);
    EXPECT_EQ(qg.ran_b->load(), 1);

    // Bump quote_a's version (a new market print). quote_b is unchanged.
    // Rebuild the graph with the new stamp -- the graph is frozen, so a new
    // stamp means a new build (keyed_graph_builder does this per resolve).
    quote_graph qg2 = build_quote_graph(/*version_a=*/2, /*version_b=*/1);
    ASSERT_NE(qg2.g, nullptr);

    ASSERT_TRUE(executor.run_incremental(*qg2.g, baseline, cache).ok());

    // quote_a recomputed (version moved); combined recomputed (downstream of
    // a dirty node); quote_b did NOT recompute (clean, served from cache).
    EXPECT_EQ(qg2.ran_a->load(), 1);
    EXPECT_EQ(qg2.ran_b->load(), 0);  // served from cache
    EXPECT_EQ(qg2.ran_combined->load(), 1);
    EXPECT_EQ(std::any_cast<int>(cache.at(qg2.combined)), 30);
}

// Nothing bumped: a second incremental run against the same baseline does no
// work at all -- every node is clean and served from cache.
TEST(GraphExecutor, run_incremental_nothing_changed_does_no_work)
{
    quote_graph qg = build_quote_graph(1, 1);
    ASSERT_NE(qg.g, nullptr);

    graph_executor                        executor;
    std::unordered_map<node_id, std::any> cache;
    ASSERT_TRUE(executor.run_incremental(*qg.g, {}, cache).ok());
    const std::vector<std::uint64_t> baseline = snapshot_versions(*qg.g);

    // Second run, same graph, same baseline: nothing dirty.
    ASSERT_TRUE(executor.run_incremental(*qg.g, baseline, cache).ok());

    EXPECT_EQ(qg.ran_a->load(), 1);  // still 1 from the first run
    EXPECT_EQ(qg.ran_b->load(), 1);
    EXPECT_EQ(qg.ran_combined->load(), 1);  // no recompute
    EXPECT_EQ(std::any_cast<int>(cache.at(qg.combined)), 30);
}

// A clean node missing from the cache is treated as dirty (it has nothing to
// serve), so it recomputes even though its version didn't move.
TEST(GraphExecutor, run_incremental_missing_cache_entry_recomputes)
{
    quote_graph qg = build_quote_graph(1, 1);
    ASSERT_NE(qg.g, nullptr);

    graph_executor                        executor;
    std::unordered_map<node_id, std::any> cache;
    ASSERT_TRUE(executor.run_incremental(*qg.g, {}, cache).ok());
    const std::vector<std::uint64_t> baseline = snapshot_versions(*qg.g);

    // Evict quote_a's cached result; versions unchanged.
    cache.erase(qg.quote_a);

    ASSERT_TRUE(executor.run_incremental(*qg.g, baseline, cache).ok());

    // quote_a recomputed (no cache to serve), and combined downstream of it.
    EXPECT_EQ(qg.ran_a->load(), 2);
    EXPECT_EQ(qg.ran_b->load(), 1);  // still cached
    EXPECT_EQ(qg.ran_combined->load(), 2);
}

// Multi-hop propagation: bumping A in A -> B -> C recomputes all three, even
// though only A's version moved (B and C are dirty by transitivity).
TEST(GraphExecutor, run_incremental_propagates_dirty_multiple_hops)
{
    auto ran_a = std::make_shared<std::atomic<int>>(0);
    auto ran_b = std::make_shared<std::atomic<int>>(0);
    auto ran_c = std::make_shared<std::atomic<int>>(0);

    graph_builder builder;
    const node_id a = builder.add_node(
        "a",
        [ran_a](const std::vector<std::any>&) -> std::any
        {
            ++(*ran_a);
            return 1;
        });
    const node_id b = builder.add_node(
        "b",
        [ran_b](const std::vector<std::any>& in) -> std::any
        {
            ++(*ran_b);
            return std::any_cast<int>(in[0]) + 1;
        });
    const node_id c = builder.add_node(
        "c",
        [ran_c](const std::vector<std::any>& in) -> std::any
        {
            ++(*ran_c);
            return std::any_cast<int>(in[0]) + 1;
        });
    builder.depends_on(b, a).depends_on(c, b);
    builder.with_node_version(a, 1);  // only A is stamped

    std::shared_ptr<dependency_graph> g;
    ASSERT_TRUE(builder.build(g).ok());

    graph_executor                        executor;
    std::unordered_map<node_id, std::any> cache;
    ASSERT_TRUE(executor.run_incremental(*g, {}, cache).ok());
    EXPECT_EQ(ran_a->load(), 1);
    EXPECT_EQ(ran_b->load(), 1);
    EXPECT_EQ(ran_c->load(), 1);

    // Bump A's version via a baseline that disagrees on A only.
    std::vector<std::uint64_t> baseline = snapshot_versions(*g);
    baseline[a]                         = 99;  // graph says 1, baseline says 99 -> A dirty
    ASSERT_TRUE(executor.run_incremental(*g, baseline, cache).ok());

    // A recomputed (version moved); B and C recomputed (transitively dirty).
    EXPECT_EQ(ran_a->load(), 2);
    EXPECT_EQ(ran_b->load(), 2);
    EXPECT_EQ(ran_c->load(), 2);
    EXPECT_EQ(std::any_cast<int>(cache.at(c)), 3);
}

// Fan-out dirty: one bumped source dirties BOTH of its dependents.
TEST(GraphExecutor, run_incremental_dirty_source_dirties_all_dependents)
{
    auto ran_src   = std::make_shared<std::atomic<int>>(0);
    auto ran_left  = std::make_shared<std::atomic<int>>(0);
    auto ran_right = std::make_shared<std::atomic<int>>(0);

    graph_builder builder;
    const node_id src = builder.add_node(
        "src",
        [ran_src](const std::vector<std::any>&) -> std::any
        {
            ++(*ran_src);
            return 5;
        });
    const node_id left = builder.add_node(
        "left",
        [ran_left](const std::vector<std::any>& in) -> std::any
        {
            ++(*ran_left);
            return std::any_cast<int>(in[0]) * 2;
        });
    const node_id right = builder.add_node(
        "right",
        [ran_right](const std::vector<std::any>& in) -> std::any
        {
            ++(*ran_right);
            return std::any_cast<int>(in[0]) * 3;
        });
    builder.depends_on(left, src).depends_on(right, src);
    builder.with_node_version(src, 1);

    std::shared_ptr<dependency_graph> g;
    ASSERT_TRUE(builder.build(g).ok());

    graph_executor                        executor;
    std::unordered_map<node_id, std::any> cache;
    ASSERT_TRUE(executor.run_incremental(*g, {}, cache).ok());

    std::vector<std::uint64_t> baseline = snapshot_versions(*g);
    baseline[src]                       = 2;  // bump the shared source
    ASSERT_TRUE(executor.run_incremental(*g, baseline, cache).ok());

    EXPECT_EQ(ran_src->load(), 2);
    EXPECT_EQ(ran_left->load(), 2);  // both dependents recomputed
    EXPECT_EQ(ran_right->load(), 2);
    EXPECT_EQ(std::any_cast<int>(cache.at(left)), 10);
    EXPECT_EQ(std::any_cast<int>(cache.at(right)), 15);
}

// Failure-then-rerun: a dirty node that throws does NOT poison the cache --
// its entry is dropped, so the next run recomputes it instead of serving an
// empty result with an ok status.
TEST(GraphExecutor, run_incremental_failure_does_not_poison_cache)
{
    auto ran_bad = std::make_shared<std::atomic<int>>(0);
    auto fail    = std::make_shared<std::atomic<bool>>(true);

    graph_builder builder;
    const node_id good =
        builder.add_node("good", [](const std::vector<std::any>&) -> std::any { return 1; });
    const node_id bad = builder.add_node(
        "bad",
        [ran_bad, fail](const std::vector<std::any>& in) -> std::any
        {
            ++(*ran_bad);
            if (fail->load())
            {
                throw std::runtime_error("bad node failed");
            }
            return std::any_cast<int>(in[0]) + 1;
        });
    builder.depends_on(bad, good);
    builder.with_node_version(good, 1);
    builder.with_node_version(bad, 1);

    std::shared_ptr<dependency_graph> g;
    ASSERT_TRUE(builder.build(g).ok());

    graph_executor                        executor;
    std::unordered_map<node_id, std::any> cache;

    // First run: bad throws. Status is a failure.
    EXPECT_FALSE(executor.run_incremental(*g, {}, cache).ok());
    EXPECT_EQ(ran_bad->load(), 1);
    // The failed node's result must NOT be cached.
    EXPECT_EQ(cache.find(bad), cache.end());

    // Fix the node and rerun with the same versions: bad is dirty again
    // (its entry was dropped, so no_cache), recomputes, and succeeds.
    fail->store(false);
    const std::vector<std::uint64_t> baseline = snapshot_versions(*g);
    ASSERT_TRUE(executor.run_incremental(*g, baseline, cache).ok());
    EXPECT_EQ(ran_bad->load(), 2);  // recomputed, not served from cache
    EXPECT_EQ(std::any_cast<int>(cache.at(bad)), 2);
}

// keyed_graph_builder's with_key_version / key_version stamp the resolved
// node and read it back.
TEST(KeyedGraphBuilder, key_version_stamps_and_reads_back)
{
    keyed_graph_builder<std::string> builder;

    const keyed_graph_builder<std::string>::resolver_fn resolver =
        [](const std::string& key,
           keyed_graph_builder<std::string>&,
           std::string& out_name,
           node_work&   out_work) -> graph_status
    {
        out_name = key;
        out_work = [key](const std::vector<std::any>&) -> std::any { return key; };
        return graph_status::success();
    };

    node_id id = 0;
    ASSERT_TRUE(builder.resolve("quote_a", resolver, id).ok());

    // Unresolved key: with_key_version fails, key_version reads 0.
    EXPECT_FALSE(builder.with_key_version("nope", 5));
    EXPECT_EQ(builder.key_version("nope"), 0);

    // Resolved key: stamp and read back.
    EXPECT_TRUE(builder.with_key_version("quote_a", 7));
    EXPECT_EQ(builder.key_version("quote_a"), 7);

    // The stamp survives into the built graph.
    std::shared_ptr<dependency_graph> g;
    ASSERT_TRUE(builder.build(g).ok());
    EXPECT_EQ(g->version(id), 7);
}
