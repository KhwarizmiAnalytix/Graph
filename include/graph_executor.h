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

#pragma once

#include <any>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <parallel.h>

#include "common/graph_export.h"
#include "dependency_graph.h"
#include "graph_types.h"

namespace graph
{

/// Configuration for graph_executor.
///
/// number_of_threads: worker threads in the executor's persistent pool.
///   - 0 (default) resolves to std::thread::hardware_concurrency() (clamped
///     to at least 1) at executor construction.
///   - 1 reproduces the previous default (single worker thread).
///
/// on_node_start / on_node_end: optional, non-owning-style hooks invoked on
/// the worker thread around each node's work_. They are called with the
/// node's id and name. Exceptions thrown by a hook are swallowed so they
/// cannot kill a worker thread; hooks are for observation (e.g. wiring
/// Profiler) and must not mutate graph state. Both default to
/// nullptr (no hook). Set them before run(); they are read once per run().
struct graph_executor_options
{
    int number_of_threads = 0;

    std::function<void(node_id, const std::string&)> on_node_start;
    std::function<void(node_id, const std::string&)> on_node_end;
};

/**
 * @brief Runs a graph's nodes in parallel, respecting dependency order.
 *
 * The scheduler is an autograd-engine-style ready queue, not a per-node
 * future chain. At the start of a run each node gets an atomic
 * `remaining_deps` counter; when a node finishes it decrements each
 * successor's counter, and a successor that hits zero is pushed onto a
 * ready queue. Workers pop ready nodes and run them. This is the shape of
 * PyTorch's autograd engine (torch/csrc/autograd/engine.cpp), minus the
 * per-node shared_future overhead push_dependent would add -- scheduling a
 * node is one atomic decrement and (when it becomes ready) one push, not a
 * shared_future allocation.
 *
 * The ready queue is priority-ordered by each node's bottom-level
 * (critical-path) priority computed at build time, so nodes on a longer
 * chain to a sink are started before side branches on few threads.
 *
 * The worker pool is owned by the executor and reused across run() calls
 * -- constructing threads is the dominant fixed cost of running a small
 * graph, so it is paid once per executor, not once per run(). Actual
 * concurrency is controlled by graph_executor_options::number_of_threads.
 *
 * A node's work_ may throw. graph_executor catches it (instead of letting
 * it escape a worker thread, which would otherwise call std::terminate()),
 * records the first such failure, and skips every node that depends,
 * directly or transitively, on the failed node -- see run()'s status
 * return.
 *
 * Thread safety: a single graph_executor's run() must not be called
 * concurrently from multiple threads. Running two distinct executors, or
 * two distinct graphs on two executors, concurrently is fine.
 */
class GRAPH_VISIBILITY graph_executor
{
public:
    GRAPH_API explicit graph_executor(graph_executor_options options = {});

    /// Runs every node of `g` to completion. `out_results` receives one
    /// entry per node (a default-constructed, empty std::any for any node
    /// skipped due to an upstream failure or a cancellation). Blocks until
    /// the whole graph has finished (or been cancelled).
    GRAPH_API graph_execution_status
    run(const dependency_graph& g, std::unordered_map<node_id, std::any>& out_results);

    /**
     * @brief Runs only the nodes `sinks` (transitively) depend on, and
     * reports only the sinks' results.
     *
     * This is DCE at execution time: nodes no sink needs are never
     * scheduled. On top of that, a result slot is released as soon as its
     * last scheduled consumer has read it (liveness), so peak memory tracks
     * the live set of the pruned graph rather than all nodes. `out_results`
     * receives exactly one entry per sink (empty std::any if that sink was
     * skipped by an upstream failure or cancellation).
     *
     * Returns a failure status if any sink id is out of range.
     */
    GRAPH_API graph_execution_status run_to(
        const dependency_graph&                g,
        const std::vector<node_id>&            sinks,
        std::unordered_map<node_id, std::any>& out_results);

    /**
     * @brief Recomputes only the nodes whose version stamp moved since a
     * baseline, serving the rest from cache.
     *
     * A node is DIRTY -- its work re-runs -- when its version()
     * (graph_builder::with_node_version / keyed_graph_builder::
     * with_key_version) differs from `baseline_versions[id]`, or when any
     * dependency is dirty. A CLEAN node does no work: its entry in
     * `in_out_cache` is kept and forwarded to its dependents.
     *
     * `in_out_cache` is both input and output: on entry it must hold the
     * previous run's results (keyed by node_id; missing entries make their
     * node dirty); on return it holds this run's results for every node
     * (recomputed for dirty nodes, carried over for clean ones).
     *
     * `baseline_versions` is indexed by node_id and should hold the stamps
     * the cache was computed against; entries past the end of the vector
     * count as version 0 (so an empty baseline dirties every stamped node).
     * A node that is clean but missing from the cache is treated as dirty.
     *
     * Failure of a dirty node propagates to its dependents (they are
     * skipped, as in run()); clean nodes are unaffected. A failed/skipped
     * node's result is NOT written back into the cache, so the next run
     * recomputes it rather than serving an empty result.
     *
     * @warning The cache and baseline are keyed by node_id, so they are only
     * meaningful across runs of graphs whose node ids are stable -- i.e. the
     * same builder calls in the same order. Rebuilding a keyed graph after a
     * dependency change can permute resolution order and hence ids; callers
     * doing that must re-snapshot the baseline (and ideally the cache)
     * against the new graph's ids. Stamp versions starting at 1: version 0
     * means "unstamped" and never triggers a version-based recompute.
     */
    GRAPH_API graph_execution_status run_incremental(
        const dependency_graph&                g,
        const std::vector<std::uint64_t>&      baseline_versions,
        std::unordered_map<node_id, std::any>& in_out_cache);

    /**
     * @brief Starts a run without blocking; returns a future for its status.
     *
     * `out_results` is written by the worker threads and must outlive the
     * returned future (it is only safe to read once the future is ready).
     * Call cancel() to request an early stop: nodes not yet started are
     * skipped, in-flight nodes finish, and the returned status is
     * graph_execution_status_code::cancelled.
     */
    GRAPH_API std::future<graph_execution_status> run_async(
        const dependency_graph& g, std::unordered_map<node_id, std::any>& out_results);

    /// Requests cancellation of the in-flight run_async(), if any. Nodes
    /// not yet started are skipped; in-flight nodes run to completion.
    /// No effect if no run is in flight.
    GRAPH_API void cancel();

private:
    // Per-run state, held in one heap block so the worker threads can
    // reference it without dangling once run() returns.
    struct run_state;

    graph_execution_status execute(
        const dependency_graph&                   g,
        std::unordered_map<node_id, std::any>&    out_results,
        const std::shared_ptr<std::atomic<bool>>& cancel_flag,
        const std::vector<node_id>*               sinks,
        const std::vector<char>*                  dirty,
        std::unordered_map<node_id, std::any>*    cache);

    graph_executor_options options_;

    // Persistent worker pool. Declared after options_ so it is destroyed
    // first (its destructor joins the worker threads before options_ and
    // any state the hooks might reference is torn down).
    std::unique_ptr<threaded_callback_queue> queue_;

    // Cancellation flag for the in-flight run_async(), if any. Replaced on
    // each run_async() call.
    std::shared_ptr<std::atomic<bool>> cancel_flag_;
    std::mutex                         cancel_mutex_;
};

}  // namespace graph
