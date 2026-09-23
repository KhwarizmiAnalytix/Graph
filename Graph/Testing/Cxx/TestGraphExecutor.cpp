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
#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "GraphTest.h"
#include "dependency_graph.h"
#include "graph_builder.h"
#include "graph_executor.h"
#include "graph_types.h"

using namespace graph;

// Diamond graph: A produces 2, B = A*10, C = A*100, D = B + C.
// Exercises both the fan-out (B and C both depend on A) and fan-in (D
// depends on both B and C, in the order they were declared) data flow.
TEST(GraphExecutor, diamond_graph_combines_dependency_results)
{
    graph_builder builder;

    const node_id a =
        builder.add_node("A", [](const std::vector<std::any>&) -> std::any { return 2; });
    const node_id b = builder.add_node(
        "B",
        [](const std::vector<std::any>& inputs) -> std::any
        { return std::any_cast<int>(inputs[0]) * 10; });
    const node_id c = builder.add_node(
        "C",
        [](const std::vector<std::any>& inputs) -> std::any
        { return std::any_cast<int>(inputs[0]) * 100; });
    const node_id d = builder.add_node(
        "D",
        [](const std::vector<std::any>& inputs) -> std::any
        { return std::any_cast<int>(inputs[0]) + std::any_cast<int>(inputs[1]); });

    builder.depends_on(b, a).depends_on(c, a).depends_on(d, b).depends_on(d, c);

    std::shared_ptr<dependency_graph> g;
    ASSERT_TRUE(builder.build(g).ok());

    graph_executor                        executor;
    std::unordered_map<node_id, std::any> results;
    const graph_execution_status          status = executor.run(*g, results);

    ASSERT_TRUE(status.ok());
    ASSERT_EQ(results.size(), 4U);
    EXPECT_EQ(std::any_cast<int>(results.at(a)), 2);
    EXPECT_EQ(std::any_cast<int>(results.at(b)), 20);
    EXPECT_EQ(std::any_cast<int>(results.at(c)), 200);
    EXPECT_EQ(std::any_cast<int>(results.at(d)), 220);
}

// D depends on C then B (depends_on(d, c) called before depends_on(d, b)),
// even though B was add_node()'d before C (b < c as node ids). D's inputs
// must arrive in declared depends_on() order -- [C's result, B's result] --
// not in ascending dependency node_id order. Regression test for the input-
// ordering bug caught in review: graph_executor used to rebuild each node's
// dependency list by scanning ids 0..count-1, which silently reversed this
// case.
TEST(GraphExecutor, dependency_inputs_arrive_in_declared_order)
{
    graph_builder builder;

    const node_id a = builder.add_node(
        "A", [](const std::vector<std::any>&) -> std::any { return std::string("unused"); });
    const node_id b = builder.add_node(
        "B", [](const std::vector<std::any>&) -> std::any { return std::string("B"); });
    const node_id c = builder.add_node(
        "C", [](const std::vector<std::any>&) -> std::any { return std::string("C"); });
    const node_id d = builder.add_node(
        "D",
        [](const std::vector<std::any>& inputs) -> std::any
        { return std::any_cast<std::string>(inputs[0]) + std::any_cast<std::string>(inputs[1]); });

    (void)a;
    builder.depends_on(d, c).depends_on(d, b);  // declared C-then-B, not id order

    std::shared_ptr<dependency_graph> g;
    ASSERT_TRUE(builder.build(g).ok());

    graph_executor                        executor;
    std::unordered_map<node_id, std::any> results;
    ASSERT_TRUE(executor.run(*g, results).ok());

    EXPECT_EQ(std::any_cast<std::string>(results.at(d)), "CB");
}

// A set of independent (no edges between them) nodes should all run and
// produce correct, independent results -- no assertion on wall-clock
// overlap, only on correctness (timing assertions are flaky under CI).
TEST(GraphExecutor, independent_nodes_all_run_correctly)
{
    graph_builder        builder;
    std::vector<node_id> ids;
    constexpr int        kNodeCount = 16;
    for (int i = 0; i < kNodeCount; ++i)
    {
        ids.push_back(builder.add_node(
            "n" + std::to_string(i),
            [i](const std::vector<std::any>&) -> std::any { return i * i; }));
    }

    std::shared_ptr<dependency_graph> g;
    ASSERT_TRUE(builder.build(g).ok());

    graph_executor                        executor;
    std::unordered_map<node_id, std::any> results;
    ASSERT_TRUE(executor.run(*g, results).ok());

    ASSERT_EQ(results.size(), static_cast<std::size_t>(kNodeCount));
    for (int i = 0; i < kNodeCount; ++i)
    {
        EXPECT_EQ(std::any_cast<int>(results.at(ids[static_cast<std::size_t>(i)])), i * i);
    }
}

// Same independent-node graph, run with a multi-threaded executor: results
// must stay correct regardless of how many worker threads actually run it.
TEST(GraphExecutor, multi_threaded_executor_still_produces_correct_results)
{
    graph_builder        builder;
    std::vector<node_id> ids;
    constexpr int        kNodeCount = 16;
    for (int i = 0; i < kNodeCount; ++i)
    {
        ids.push_back(builder.add_node(
            "n" + std::to_string(i),
            [i](const std::vector<std::any>&) -> std::any { return i * i; }));
    }

    std::shared_ptr<dependency_graph> g;
    ASSERT_TRUE(builder.build(g).ok());

    graph_executor                        executor(graph_executor_options{/*number_of_threads=*/8});
    std::unordered_map<node_id, std::any> results;
    ASSERT_TRUE(executor.run(*g, results).ok());

    ASSERT_EQ(results.size(), static_cast<std::size_t>(kNodeCount));
    for (int i = 0; i < kNodeCount; ++i)
    {
        EXPECT_EQ(std::any_cast<int>(results.at(ids[static_cast<std::size_t>(i)])), i * i);
    }
}

TEST(GraphExecutor, single_node_graph_runs)
{
    graph_builder builder;
    const node_id a = builder.add_node(
        "A", [](const std::vector<std::any>&) -> std::any { return std::string("done"); });

    std::shared_ptr<dependency_graph> g;
    ASSERT_TRUE(builder.build(g).ok());

    graph_executor                        executor;
    std::unordered_map<node_id, std::any> results;
    ASSERT_TRUE(executor.run(*g, results).ok());

    ASSERT_EQ(results.size(), 1U);
    EXPECT_EQ(std::any_cast<std::string>(results.at(a)), "done");
}

// A throwing node is reported through the returned status, identified by
// node id, instead of escaping the worker thread (which would otherwise
// call std::terminate() -- see graph_executor.h's class doc comment).
TEST(GraphExecutor, throwing_node_reports_node_failed)
{
    graph_builder builder;
    const node_id a = builder.add_node(
        "A", [](const std::vector<std::any>&) -> std::any { throw std::runtime_error("boom"); });

    std::shared_ptr<dependency_graph> g;
    ASSERT_TRUE(builder.build(g).ok());

    graph_executor                        executor;
    std::unordered_map<node_id, std::any> results;
    const graph_execution_status          status = executor.run(*g, results);

    ASSERT_FALSE(status.ok());
    EXPECT_EQ(status.code(), graph_execution_status_code::node_failed);
    EXPECT_EQ(status.failed_node(), a);
    EXPECT_NE(status.message().find("boom"), std::string::npos);
}

// A node depending on a failed node must not run its own work_ at all.
TEST(GraphExecutor, dependent_of_failed_node_is_skipped)
{
    graph_builder    builder;
    std::atomic<int> dependent_run_count{0};

    const node_id a = builder.add_node(
        "A", [](const std::vector<std::any>&) -> std::any { throw std::runtime_error("boom"); });
    const node_id b = builder.add_node(
        "B",
        [&dependent_run_count](const std::vector<std::any>&) -> std::any
        {
            ++dependent_run_count;
            return {};
        });
    builder.depends_on(b, a);

    std::shared_ptr<dependency_graph> g;
    ASSERT_TRUE(builder.build(g).ok());

    graph_executor                        executor;
    std::unordered_map<node_id, std::any> results;
    const graph_execution_status          status = executor.run(*g, results);

    ASSERT_FALSE(status.ok());
    EXPECT_EQ(status.failed_node(), a);
    EXPECT_EQ(dependent_run_count.load(), 0);
    // Both nodes still get an entry (empty) in the results map.
    ASSERT_EQ(results.size(), 2U);
}

// Cancel after the source has entered work_ but before it returns, so the
// dependent is still unstarted and the status reports cancelled. Handshake
// instead of sleeping: a wall-clock wait can lose the race when the test
// thread is delayed past the source node's finish.
TEST(GraphExecutor, cancel_skips_unstarted_nodes)
{
    graph_builder builder;

    std::atomic<int>          started{0};
    std::mutex                mu;
    std::condition_variable   cv;
    bool                      slow_entered = false;
    bool                      slow_release = false;

    const node_id slow = builder.add_node(
        "slow",
        [&](const std::vector<std::any>&) -> std::any
        {
            ++started;
            {
                const std::lock_guard<std::mutex> lock(mu);
                slow_entered = true;
            }
            cv.notify_all();
            {
                std::unique_lock<std::mutex> lock(mu);
                cv.wait(lock, [&] { return slow_release; });
            }
            return 1;
        });
    const node_id dependent = builder.add_node(
        "dependent",
        [&started](const std::vector<std::any>&) -> std::any
        {
            ++started;
            return 2;
        });
    builder.depends_on(dependent, slow);

    std::shared_ptr<dependency_graph> g;
    ASSERT_TRUE(builder.build(g).ok());

    graph_executor                        executor(graph_executor_options{/*number_of_threads=*/2});
    std::unordered_map<node_id, std::any> results;
    std::future<graph_execution_status>   future = executor.run_async(*g, results);

    // Unblock the source even if an assertion fires, so the worker cannot stay
    // parked on the condition variable while the future destructor waits.
    struct release_guard
    {
        std::mutex&              mu;
        std::condition_variable& cv;
        bool&                    slow_release;
        ~release_guard()
        {
            {
                const std::lock_guard<std::mutex> lock(mu);
                slow_release = true;
            }
            cv.notify_all();
        }
    };

    {
        release_guard guard{mu, cv, slow_release};
        {
            std::unique_lock<std::mutex> lock(mu);
            ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(5), [&] { return slow_entered; }));
        }
        executor.cancel();
    }

    const graph_execution_status status = future.get();
    ASSERT_FALSE(status.ok());
    EXPECT_EQ(status.code(), graph_execution_status_code::cancelled);
    // The slow node ran; the dependent was skipped.
    EXPECT_EQ(started.load(), 1);
}

// run_async returns immediately and the blocking wait happens on the future.
TEST(GraphExecutor, run_async_completes_with_results)
{
    graph_builder builder;
    const node_id a =
        builder.add_node("A", [](const std::vector<std::any>&) -> std::any { return 42; });

    std::shared_ptr<dependency_graph> g;
    ASSERT_TRUE(builder.build(g).ok());

    graph_executor                        executor;
    std::unordered_map<node_id, std::any> results;
    std::future<graph_execution_status>   future = executor.run_async(*g, results);

    const graph_execution_status status = future.get();
    ASSERT_TRUE(status.ok());
    ASSERT_EQ(results.size(), 1U);
    EXPECT_EQ(std::any_cast<int>(results.at(a)), 42);
}
