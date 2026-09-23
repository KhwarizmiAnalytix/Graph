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

#include "GraphTest.h"
#include "dependency_graph.h"
#include "graph_executor.h"
#include "graph_types.h"
#include "keyed_graph_builder.h"

using namespace graph;

// B depends on A purely because its resolve_fn calls ctx.resolve("A", ...)
// -- no explicit depends_on() call is made by the caller.
TEST(KeyedGraphBuilder, resolving_a_dependency_records_the_edge)
{
    keyed_graph_builder<std::string> kb;

    keyed_graph_builder<std::string>::resolver_fn resolve_a = [](const std::string&,
                                                                 keyed_graph_builder<std::string>&,
                                                                 std::string& out_name,
                                                                 node_work&   out_work)
    {
        out_name = "A";
        out_work = [](const std::vector<std::any>&) -> std::any { return 2; };
        return graph_status::success();
    };
    keyed_graph_builder<std::string>::resolver_fn resolve_b =
        [&](const std::string&,
            keyed_graph_builder<std::string>& ctx,
            std::string&                      out_name,
            node_work&                        out_work)
    {
        node_id            dep_a;
        const graph_status status = ctx.resolve("A", resolve_a, dep_a);
        if (!status.ok())
        {
            return status;
        }
        out_name = "B";
        out_work = [](const std::vector<std::any>& inputs) -> std::any
        { return std::any_cast<int>(inputs[0]) * 10; };
        return graph_status::success();
    };

    node_id a;
    node_id b;
    ASSERT_TRUE(kb.resolve("A", resolve_a, a).ok());
    ASSERT_TRUE(kb.resolve("B", resolve_b, b).ok());

    std::shared_ptr<dependency_graph> g;
    ASSERT_TRUE(kb.build(g).ok());
    ASSERT_EQ(g->node_count(), 2U);
    EXPECT_EQ(g->node_at(b).dependencies_, (std::vector<node_id>{a}));

    graph_executor                        executor;
    std::unordered_map<node_id, std::any> results;
    ASSERT_TRUE(executor.run(*g, results).ok());
    EXPECT_EQ(std::any_cast<int>(results.at(a)), 2);
    EXPECT_EQ(std::any_cast<int>(results.at(b)), 20);
}

// C and D both depend on shared key "S". "S" must be resolved (and its
// resolve_fn invoked) exactly once, and both C and D must end up with an
// edge to the same node_id.
TEST(KeyedGraphBuilder, resolving_the_same_key_twice_is_memoized)
{
    keyed_graph_builder<std::string> kb;
    std::atomic<int>                 shared_resolve_count{0};

    keyed_graph_builder<std::string>::resolver_fn resolve_shared =
        [&](const std::string&,
            keyed_graph_builder<std::string>&,
            std::string& out_name,
            node_work&   out_work)
    {
        ++shared_resolve_count;
        out_name = "S";
        out_work = [](const std::vector<std::any>&) -> std::any { return 7; };
        return graph_status::success();
    };
    auto resolve_dependent = [&](const std::string& name)
    {
        return [&, name](
                   const std::string&,
                   keyed_graph_builder<std::string>& ctx,
                   std::string&                      out_name,
                   node_work&                        out_work)
        {
            node_id            shared;
            const graph_status status = ctx.resolve("S", resolve_shared, shared);
            if (!status.ok())
            {
                return status;
            }
            out_name = name;
            out_work = [](const std::vector<std::any>& inputs) -> std::any
            { return std::any_cast<int>(inputs[0]) + 1; };
            return graph_status::success();
        };
    };

    node_id c;
    node_id d;
    ASSERT_TRUE(kb.resolve("C", resolve_dependent("C"), c).ok());
    ASSERT_TRUE(kb.resolve("D", resolve_dependent("D"), d).ok());

    EXPECT_EQ(shared_resolve_count.load(), 1);

    std::shared_ptr<dependency_graph> g;
    ASSERT_TRUE(kb.build(g).ok());
    ASSERT_EQ(g->node_count(), 3U);  // S, C, D -- not S resolved twice.
    ASSERT_EQ(g->node_at(c).dependencies_.size(), 1U);
    ASSERT_EQ(g->node_at(d).dependencies_.size(), 1U);
    EXPECT_EQ(g->node_at(c).dependencies_[0], g->node_at(d).dependencies_[0]);
}

// A's resolve_fn resolves B, whose resolve_fn resolves A again: an
// unbounded recursion that resolve() must reject instead of recursing
// forever.
TEST(KeyedGraphBuilder, resolution_cycle_is_rejected)
{
    keyed_graph_builder<std::string> kb;

    keyed_graph_builder<std::string>::resolver_fn resolve_a;
    keyed_graph_builder<std::string>::resolver_fn resolve_b;
    resolve_a =
        [&](const std::string&, keyed_graph_builder<std::string>& ctx, std::string&, node_work&)
    {
        node_id dep;
        return ctx.resolve("B", resolve_b, dep);
    };
    resolve_b =
        [&](const std::string&, keyed_graph_builder<std::string>& ctx, std::string&, node_work&)
    {
        node_id dep;
        return ctx.resolve("A", resolve_a, dep);
    };

    node_id            a;
    const graph_status status = kb.resolve("A", resolve_a, a);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code(), graph_build_status_code::cycle_detected);
}

// A dependency's resolve_fn failing (for a reason unrelated to a cycle)
// must propagate up through the resolve() call that depends on it, without
// adding any node for either key.
TEST(KeyedGraphBuilder, dependency_resolution_failure_propagates)
{
    keyed_graph_builder<std::string> kb;

    keyed_graph_builder<std::string>::resolver_fn resolve_missing =
        [](const std::string&, keyed_graph_builder<std::string>&, std::string&, node_work&)
    {
        return graph_status::failure(graph_build_status_code::unknown_node_reference, "no builder");
    };
    keyed_graph_builder<std::string>::resolver_fn resolve_dependent =
        [&](const std::string&,
            keyed_graph_builder<std::string>& ctx,
            std::string&                      out_name,
            node_work&                        out_work)
    {
        node_id            dep;
        const graph_status status = ctx.resolve("missing", resolve_missing, dep);
        if (!status.ok())
        {
            return status;
        }
        out_name = "unused";
        out_work = [](const std::vector<std::any>&) -> std::any { return 0; };
        return graph_status::success();
    };

    node_id            id;
    const graph_status status = kb.resolve("dependent", resolve_dependent, id);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code(), graph_build_status_code::unknown_node_reference);

    std::shared_ptr<dependency_graph> g;
    ASSERT_TRUE(kb.build(g).ok());
    EXPECT_EQ(g->node_count(), 0U);
}

// A diamond assembled purely through nested resolve() calls (no explicit
// depends_on()) still executes to the correct combined result -- the
// end-to-end trace-then-execute story this class exists for.
TEST(KeyedGraphBuilder, diamond_traced_through_resolve_executes_correctly)
{
    keyed_graph_builder<std::string> kb;

    keyed_graph_builder<std::string>::resolver_fn resolve_base =
        [](const std::string&,
           keyed_graph_builder<std::string>&,
           std::string& out_name,
           node_work&   out_work)
    {
        out_name = "base";
        out_work = [](const std::vector<std::any>&) -> std::any { return 2; };
        return graph_status::success();
    };
    auto make_multiplier = [&](const std::string& name, int factor)
    {
        return [&, name, factor](
                   const std::string&,
                   keyed_graph_builder<std::string>& ctx,
                   std::string&                      out_name,
                   node_work&                        out_work)
        {
            node_id            base;
            const graph_status status = ctx.resolve("base", resolve_base, base);
            if (!status.ok())
            {
                return status;
            }
            out_name = name;
            out_work = [factor](const std::vector<std::any>& inputs) -> std::any
            { return std::any_cast<int>(inputs[0]) * factor; };
            return graph_status::success();
        };
    };
    keyed_graph_builder<std::string>::resolver_fn resolve_times10  = make_multiplier("x10", 10);
    keyed_graph_builder<std::string>::resolver_fn resolve_times100 = make_multiplier("x100", 100);
    keyed_graph_builder<std::string>::resolver_fn resolve_sum =
        [&](const std::string&,
            keyed_graph_builder<std::string>& ctx,
            std::string&                      out_name,
            node_work&                        out_work)
    {
        node_id      times10;
        node_id      times100;
        graph_status status = ctx.resolve("x10", resolve_times10, times10);
        if (!status.ok())
        {
            return status;
        }
        status = ctx.resolve("x100", resolve_times100, times100);
        if (!status.ok())
        {
            return status;
        }
        out_name = "sum";
        out_work = [](const std::vector<std::any>& inputs) -> std::any
        { return std::any_cast<int>(inputs[0]) + std::any_cast<int>(inputs[1]); };
        return graph_status::success();
    };

    node_id sum;
    ASSERT_TRUE(kb.resolve("sum", resolve_sum, sum).ok());

    std::shared_ptr<dependency_graph> g;
    ASSERT_TRUE(kb.build(g).ok());
    ASSERT_EQ(g->node_count(), 4U);  // base, x10, x100, sum

    graph_executor                        executor;
    std::unordered_map<node_id, std::any> results;
    ASSERT_TRUE(executor.run(*g, results).ok());
    EXPECT_EQ(std::any_cast<int>(results.at(sum)), 220);
}

// Models the market-data-builder pattern this class is designed to replace:
// a cross-currency discount curve (build_curve<discount_curve_id> in a
// pretorian-style curve_calibration_gb.cxx) whose calibration instruments
// need a foreign-currency discount curve and an FX spot rate, but where
// discovering those needs is itself a "do { ids = discover(...); ... }
// while (!ids.empty())" loop: the FX leg is visible on the first pass, and
// only once it's accounted for does the walk reveal that the foreign
// discount curve is needed too. Exercises resolve() called repeatedly, with
// a changing key set, from inside a single resolver_fn -- and that the
// resulting node still executes to the right calibrated value.
TEST(KeyedGraphBuilder, discount_curve_style_iterative_discovery_resolves_and_executes)
{
    keyed_graph_builder<std::string> kb;

    keyed_graph_builder<std::string>::resolver_fn resolve_usd_discount =
        [](const std::string&,
           keyed_graph_builder<std::string>&,
           std::string& out_name,
           node_work&   out_work)
    {
        out_name = "USD_DISCOUNT";
        // Stands in for a single-currency bootstrap producing a discount factor.
        out_work = [](const std::vector<std::any>&) -> std::any { return 100.0; };
        return graph_status::success();
    };
    keyed_graph_builder<std::string>::resolver_fn resolve_eurusd_fx =
        [](const std::string&,
           keyed_graph_builder<std::string>&,
           std::string& out_name,
           node_work&   out_work)
    {
        out_name = "EURUSD_FX";
        out_work = [](const std::vector<std::any>&) -> std::any { return 1.1; };
        return graph_status::success();
    };
    const std::unordered_map<std::string, keyed_graph_builder<std::string>::resolver_fn>
        leaf_resolvers = {{"USD_DISCOUNT", resolve_usd_discount}, {"EURUSD_FX", resolve_eurusd_fx}};

    keyed_graph_builder<std::string>::resolver_fn resolve_eur_discount =
        [&](const std::string&,
            keyed_graph_builder<std::string>& ctx,
            std::string&                      out_name,
            node_work&                        out_work)
    {
        // Discovery reveals more each pass, exactly like
        // curve_calibration::discover() widening what it can see as more of
        // the market becomes known: pass 0 only sees the FX leg, pass 1 (now
        // that FX is accounted for) also sees the foreign discount curve,
        // pass 2 finds nothing new and the loop stops.
        static const std::vector<std::vector<std::string>> discovery_schedule = {
            {"EURUSD_FX"}, {"USD_DISCOUNT"}, {}};

        std::vector<std::string> ids;
        std::size_t              pass = 0;
        do
        {
            ids = discovery_schedule[pass++];
            for (const auto& dep : ids)
            {
                node_id            dep_id;
                const graph_status status = ctx.resolve(dep, leaf_resolvers.at(dep), dep_id);
                if (!status.ok())
                {
                    return status;
                }
            }
        } while (!ids.empty());

        out_name = "EUR_DISCOUNT";
        out_work = [](const std::vector<std::any>& inputs) -> std::any
        {
            // inputs arrive in resolve() call order: [EURUSD_FX, USD_DISCOUNT].
            return std::any_cast<double>(inputs[0]) * std::any_cast<double>(inputs[1]);
        };
        return graph_status::success();
    };

    node_id eur;
    ASSERT_TRUE(kb.resolve("EUR_DISCOUNT", resolve_eur_discount, eur).ok());

    std::shared_ptr<dependency_graph> g;
    ASSERT_TRUE(kb.build(g).ok());
    ASSERT_EQ(g->node_count(), 3U);  // EURUSD_FX, USD_DISCOUNT, EUR_DISCOUNT
    ASSERT_EQ(g->node_at(eur).dependencies_.size(), 2U);
    EXPECT_EQ(g->node_at(g->node_at(eur).dependencies_[0]).name_, "EURUSD_FX");
    EXPECT_EQ(g->node_at(g->node_at(eur).dependencies_[1]).name_, "USD_DISCOUNT");

    graph_executor                        executor;
    std::unordered_map<node_id, std::any> results;
    ASSERT_TRUE(executor.run(*g, results).ok());
    EXPECT_DOUBLE_EQ(std::any_cast<double>(results.at(eur)), 110.0);
}

// The same cross-currency discount-curve shape as the test above, but
// disentangled the way resolve_with_discovery() is meant to be used:
// discover_fn only ever answers "what do I still need" (using
// ctx.is_resolved() the same way curve_calibration::discover() uses
// market.contains()), and build_fn only ever answers "what do I compute".
// resolve_with_discovery() itself drives the do-while loop that the test
// above had to hand-roll inside a single resolver_fn -- and, checking
// candidates in order the same way the pretorian-style discover() does,
// USD_DISCOUNT is only revealed once EURUSD_FX is already resolved, without
// either function needing to know that ordering was ever a hand-authored
// schedule.
TEST(KeyedGraphBuilder, discount_curve_style_discovery_and_build_are_disentangled)
{
    using kb_t = keyed_graph_builder<std::string>;
    kb_t kb;

    const kb_t::discover_fn no_further_discovery = [](const std::string&, const kb_t&)
    { return std::vector<std::string>{}; };

    const kb_t::build_fn build_usd_discount =
        [](const std::string&, std::string& out_name, node_work& out_work)
    {
        out_name = "USD_DISCOUNT";
        out_work = [](const std::vector<std::any>&) -> std::any { return 100.0; };
        return graph_status::success();
    };
    const kb_t::build_fn build_eurusd_fx =
        [](const std::string&, std::string& out_name, node_work& out_work)
    {
        out_name = "EURUSD_FX";
        out_work = [](const std::vector<std::any>&) -> std::any { return 1.1; };
        return graph_status::success();
    };

    const kb_t::discover_fn discover_eur_discount = [](const std::string&, const kb_t& ctx)
    {
        if (!ctx.is_resolved("EURUSD_FX"))
        {
            return std::vector<std::string>{"EURUSD_FX"};
        }
        if (!ctx.is_resolved("USD_DISCOUNT"))
        {
            return std::vector<std::string>{"USD_DISCOUNT"};
        }
        return std::vector<std::string>{};
    };
    const kb_t::build_fn build_eur_discount =
        [](const std::string&, std::string& out_name, node_work& out_work)
    {
        out_name = "EUR_DISCOUNT";
        out_work = [](const std::vector<std::any>& inputs) -> std::any
        {
            // inputs arrive in discovery order: [EURUSD_FX, USD_DISCOUNT].
            return std::any_cast<double>(inputs[0]) * std::any_cast<double>(inputs[1]);
        };
        return graph_status::success();
    };

    // Plays the role of GenericBuilder's type-name-keyed registry: looked up
    // once per key encountered, at any recursion depth.
    const kb_t::resolver_provider provider = [&](const std::string& key)
    {
        if (key == "EUR_DISCOUNT")
        {
            return std::make_pair(discover_eur_discount, build_eur_discount);
        }
        if (key == "USD_DISCOUNT")
        {
            return std::make_pair(no_further_discovery, build_usd_discount);
        }
        return std::make_pair(no_further_discovery, build_eurusd_fx);
    };

    node_id eur;
    ASSERT_TRUE(kb.resolve_with_discovery("EUR_DISCOUNT", provider, eur).ok());

    std::shared_ptr<dependency_graph> g;
    ASSERT_TRUE(kb.build(g).ok());
    ASSERT_EQ(g->node_count(), 3U);  // EURUSD_FX, USD_DISCOUNT, EUR_DISCOUNT
    ASSERT_EQ(g->node_at(eur).dependencies_.size(), 2U);
    EXPECT_EQ(g->node_at(g->node_at(eur).dependencies_[0]).name_, "EURUSD_FX");
    EXPECT_EQ(g->node_at(g->node_at(eur).dependencies_[1]).name_, "USD_DISCOUNT");

    graph_executor                        executor;
    std::unordered_map<node_id, std::any> results;
    ASSERT_TRUE(executor.run(*g, results).ok());
    EXPECT_DOUBLE_EQ(std::any_cast<double>(results.at(eur)), 110.0);
}
