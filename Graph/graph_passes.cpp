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

#include "graph_passes.h"

#include <algorithm>
#include <cstdint>

#include "graph_builder.h"

namespace graph
{

namespace graph_passes
{

std::vector<char> ancestor_set(const dependency_graph& g, const std::vector<node_id>& sinks)
{
    const std::size_t    count = g.node_count();
    std::vector<char>    keep(count, 0);
    std::vector<node_id> stack;
    stack.reserve(count);

    for (const node_id sink : sinks)
    {
        if (sink < count && !keep[sink])
        {
            keep[sink] = 1;
            stack.push_back(sink);
        }
    }

    // Depth-first walk over dependencies (the transpose edges). The graph is
    // frozen, so this is a plain reachability pass -- O(V + E).
    while (!stack.empty())
    {
        const node_id id = stack.back();
        stack.pop_back();
        const auto deps = g.dependency_range(id);
        for (auto it = deps.begin; it != deps.end; ++it)
        {
            const node_id dep = *it;
            if (!keep[dep])
            {
                keep[dep] = 1;
                stack.push_back(dep);
            }
        }
    }
    return keep;
}

std::vector<std::uint32_t> consumer_counts(
    const dependency_graph& g, const std::vector<char>& scheduled)
{
    const std::size_t          count = g.node_count();
    std::vector<std::uint32_t> consumers(count, 0);
    for (node_id id = 0; id < count; ++id)
    {
        if (!scheduled[id])
        {
            continue;
        }
        const auto succs = g.successor_range(id);
        for (auto it = succs.begin; it != succs.end; ++it)
        {
            if (scheduled[*it])
            {
                ++consumers[id];
            }
        }
    }
    return consumers;
}

std::shared_ptr<dependency_graph> prune_to_sinks(
    const dependency_graph& g, const std::vector<node_id>& sinks, std::vector<node_id>& old_to_new)
{
    const std::size_t count = g.node_count();
    old_to_new.clear();

    if (std::any_of(sinks.begin(), sinks.end(), [&](const node_id sink) { return sink >= count; }))
    {
        return nullptr;
    }

    const std::vector<char> keep = ancestor_set(g, sinks);

    // Dense remap: kept nodes get sequential new ids in original order.
    old_to_new.assign(count, npos);
    node_id next = 0;
    for (node_id id = 0; id < count; ++id)
    {
        if (keep[id])
        {
            old_to_new[id] = next++;
        }
    }

    // Rebuild through graph_builder so the new graph gets its own validated
    // topological order, CSR packing, and priorities for free. Version stamps
    // are forwarded so a prune -> run_incremental pipeline keeps them.
    graph_builder builder;
    for (node_id id = 0; id < count; ++id)
    {
        if (keep[id])
        {
            const node_id new_id = builder.add_node(g.node_at(id).name_, g.node_at(id).work_);
            builder.with_node_version(new_id, g.version(id));
        }
    }
    for (node_id id = 0; id < count; ++id)
    {
        if (!keep[id])
        {
            continue;
        }
        // Dependency input order must be preserved -- node_work relies on it
        // (graph_types.h's node_work doc comment).
        const auto deps = g.dependency_range(id);
        for (auto it = deps.begin; it != deps.end; ++it)
        {
            builder.depends_on(old_to_new[id], old_to_new[*it]);
        }
    }

    std::shared_ptr<dependency_graph> out;
    if (!builder.build(out).ok())
    {
        old_to_new.clear();
        return nullptr;
    }
    return out;
}

}  // namespace graph_passes

}  // namespace graph
