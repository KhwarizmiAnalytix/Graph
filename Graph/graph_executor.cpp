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

#include "graph_executor.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <thread>
#include <utility>

#include "graph_passes.h"

namespace graph
{

namespace
{

/// Resolves graph_executor_options::number_of_threads: 0 means "use the
/// hardware", clamped to at least one worker.
int resolve_thread_count(int requested)
{
    if (requested > 0)
    {
        return requested;
    }
    const unsigned int hw = std::thread::hardware_concurrency();
    return hw == 0 ? 1 : static_cast<int>(hw);
}

/// Runs an observation hook, swallowing any exception it throws so a bad
/// hook cannot kill a worker thread. Kept as a named function (rather than
/// an inline try/catch at each call site) so the intentional empty catch is
/// suppressed in exactly one place.
void run_hook_safely(
    const std::function<void(node_id, const std::string&)>& hook,
    node_id                                                 id,
    const std::string&                                      name)
{
    if (!hook)
    {
        return;
    }
    try
    {
        hook(id, name);
    }
    catch (const std::exception& e)
    {
        // Observation hooks must not kill a worker thread; swallowing is
        // the intended behavior. Reference the exception so the catch is
        // not empty (bugprone-empty-catch).
        (void)e;
    }
    catch (...)
    {
        // Non-std::exception throw from a hook; same swallowing rationale.
        // A no-op statement keeps this catch non-empty for the linter.
        int swallowed = 0;
        (void)swallowed;
    }
}

}  // namespace

// Per-run state. One heap block referenced by every worker lambda for the
// duration of a run; run() blocks until completion, so it outlives them.
struct graph_executor::run_state
{
    run_state(
        const dependency_graph&                graph,
        const std::vector<char>*               sched,
        const std::vector<node_id>*            sink_list,
        const std::vector<char>*               dirty_set,
        std::unordered_map<node_id, std::any>* cache_map)
        : g(graph), remaining(graph.node_count())
    {
        const std::size_t count = g.node_count();
        results.resize(count);
        failed.assign(count, 0);

        if (dirty_set != nullptr)
        {
            // Incremental run: schedule every node, but only dirty ones do
            // work; clean nodes serve their cached result.
            incremental  = true;
            dirty        = *dirty_set;
            cache        = cache_map;
            total_to_run = count;
            for (node_id id = 0; id < count; ++id)
            {
                remaining[id].store(g.dependency_count(id), std::memory_order_relaxed);
            }
        }
        else if (sched != nullptr)
        {
            // Restricted run: schedule only the sink ancestors, count only
            // scheduled dependencies, and set up liveness tracking.
            scheduled = *sched;
            is_sink.assign(count, 0);
            for (const node_id s : *sink_list)
            {
                is_sink[s] = 1;
            }
            const std::vector<std::uint32_t> counts = graph_passes::consumer_counts(g, scheduled);
            consumers_left.reserve(count);
            for (node_id id = 0; id < count; ++id)
            {
                consumers_left.push_back(std::make_unique<std::atomic<std::uint32_t>>(counts[id]));
            }
            total_to_run = 0;
            for (node_id id = 0; id < count; ++id)
            {
                if (!scheduled[id])
                {
                    continue;
                }
                ++total_to_run;
                std::uint32_t deps = 0;
                const auto    dr   = g.dependency_range(id);
                for (auto it = dr.begin; it != dr.end; ++it)
                {
                    if (scheduled[*it])
                    {
                        ++deps;
                    }
                }
                remaining[id].store(deps, std::memory_order_relaxed);
            }
        }
        else
        {
            total_to_run = count;
            for (node_id id = 0; id < count; ++id)
            {
                remaining[id].store(g.dependency_count(id), std::memory_order_relaxed);
            }
        }
    }

    const dependency_graph& g;

    // One atomic in-degree counter per node; a node becomes ready when it
    // hits zero. This is the autograd-engine decrement, replacing the
    // per-node shared_future push_dependent would allocate.
    std::vector<std::atomic<std::uint32_t>> remaining;

    // Shared result per node (fan-out shares it; see run()).
    std::vector<std::shared_ptr<std::any>> results;

    // Per-node failure flag so dependents are skipped. A plain char vector
    // (not std::atomic<bool>) because each element is written by exactly one
    // worker (the one that ran that node) before its successors are
    // released, and read by those successors only after the releasing
    // fetch_sub on the successor's `remaining` counter establishes
    // happens-before.
    std::vector<char> failed;

    // --- Restricted-run (run_to) state; empty/unused for a full run() ---
    // Which nodes are scheduled (ancestors of the requested sinks).
    std::vector<char> scheduled;
    // Which nodes are requested outputs (their slots are never freed early).
    std::vector<char> is_sink;
    // Liveness: how many scheduled consumers still need each node's result.
    // One heap-allocated atomic per node (a std::vector<std::atomic<T>> can't
    // be resized/moved). Atomic because sibling consumers of a fan-out node
    // run concurrently on different workers and decrement the same counter.
    std::vector<std::unique_ptr<std::atomic<std::uint32_t>>> consumers_left;
    // How many nodes this run will actually execute.
    std::size_t total_to_run = 0;

    // --- Incremental-run (run_incremental) state; unused otherwise ---
    bool incremental = false;
    // Which nodes recompute (version moved or downstream of a moved node).
    // Written only during the pre-run dirty propagation; read-only during
    // the run itself, so a plain char vector is race-free here.
    std::vector<char> dirty;
    // Previous run's results, keyed by node_id; clean nodes serve from it.
    std::unordered_map<node_id, std::any>* cache = nullptr;

    // Ready queue: nodes whose remaining count reached zero, in priority
    // order (higher bottom-level first). Guarded by ready_mutex.
    struct ready_item
    {
        std::uint32_t priority;
        node_id       id;
        bool          operator<(const ready_item& other) const { return priority < other.priority; }
    };
    std::vector<ready_item> ready_heap;
    std::mutex              ready_mutex;
    std::condition_variable ready_cv;

    // Number of nodes that have finished (successfully, skipped, or failed).
    // The run is done when this reaches total_to_run.
    std::atomic<std::size_t> completed{0};

    // First-failure-wins record.
    std::atomic<bool> failure_recorded{false};
    std::mutex        failure_mutex;
    node_id           failed_node_id = 0;
    std::string       failed_message;
};

graph_executor::graph_executor(graph_executor_options options)
    : options_(std::move(options)), queue_(std::make_unique<threaded_callback_queue>())
{
    queue_->set_number_of_threads(resolve_thread_count(options_.number_of_threads));
}

graph_execution_status graph_executor::run(
    const dependency_graph& g, std::unordered_map<node_id, std::any>& out_results)
{
    // Blocking run with no cancellation, all nodes scheduled.
    return execute(g, out_results, nullptr, nullptr, nullptr, nullptr);
}

graph_execution_status graph_executor::run_to(
    const dependency_graph&                g,
    const std::vector<node_id>&            sinks,
    std::unordered_map<node_id, std::any>& out_results)
{
    auto const invalid = std::find_if(
        sinks.begin(), sinks.end(), [&](const node_id sink) { return sink >= g.node_count(); });
    if (invalid != sinks.end())
    {
        return graph_execution_status::failure(
            *invalid, "run_to(): sink node id outside the graph");
    }
    return execute(g, out_results, nullptr, &sinks, nullptr, nullptr);
}

graph_execution_status graph_executor::run_incremental(
    const dependency_graph&                g,
    const std::vector<std::uint64_t>&      baseline_versions,
    std::unordered_map<node_id, std::any>& in_out_cache)
{
    const std::size_t count = g.node_count();

    // Seed dirtiness: a node is dirty when its version moved since the
    // baseline, or when it has no cached result to serve.
    std::vector<char> dirty(count, 0);
    for (node_id id = 0; id < count; ++id)
    {
        const std::uint64_t baseline = id < baseline_versions.size() ? baseline_versions[id] : 0;
        const bool          version_moved = g.version(id) != baseline;
        const bool          no_cache      = in_out_cache.find(id) == in_out_cache.end();
        dirty[id]                         = (version_moved || no_cache) ? 1 : 0;
    }

    // Propagate: downstream of a dirty node is dirty. The topological order
    // guarantees every dependency is visited before its dependents.
    for (const node_id id : g.topological_order())
    {
        if (dirty[id])
        {
            continue;
        }
        const auto deps = g.dependency_range(id);
        for (auto it = deps.begin; it != deps.end; ++it)
        {
            if (dirty[*it])
            {
                dirty[id] = 1;
                break;
            }
        }
    }

    return execute(g, in_out_cache, nullptr, nullptr, &dirty, &in_out_cache);
}

std::future<graph_execution_status> graph_executor::run_async(
    const dependency_graph& g, std::unordered_map<node_id, std::any>& out_results)
{
    auto flag = std::make_shared<std::atomic<bool>>(false);
    {
        const std::lock_guard<std::mutex> lock(cancel_mutex_);
        cancel_flag_ = flag;
    }
    return std::async(
        std::launch::async,
        [this, &g, &out_results, flag]
        { return execute(g, out_results, flag, nullptr, nullptr, nullptr); });
}

void graph_executor::cancel()
{
    std::shared_ptr<std::atomic<bool>> flag;
    {
        const std::lock_guard<std::mutex> lock(cancel_mutex_);
        flag = cancel_flag_;
    }
    if (flag)
    {
        flag->store(true, std::memory_order_release);
    }
}

graph_execution_status graph_executor::execute(
    const dependency_graph&                   g,
    std::unordered_map<node_id, std::any>&    out_results,
    const std::shared_ptr<std::atomic<bool>>& cancel_flag,
    const std::vector<node_id>*               sinks,
    const std::vector<char>*                  dirty,
    std::unordered_map<node_id, std::any>*    cache)
{
    const std::size_t count = g.node_count();

    // Restricted run: compute the sink-ancestor set once (DCE), then only
    // those nodes are scheduled and only sinks' results are reported.
    std::vector<char> ancestors;
    if (sinks != nullptr)
    {
        ancestors = graph_passes::ancestor_set(g, *sinks);
    }
    auto state = std::make_unique<run_state>(
        g, sinks != nullptr ? &ancestors : nullptr, sinks, dirty, cache);

    // Hooks are read once per run so a caller can swap them between runs
    // without synchronizing with in-flight workers.
    const auto& on_node_start = options_.on_node_start;
    const auto& on_node_end   = options_.on_node_end;

    auto is_cancelled = [&cancel_flag]()
    { return cancel_flag && cancel_flag->load(std::memory_order_acquire); };

    auto record_failure = [&state](node_id id, std::string message)
    {
        bool expected = false;
        if (state->failure_recorded.compare_exchange_strong(expected, true))
        {
            const std::lock_guard<std::mutex> lock(state->failure_mutex);
            state->failed_node_id = id;
            state->failed_message = std::move(message);
        }
    };

    // Mark a node finished and wake the caller once the whole graph is done.
    auto finish_node = [&state]()
    {
        state->completed.fetch_add(1, std::memory_order_acq_rel);
        const std::lock_guard<std::mutex> lock(state->ready_mutex);
        state->ready_cv.notify_all();
    };

    // Execute one node, then decrement successors and enqueue any that
    // become ready. This is the scheduler's inner loop.
    auto execute_node =
        [&state, &record_failure, &on_node_start, &on_node_end, &finish_node, &is_cancelled](
            node_id id)
    {
        const dependency_graph& g = state->g;

        // Skip if cancelled or if any dependency failed.
        bool skip = is_cancelled();
        if (!skip)
        {
            const auto deps = g.dependency_range(id);
            for (auto it = deps.begin; it != deps.end; ++it)
            {
                if (state->failed[*it] != 0)
                {
                    skip = true;
                    break;
                }
            }
        }

        if (skip)
        {
            state->failed[id]  = 1;
            state->results[id] = std::make_shared<std::any>();
        }
        else if (state->incremental && state->dirty[id] == 0)
        {
            // Clean node: serve the cached result without running work_.
            // The dirty computation guaranteed a cache entry exists (a clean
            // node missing from the cache was marked dirty).
            const auto it = state->cache->find(id);
            if (it != state->cache->end())
            {
                state->results[id] = std::make_shared<std::any>(it->second);
            }
            else
            {
                // Defensive: seeding marked cache-missing nodes dirty, so
                // this is unreachable -- but if it ever fires, report it
                // rather than silently returning an empty result as success.
                state->failed[id]  = 1;
                state->results[id] = std::make_shared<std::any>();
                record_failure(id, "clean node missing from incremental cache");
            }
        }
        else
        {
            // Gather inputs (shared, not moved: a fan-out dependency's
            // result is read once per consumer).
            const auto            deps = g.dependency_range(id);
            std::vector<std::any> inputs;
            inputs.reserve(deps.end - deps.begin);
            for (auto it = deps.begin; it != deps.end; ++it)
            {
                inputs.push_back(*state->results[*it]);
            }

            run_hook_safely(on_node_start, id, g.node_at(id).name_);
            try
            {
                state->results[id] = std::make_shared<std::any>(g.node_at(id).work_(inputs));
            }
            catch (const std::exception& e)
            {
                state->failed[id] = 1;
                record_failure(id, e.what());
                state->results[id] = std::make_shared<std::any>();
            }
            catch (...)
            {
                state->failed[id] = 1;
                record_failure(id, "unknown exception in node \"" + g.node_at(id).name_ + "\"");
                state->results[id] = std::make_shared<std::any>();
            }
            run_hook_safely(on_node_end, id, g.node_at(id).name_);
        }

        // Liveness: this node has now consumed its dependencies' results, so
        // release any dependency slot whose last consumer just ran (restricted
        // runs only; a full run() reports every result, so nothing is freed).
        if (!state->scheduled.empty())
        {
            const auto deps = g.dependency_range(id);
            for (auto it = deps.begin; it != deps.end; ++it)
            {
                const node_id dep = *it;
                if (state->scheduled[dep] && !state->is_sink[dep])
                {
                    // Atomic: sibling consumers of a fan-out dep decrement
                    // concurrently. The last fetch_sub synchronizes-with all
                    // prior ones, so their reads of results[dep] happen-before
                    // the reset.
                    if (state->consumers_left[dep]->fetch_sub(1, std::memory_order_acq_rel) == 1)
                    {
                        state->results[dep] = nullptr;
                    }
                }
            }
        }

        // Decrement successors; enqueue any that become ready.
        const auto succs = g.successor_range(id);
        for (auto it = succs.begin; it != succs.end; ++it)
        {
            const node_id succ = *it;
            if (!state->scheduled.empty() && !state->scheduled[succ])
            {
                continue;  // not part of this restricted run
            }
            if (state->remaining[succ].fetch_sub(1, std::memory_order_acq_rel) == 1)
            {
                // This thread brought the successor to zero; enqueue it.
                {
                    const std::lock_guard<std::mutex> lock(state->ready_mutex);
                    state->ready_heap.push_back({g.priority(succ), succ});
                    std::push_heap(state->ready_heap.begin(), state->ready_heap.end());
                }
                state->ready_cv.notify_one();
            }
        }

        finish_node();
    };

    // Seed the ready queue with source nodes (in-degree zero) that are part
    // of this run.
    {
        const std::lock_guard<std::mutex> lock(state->ready_mutex);
        for (node_id id = 0; id < count; ++id)
        {
            if (!state->scheduled.empty() && !state->scheduled[id])
            {
                continue;
            }
            if (state->remaining[id].load(std::memory_order_relaxed) == 0)
            {
                state->ready_heap.push_back({g.priority(id), id});
            }
        }
        std::make_heap(state->ready_heap.begin(), state->ready_heap.end());
    }

    // Worker loop: pop the highest-priority ready node and run it, until the
    // whole graph is done. Pushed as one task per pop onto the persistent
    // pool; the pool's workers pull them off. Keep each task's future so we
    // can wait for the workers to exit before `state` is destroyed.
    const int workers = queue_->get_number_of_threads();
    std::vector<threaded_callback_queue::shared_future_pointer<void>> worker_futures;
    worker_futures.reserve(static_cast<std::size_t>(workers));
    for (int w = 0; w < workers; ++w)
    {
        worker_futures.push_back(queue_->push(
            [&state, &execute_node]()
            {
                for (;;)
                {
                    node_id id = 0;
                    {
                        std::unique_lock<std::mutex> lock(state->ready_mutex);
                        state->ready_cv.wait(
                            lock,
                            [&state]
                            {
                                return !state->ready_heap.empty() ||
                                       state->completed.load(std::memory_order_acquire) ==
                                           state->total_to_run;
                            });
                        if (state->ready_heap.empty())
                        {
                            // Graph done and nothing left to run.
                            return;
                        }
                        std::pop_heap(state->ready_heap.begin(), state->ready_heap.end());
                        id = state->ready_heap.back().id;
                        state->ready_heap.pop_back();
                    }
                    execute_node(id);
                }
            }));
    }

    // Block until every scheduled node has finished.
    {
        std::unique_lock<std::mutex> lock(state->ready_mutex);
        state->ready_cv.wait(
            lock,
            [&state]
            { return state->completed.load(std::memory_order_acquire) == state->total_to_run; });
    }

    // The graph is done, but the worker-loop tasks pushed above may still be
    // blocked in ready_cv.wait() (they only return once they observe the
    // completion predicate). Wake them and wait for their futures so `state`
    // is not destroyed out from under a worker that wakes late.
    {
        const std::lock_guard<std::mutex> lock(state->ready_mutex);
        state->ready_cv.notify_all();
    }
    for (auto& f : worker_futures)
    {
        f->wait();
    }

    // Collect results: move out when this run is the sole owner. A run_to()
    // reports only the requested sinks (intermediate slots were released by
    // liveness); an incremental run writes every node's result back into the
    // cache (recomputed for dirty nodes, carried over for clean ones); a
    // full run() reports every node.
    out_results.clear();
    if (state->incremental)
    {
        // out_results IS the caller's cache (run_incremental passed it as
        // both). Rewrite it in place with this run's per-node results. A
        // node that failed or was skipped gets NO cache entry -- its empty
        // slot must not be cached, or the next run's seeding would serve the
        // empty result as "clean" instead of recomputing it.
        for (node_id id = 0; id < count; ++id)
        {
            std::shared_ptr<std::any>& slot = state->results[id];
            if (state->failed[id] != 0)
            {
                out_results.erase(id);  // poison nothing: recompute next time
            }
            else if (slot)
            {
                out_results[id] = std::move(*slot);
            }
            else
            {
                out_results[id] = std::any{};
            }
        }
    }
    else if (sinks != nullptr)
    {
        out_results.reserve(sinks->size());
        for (const node_id sink : *sinks)
        {
            std::shared_ptr<std::any>& slot = state->results[sink];
            if (slot)
            {
                out_results.emplace(sink, std::move(*slot));
            }
            else
            {
                out_results.emplace(sink, std::any{});
            }
        }
    }
    else
    {
        out_results.reserve(count);
        for (node_id id = 0; id < count; ++id)
        {
            std::shared_ptr<std::any>& slot = state->results[id];
            if (slot.use_count() == 1)
            {
                out_results.emplace(id, std::move(*slot));
            }
            else
            {
                out_results.emplace(id, *slot);
            }
        }
    }

    if (is_cancelled())
    {
        return graph_execution_status::cancelled("run cancelled");
    }
    if (state->failure_recorded.load())
    {
        const std::lock_guard<std::mutex> lock(state->failure_mutex);
        return graph_execution_status::failure(state->failed_node_id, state->failed_message);
    }
    return graph_execution_status::success();
}

}  // namespace graph
