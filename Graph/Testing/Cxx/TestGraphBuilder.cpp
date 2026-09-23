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

#include <algorithm>
#include <any>
#include <memory>

#include "GraphTest.h"
#include "dependency_graph.h"
#include "graph_builder.h"
#include "graph_types.h"

using namespace graph;

namespace
{
node_work noop_work()
{
    return [](const std::vector<std::any>&) -> std::any { return {}; };
}
}  // namespace

TEST(GraphBuilder, linear_chain_builds_in_topological_order)
{
    graph_builder builder;
    const node_id a = builder.add_node("A", noop_work());
    const node_id b = builder.add_node("B", noop_work());
    const node_id c = builder.add_node("C", noop_work());
    builder.depends_on(b, a).depends_on(c, b);

    std::shared_ptr<dependency_graph> g;
    const graph_status                status = builder.build(g);

    ASSERT_TRUE(status.ok());
    ASSERT_NE(g, nullptr);
    ASSERT_EQ(g->node_count(), 3U);
    EXPECT_EQ(g->topological_order(), (std::vector<node_id>{a, b, c}));
}

TEST(GraphBuilder, diamond_dependency_builds_successfully)
{
    graph_builder builder;
    const node_id a = builder.add_node("A", noop_work());
    const node_id b = builder.add_node("B", noop_work());
    const node_id c = builder.add_node("C", noop_work());
    const node_id d = builder.add_node("D", noop_work());
    builder.depends_on(b, a).depends_on(c, a).depends_on(d, b).depends_on(d, c);

    std::shared_ptr<dependency_graph> g;
    const graph_status                status = builder.build(g);

    ASSERT_TRUE(status.ok());
    const auto& order = g->topological_order();
    ASSERT_EQ(order.size(), 4U);
    // a must precede b and c; b and c must both precede d.
    auto index_of = [&order](node_id id)
    { return static_cast<std::size_t>(std::find(order.begin(), order.end(), id) - order.begin()); };
    EXPECT_LT(index_of(a), index_of(b));
    EXPECT_LT(index_of(a), index_of(c));
    EXPECT_LT(index_of(b), index_of(d));
    EXPECT_LT(index_of(c), index_of(d));
}

TEST(GraphBuilder, cycle_is_rejected)
{
    graph_builder builder;
    const node_id a = builder.add_node("A", noop_work());
    const node_id b = builder.add_node("B", noop_work());
    builder.depends_on(b, a).depends_on(a, b);

    std::shared_ptr<dependency_graph> g;
    const graph_status                status = builder.build(g);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code(), graph_build_status_code::cycle_detected);
    EXPECT_EQ(g, nullptr);
}

TEST(GraphBuilder, unknown_node_reference_is_rejected)
{
    graph_builder builder;
    const node_id a = builder.add_node("A", noop_work());
    builder.depends_on(a, /*dependency=*/42);

    std::shared_ptr<dependency_graph> g;
    const graph_status                status = builder.build(g);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code(), graph_build_status_code::unknown_node_reference);
    EXPECT_EQ(g, nullptr);
}

TEST(GraphBuilder, single_node_with_no_edges_builds)
{
    graph_builder builder;
    const node_id a = builder.add_node("A", noop_work());

    std::shared_ptr<dependency_graph> g;
    const graph_status                status = builder.build(g);

    ASSERT_TRUE(status.ok());
    ASSERT_EQ(g->node_count(), 1U);
    EXPECT_EQ(g->topological_order(), (std::vector<node_id>{a}));
}
