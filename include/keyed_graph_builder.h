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

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "dependency_graph.h"
#include "graph_builder.h"
#include "graph_types.h"

namespace graph
{

/**
 * @brief Builds a graph by recursively resolving keys on demand, instead of
 * requiring every node and edge to be known before construction starts.
 *
 * graph_builder (graph_builder.h) requires the caller to already know the
 * full node/edge set before build(). Some domains don't have that upfront:
 * which other objects a computation needs can only be discovered by
 * partially inspecting it, and a dependency's own dependencies are only
 * discoverable once *it* has been inspected too -- e.g. resolving what a
 * market-data object (a calibrated curve, say) needs is itself a recursive,
 * data-dependent walk, not a fixed schema.
 *
 * This mirrors how PyTorch's autograd graph is assembled: a Node's edges to
 * its next_functions_ are recorded as a side effect of calling other ops
 * during the forward pass -- not declared upfront -- and the resulting
 * graph is only handed to the engine once that pass is complete. resolve()
 * plays that role: calling ctx.resolve() on a dependency from inside
 * resolve_fn *is* the act of recording the edge, in call order, with no
 * separate depends_on() call needed from the caller. graph_executor then
 * plays the engine's part unchanged, once build() freezes the traced graph.
 *
 * Not thread-safe -- intended for a single-threaded resolution ("trace")
 * pass that precedes the (potentially parallel) graph_executor::run() pass.
 */
template <typename Key>
class keyed_graph_builder
{
public:
    /**
     * @brief Produces `key`'s node name and work.
     *
     * Called at most once per distinct key -- resolve() memoizes, so a key
     * requested again (by this or another resolve_fn) returns the cached
     * node_id without invoking resolve_fn again. Before returning success,
     * an implementation must have called `ctx.resolve()` -- `ctx` is the
     * same keyed_graph_builder -- for every key `out_work` depends on, in
     * the order `out_work`'s input vector should receive their results.
     * Returning a non-ok graph_status leaves `out_name`/`out_work` unused
     * and aborts this key's resolution, propagating to whichever resolve()
     * call (if any) is waiting on it.
     */
    using resolver_fn = std::function<graph_status(
        const Key& key, keyed_graph_builder& ctx, std::string& out_name, node_work& out_work)>;

    /**
     * @brief Resolves `key` to a node_id, invoking `resolve_fn` at most once
     * per distinct key.
     *
     * Detects a resolution cycle -- resolve_fn for `key`, directly or
     * transitively, calling resolve() on `key` again before returning --
     * and reports it as graph_build_status_code::cycle_detected instead of
     * recursing without bound.
     */
    graph_status resolve(const Key& key, const resolver_fn& resolve_fn, node_id& out)
    {
        if (const auto it = resolved_.find(key); it != resolved_.end())
        {
            record_in_caller_frame(it->second);
            out = it->second;
            return graph_status::success();
        }

        if (in_progress_.find(key) != in_progress_.end())
        {
            return graph_status::failure(
                graph_build_status_code::cycle_detected,
                "keyed_graph_builder::resolve(): resolution cycle detected");
        }

        in_progress_.insert(key);
        frame_stack_.emplace_back();

        std::string        name;
        node_work          work;
        const graph_status status = resolve_fn(key, *this, name, work);

        std::vector<node_id> dependencies = std::move(frame_stack_.back());
        frame_stack_.pop_back();
        in_progress_.erase(key);

        if (!status.ok())
        {
            return status;
        }

        const node_id id = builder_.add_node(std::move(name), std::move(work));
        for (const node_id dependency : dependencies)
        {
            builder_.depends_on(id, dependency);
        }
        resolved_.emplace(key, id);
        record_in_caller_frame(id);

        out = id;
        return graph_status::success();
    }

    /**
     * @brief Validates and freezes the graph accumulated across resolve()
     * calls -- forwards to graph_builder::build().
     *
     * graph_builder::build()'s own Kahn's-algorithm checks
     * (cycle_detected / unknown_node_reference) are structurally
     * unreachable here: resolve() only ever calls depends_on() with ids it
     * obtained from a prior, already-succeeded resolve() call, and its own
     * in_progress_ check rejects a resolution cycle before any edge for it
     * is recorded.
     */
    graph_status build(std::shared_ptr<dependency_graph>& out) const { return builder_.build(out); }

    /**
     * @brief Stamps the node `key` resolved to with a version for
     * incremental re-run.
     *
     * Callers version their input keys (e.g. a market quote's "as-of"
     * counter); graph_executor::run_incremental() then recomputes exactly
     * the nodes whose key version moved since a baseline, plus everything
     * downstream of them, and serves the rest from cache. Derived keys
     * (curves, prices) are typically left unstamped -- they recompute
     * whenever an input they depend on is dirty, which is the correct
     * default. Returns false if `key` has not been resolved yet. Must be
     * called before build() -- build() copies the stamps into the frozen
     * graph, so stamping afterwards is a no-op on the built graph.
     */
    bool with_key_version(const Key& key, std::uint64_t version)
    {
        const auto it = resolved_.find(key);
        if (it == resolved_.end())
        {
            return false;
        }
        return builder_.with_node_version(it->second, version);
    }

    /// Current version stamp of the node `key` resolved to, or 0 if the key
    /// is unresolved or was never stamped. Lets a caller snapshot the stamps
    /// it set (its baseline for the next run_incremental) without tracking
    /// them separately.
    std::uint64_t key_version(const Key& key) const
    {
        const auto it = resolved_.find(key);
        return it == resolved_.end() ? 0 : builder_.node_version(it->second);
    }

    /// True once `key` has a memoized node_id -- the read-only query
    /// discover_fn uses to tell "still needed" apart from "already have it",
    /// the same role market.contains() plays in the pretorian-style pattern
    /// this generalizes (see keyed_graph_builder's class doc comment).
    bool is_resolved(const Key& key) const { return resolved_.find(key) != resolved_.end(); }

    /**
     * @brief Computes which dependency keys `key` still needs, given what
     * this keyed_graph_builder has resolved so far (queryable via
     * ctx.is_resolved()).
     *
     * Invoked once per discovery pass by resolve_with_discovery() -- not
     * just once -- because a later pass can legitimately need more than an
     * earlier one did: which keys a computation depends on may only become
     * apparent once some of its other dependencies are already known (e.g.
     * a calibration config discovered on pass 0 determines, once resolved,
     * which further inputs pass 1 needs). A pass returning only keys that
     * are already resolved (including none at all) is the fixed point that
     * ends discovery and moves on to build_fn.
     */
    using discover_fn =
        std::function<std::vector<Key>(const Key& key, const keyed_graph_builder& ctx)>;

    /**
     * @brief Produces `key`'s own node name and work, once discovery has
     * reached a fixed point.
     *
     * Unlike resolver_fn, build_fn never touches the keyed_graph_builder --
     * discovering dependencies is entirely discover_fn's job, so build_fn is
     * free to be a pure function of `key` alone (e.g. an ordinary,
     * independently testable "construct this object" routine).
     */
    using build_fn =
        std::function<graph_status(const Key& key, std::string& out_name, node_work& out_work)>;

    /**
     * @brief Looks up the (discover_fn, build_fn) pair for a key encountered
     * during resolve_with_discovery() -- the originally requested key and
     * every dependency key discovery reveals, at any recursion depth.
     *
     * Plays the role of a type-name-keyed builder registry (mirroring
     * XSIGMA_REGISTER_GENERIC_BUILDER's GenericBuilder()->run(name, ...) in
     * the pretorian-style pattern this generalizes): callers typically
     * implement it as a lookup into a small table keyed by what kind of
     * object `key` names, so each object kind's discover/build pair is
     * registered once and reused for however many distinct keys of that
     * kind resolution encounters.
     */
    using resolver_provider = std::function<std::pair<discover_fn, build_fn>(const Key& key)>;

    /**
     * @brief Resolves `key` by disentangling discovery from building: runs
     * `provider(key)`'s discover_fn to a fixed point -- resolving
     * (transitively, via this same resolve_with_discovery) whichever of its
     * returned keys aren't yet resolved, and repeating until a pass reveals
     * nothing new -- then invokes its build_fn to produce the node itself.
     *
     * This is exactly the do-while-discover-then-build shape resolve_fn
     * would otherwise have to hand-roll around ctx.resolve() calls (see
     * resolve()'s doc comment); resolve_with_discovery factors that shape
     * into shared machinery -- implemented as a single resolve() call with a
     * resolver_fn synthesized from discover_fn/build_fn -- so per-key logic
     * reduces to two independently pluggable, independently testable
     * questions: "what do I still need" and "what do I compute".
     */
    graph_status resolve_with_discovery(
        const Key& key, const resolver_provider& provider, node_id& out)
    {
        const std::pair<discover_fn, build_fn> fns = provider(key);

        // Named locals (not structured bindings) so the lambda can capture
        // them under C++17 -- capturing a structured binding is a C++20
        // extension.
        const resolver_fn synthesized = [&discover = fns.first, &build = fns.second, &provider](
                                            const Key&           k,
                                            keyed_graph_builder& ctx,
                                            std::string&         out_name,
                                            node_work&           out_work) -> graph_status
        {
            std::vector<Key> newly_needed;
            do
            {
                newly_needed.clear();
                for (const Key& candidate : discover(k, ctx))
                {
                    if (!ctx.is_resolved(candidate))
                    {
                        newly_needed.push_back(candidate);
                    }
                }
                for (const Key& dependency : newly_needed)
                {
                    node_id            dependency_id;
                    const graph_status status =
                        ctx.resolve_with_discovery(dependency, provider, dependency_id);
                    if (!status.ok())
                    {
                        return status;
                    }
                }
            } while (!newly_needed.empty());

            return build(k, out_name, out_work);
        };
        return resolve(key, synthesized, out);
    }

private:
    void record_in_caller_frame(node_id id)
    {
        if (!frame_stack_.empty())
        {
            frame_stack_.back().push_back(id);
        }
    }

    graph_builder                     builder_;
    std::unordered_map<Key, node_id>  resolved_;
    std::unordered_set<Key>           in_progress_;
    std::vector<std::vector<node_id>> frame_stack_;
};

}  // namespace graph
