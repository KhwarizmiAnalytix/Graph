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
#include <memory>
#include <vector>

#include "common/graph_export.h"
#include "dependency_graph.h"
#include "graph_types.h"

namespace graph
{

/**
 * @brief Freeze-time passes over a built dependency_graph.
 *
 * Once graph_builder::build() has frozen the DAG it is stable, so cheap
 * whole-graph analyses can run once and be reused by every execution. This
 * is the FX "freeze-then-optimize" idea from the enhancement plan, kept
 * deliberately small: no IR language, no rewriting of node work -- just
 * reachability (which nodes a requested output actually needs) and the
 * derived liveness information the executor uses to recycle result slots.
 *
 * All passes are pure functions of the frozen graph; none mutate it.
 */
namespace graph_passes
{
/**
     * @brief Marks every node that is one of `sinks` or a (transitive)
     * dependency of one.
     *
     * Returns a per-node flag vector indexed by node_id. This is the DCE
     * reachability set: nodes not marked are dead with respect to the
     * requested outputs and can be skipped or dropped.
     */
GRAPH_API std::vector<char> ancestor_set(
    const dependency_graph& g, const std::vector<node_id>& sinks);

/**
     * @brief Computes, for each node, how many of its successors are in
     * `scheduled` -- i.e. how many consumers its result still has in a run
     * restricted to that set.
     *
     * The executor uses this for liveness: a node's result slot can be
     * released once this many of its consumers have read it. Nodes not in
     * `scheduled` get 0.
     */
GRAPH_API std::vector<std::uint32_t> consumer_counts(
    const dependency_graph& g, const std::vector<char>& scheduled);

/**
     * @brief Produces a new graph containing only the ancestors of `sinks`,
     * with node ids remapped densely.
     *
     * The dead branches (nodes no sink depends on) are dropped. `old_to_new`
     * receives the id remapping, sized g.node_count(), with
     * old_to_new[old_id] == the new id for kept nodes and npos for dropped
     * ones. Node work, names, and dependency input order are preserved by
     * rebuilding through graph_builder; critical-path priorities are
     * recomputed on the pruned edge set.
     *
     * Returns nullptr (and leaves old_to_new empty) if any sink id is out
     * of range.
     */
GRAPH_API std::shared_ptr<dependency_graph> prune_to_sinks(
    const dependency_graph& g, const std::vector<node_id>& sinks, std::vector<node_id>& old_to_new);

/// Sentinel for old_to_new entries of dropped nodes.
inline constexpr node_id npos = static_cast<node_id>(-1);

}  // namespace graph_passes

}  // namespace graph
