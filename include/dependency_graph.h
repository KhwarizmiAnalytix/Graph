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

#include <cstdint>
#include <string>
#include <vector>

#include "common/graph_export.h"
#include "graph_types.h"

namespace graph
{

class graph_builder;

/**
 * @brief Immutable dependency DAG, produced by graph_builder::build().
 *
 * Holds each node's work callable, its outgoing (successors_) and incoming
 * (dependencies_) edges, and a topological order computed once at build
 * time (Kahn's algorithm) so graph_executor never has to recompute it, the
 * cycle check, or the dependency inversion on every run().
 *
 * Adjacency is stored twice, each for a different consumer:
 *
 *  - The per-node `node::successors_` / `node::dependencies_` vectors are
 *    the stable, source-compatible API callers already use.
 *  - A packed CSR (compressed sparse row) layout -- `succ_offsets_` /
 *    `succ_` and `dep_offsets_` / `dep_`, with 32-bit node ids -- is what
 *    graph_executor's scheduler walks. Flat, contiguous arrays are
 *    cache-friendly for the decrement-and-dispatch inner loop, and u32 ids
 *    halve the footprint (a graph is capped at 2^32 nodes, far beyond any
 *    realistic task DAG). graph_builder packs both once at build() time.
 *
 * The two layouts are built from the same edge list and always agree; the
 * CSR is derived, never authoritative.
 */
class GRAPH_VISIBILITY dependency_graph
{
public:
    struct node
    {
        std::string name_;
        node_work   work_;

        /// Nodes that depend on this one (used to drive Kahn's algorithm).
        std::vector<node_id> successors_;

        /// This node's own dependencies, in the order graph_builder::depends_on
        /// declared them -- node_work receives their results in this same
        /// order.
        std::vector<node_id> dependencies_;
    };

    std::size_t node_count() const { return nodes_.size(); }

    const node& node_at(node_id id) const { return nodes_[id]; }

    /// Nodes in an order where every dependency appears before its dependents.
    const std::vector<node_id>& topological_order() const { return topological_order_; }

    ///@{
    /**
     * @brief CSR adjacency for the scheduler.
     *
     * `successor_range(id)` / `dependency_range(id)` return the node's
     * successors / dependencies as a contiguous span of 32-bit ids. These
     * are the same edges as node_at(id).successors_ / .dependencies_, just
     * packed flat for cache-friendly traversal.
     */
    struct id_range
    {
        const std::uint32_t* begin;
        const std::uint32_t* end;
    };

    id_range successor_range(node_id id) const
    {
        const std::uint32_t b = succ_offsets_[id];
        const std::uint32_t e = succ_offsets_[id + 1];
        return {succ_.data() + b, succ_.data() + e};
    }

    id_range dependency_range(node_id id) const
    {
        const std::uint32_t b = dep_offsets_[id];
        const std::uint32_t e = dep_offsets_[id + 1];
        return {dep_.data() + b, dep_.data() + e};
    }
    ///@}

    /// Number of dependencies per node (in-degree), derived from the CSR.
    std::uint32_t dependency_count(node_id id) const
    {
        return dep_offsets_[id + 1] - dep_offsets_[id];
    }

    /**
     * @brief Version stamp of a node (0 unless the builder set one).
     *
     * Incremental re-run (graph_executor::run_incremental) compares these
     * stamps against a caller-supplied baseline: a node whose stamp differs
     * from the baseline -- and everything downstream of it -- is dirty and
     * recomputes; clean nodes return their cached results. keyed graphs get
     * a stamp per key from keyed_graph_builder::with_key_version(); plain
     * graph_builder graphs leave every stamp 0, which makes every node
     * dirty against any nonzero baseline.
     */
    std::uint64_t version(node_id id) const { return version_[id]; }

    /**
     * @brief Bottom-level priority of a node: the length (in nodes) of the
     * longest chain from it to a sink, computed at build time.
     *
     * A higher value means the node is on a longer critical path, so the
     * executor's ready queue should run it sooner. Nodes with no successors
     * (sinks) have priority 1; each other node is 1 + max over successors.
     */
    std::uint32_t priority(node_id id) const { return priority_[id]; }

private:
    friend class graph_builder;

    dependency_graph(
        std::vector<node>          nodes,
        std::vector<node_id>       topological_order,
        std::vector<std::uint32_t> succ_offsets,
        std::vector<std::uint32_t> succ,
        std::vector<std::uint32_t> dep_offsets,
        std::vector<std::uint32_t> dep,
        std::vector<std::uint32_t> priority,
        std::vector<std::uint64_t> version)
        : nodes_(std::move(nodes)),
          topological_order_(std::move(topological_order)),
          succ_offsets_(std::move(succ_offsets)),
          succ_(std::move(succ)),
          dep_offsets_(std::move(dep_offsets)),
          dep_(std::move(dep)),
          priority_(std::move(priority)),
          version_(std::move(version))
    {
    }

    std::vector<node>    nodes_;
    std::vector<node_id> topological_order_;

    // CSR adjacency (32-bit ids). Offsets arrays have node_count()+1 entries.
    std::vector<std::uint32_t> succ_offsets_;
    std::vector<std::uint32_t> succ_;
    std::vector<std::uint32_t> dep_offsets_;
    std::vector<std::uint32_t> dep_;

    // Bottom-level (critical-path) priority per node.
    std::vector<std::uint32_t> priority_;

    // Per-node version stamp for incremental re-run (0 = unstamped).
    std::vector<std::uint64_t> version_;
};

}  // namespace graph
