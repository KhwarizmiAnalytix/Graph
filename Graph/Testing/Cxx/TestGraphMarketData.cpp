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
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "GraphTest.h"
#include "dependency_graph.h"
#include "graph_builder.h"
#include "graph_executor.h"
#include "graph_types.h"
#include "keyed_graph_builder.h"

using namespace graph;

// A realistic quantitative-finance graph, traced lazily through
// keyed_graph_builder the way a market-data stack resolves a curve's
// dependencies on demand (the pretorian / XSIGMA_REGISTER_GENERIC_BUILDER
// pattern keyed_graph_builder.h's doc comment references).
//
// Topology (data flows downward):
//
//   fx_spot      usd_deposit   ois_quotes
//      \            /              |
//       discount_curve        ois_curve
//             \                  /
//              swap_pricing_engine
//                      |
//                  portfolio_npv
//
// Each node is a small value type; results flow as std::any. The point is
// not the math (kept trivially checkable) but that a real caller resolves
// "what does pricing need" recursively and lets the builder record the
// edges, then a multi-threaded executor runs the frozen graph.

namespace
{

struct fx_spot
{
    double rate;
};

struct deposit_quotes
{
    double rate;
    double maturity_years;
};

struct ois_quotes
{
    double fixed_rate;
    int    tenor_count;
};

struct discount_curve
{
    double df_1y;
};

struct ois_curve
{
    double df_1y;
    int    tenor_count;
};

struct pricing_engine
{
    double fx_rate;
    double domestic_df;
    double ois_df;
};

struct portfolio_npv
{
    double value;
};

// Resolver for one market-data key. Calls ctx.resolve() on each dependency
// in the order the node's inputs should receive them, then fills out_name /
// out_work. This is the trace step: the edge is recorded by resolve().
//
// The resolver is passed explicitly (rather than hard-coded) so tests can
// wrap it -- e.g. to count how many times a shared dependency is resolved,
// or to make one node fail -- and have the wrapper apply to nested
// ctx.resolve() calls too, not just the top-level key.
graph_status resolve_market_node_with(
    const std::string& key, keyed_graph_builder<std::string>& ctx, std::string& out_name,
    node_work& out_work, const keyed_graph_builder<std::string>::resolver_fn& self)
{
    out_name = key;

    // Helper: resolve one dependency, propagating failure.
    auto need = [&ctx, &self](const char* dep, const char* owner) -> graph_status
    {
        node_id            id;
        const graph_status s = ctx.resolve(std::string(dep), self, id);
        if (!s.ok())
        {
            return graph_status::failure(
                graph_build_status_code::unknown_node_reference,
                std::string(owner) + " dependency \"" + dep + "\" failed to resolve");
        }
        return graph_status::success();
    };

    if (key == "fx_spot")
    {
        out_work = [](const std::vector<std::any>&) -> std::any { return fx_spot{1.08}; };
        return graph_status::success();
    }
    if (key == "usd_deposit")
    {
        out_work = [](const std::vector<std::any>&) -> std::any
        { return deposit_quotes{0.052, 1.0}; };
        return graph_status::success();
    }
    if (key == "ois_quotes")
    {
        out_work = [](const std::vector<std::any>&) -> std::any
        { return ois_quotes{0.048, 4}; };
        return graph_status::success();
    }
    if (key == "discount_curve")
    {
        if (const graph_status s = need("fx_spot", "discount_curve"); !s.ok())
        {
            return s;
        }
        if (const graph_status s = need("usd_deposit", "discount_curve"); !s.ok())
        {
            return s;
        }
        out_work = [](const std::vector<std::any>& inputs) -> std::any
        {
            const auto& fx  = std::any_cast<const fx_spot&>(inputs[0]);
            const auto& dep = std::any_cast<const deposit_quotes&>(inputs[1]);
            // Toy bootstrap: flat DF from the deposit rate, scaled by FX.
            return discount_curve{std::exp(-dep.rate * dep.maturity_years) * fx.rate};
        };
        return graph_status::success();
    }
    if (key == "ois_curve")
    {
        if (const graph_status s = need("ois_quotes", "ois_curve"); !s.ok())
        {
            return s;
        }
        out_work = [](const std::vector<std::any>& inputs) -> std::any
        {
            const auto& q = std::any_cast<const ois_quotes&>(inputs[0]);
            return ois_curve{std::exp(-q.fixed_rate), q.tenor_count};
        };
        return graph_status::success();
    }
    if (key == "swap_pricing_engine")
    {
        if (const graph_status s = need("fx_spot", "swap_pricing_engine"); !s.ok())
        {
            return s;
        }
        if (const graph_status s = need("discount_curve", "swap_pricing_engine"); !s.ok())
        {
            return s;
        }
        if (const graph_status s = need("ois_curve", "swap_pricing_engine"); !s.ok())
        {
            return s;
        }
        out_work = [](const std::vector<std::any>& inputs) -> std::any
        {
            const auto& fx  = std::any_cast<const fx_spot&>(inputs[0]);
            const auto& dom = std::any_cast<const discount_curve&>(inputs[1]);
            const auto& ois = std::any_cast<const ois_curve&>(inputs[2]);
            return pricing_engine{fx.rate, dom.df_1y, ois.df_1y};
        };
        return graph_status::success();
    }
    if (key == "portfolio_npv")
    {
        if (const graph_status s = need("swap_pricing_engine", "portfolio_npv"); !s.ok())
        {
            return s;
        }
        out_work = [](const std::vector<std::any>& inputs) -> std::any
        {
            const auto& eng = std::any_cast<const pricing_engine&>(inputs[0]);
            // Toy NPV: notional * (domestic DF - OIS DF) in domestic ccy.
            return portfolio_npv{1'000'000.0 * (eng.domestic_df - eng.ois_df)};
        };
        return graph_status::success();
    }

    return graph_status::failure(
        graph_build_status_code::unknown_node_reference, "no resolver for key \"" + key + "\"");
}

// The plain resolver: resolves each key with itself as the nested resolver.
graph_status resolve_market_node(
    const std::string& key, keyed_graph_builder<std::string>& ctx, std::string& out_name,
    node_work& out_work)
{
    return resolve_market_node_with(key, ctx, out_name, out_work, resolve_market_node);
}

// Builds the market-data graph by tracing from the sink. Returns the graph
// and the node id of each key for result lookup.
std::shared_ptr<dependency_graph> build_market_graph(std::unordered_map<std::string, node_id>& ids)
{
    keyed_graph_builder<std::string> builder;
    std::shared_ptr<dependency_graph> g;

    node_id sink_id;
    if (!builder.resolve(std::string("portfolio_npv"), resolve_market_node, sink_id).ok())
    {
        return nullptr;
    }
    if (!builder.build(g).ok())
    {
        return nullptr;
    }

    // Re-derive each key's node id by resolving again -- resolve() memoizes,
    // so these are cheap lookups, not re-traces.
    for (const char* key :
         {"fx_spot", "usd_deposit", "ois_quotes", "discount_curve", "ois_curve",
          "swap_pricing_engine", "portfolio_npv"})
    {
        node_id id;
        if (!builder.resolve(std::string(key), resolve_market_node, id).ok())
        {
            return nullptr;
        }
        ids[key] = id;
    }
    return g;
}

}  // namespace

// The traced graph has the expected shape: 7 nodes, and the sink's inputs
// arrive in declared resolve() order (fx, discount, ois).
TEST(GraphMarketData, traced_curve_graph_builds_and_executes)
{
    std::unordered_map<std::string, node_id> ids;
    std::shared_ptr<dependency_graph>        g = build_market_graph(ids);
    ASSERT_NE(g, nullptr);
    ASSERT_EQ(g->node_count(), 7U);

    graph_executor                        executor;
    std::unordered_map<node_id, std::any> results;
    ASSERT_TRUE(executor.run(*g, results).ok());

    const auto& fx  = std::any_cast<const fx_spot&>(results.at(ids["fx_spot"]));
    const auto& dep = std::any_cast<const deposit_quotes&>(results.at(ids["usd_deposit"]));
    const auto& ois = std::any_cast<const ois_quotes&>(results.at(ids["ois_quotes"]));
    const auto& dom = std::any_cast<const discount_curve&>(results.at(ids["discount_curve"]));
    const auto& oisc = std::any_cast<const ois_curve&>(results.at(ids["ois_curve"]));
    const auto& eng = std::any_cast<const pricing_engine&>(results.at(ids["swap_pricing_engine"]));
    const auto& npv = std::any_cast<const portfolio_npv&>(results.at(ids["portfolio_npv"]));

    EXPECT_DOUBLE_EQ(fx.rate, 1.08);
    EXPECT_DOUBLE_EQ(dep.rate, 0.052);
    EXPECT_EQ(ois.tenor_count, 4);

    const double expected_dom = std::exp(-0.052 * 1.0) * 1.08;
    const double expected_ois = std::exp(-0.048);
    EXPECT_DOUBLE_EQ(dom.df_1y, expected_dom);
    EXPECT_DOUBLE_EQ(oisc.df_1y, expected_ois);

    EXPECT_DOUBLE_EQ(eng.fx_rate, 1.08);
    EXPECT_DOUBLE_EQ(eng.domestic_df, expected_dom);
    EXPECT_DOUBLE_EQ(eng.ois_df, expected_ois);

    EXPECT_NEAR(npv.value, 1'000'000.0 * (expected_dom - expected_ois), 1e-9);
}

// A shared dependency (fx_spot feeds both discount_curve and
// swap_pricing_engine) must be resolved once and its single result shared
// by both consumers, not recomputed or copied into two nodes.
TEST(GraphMarketData, shared_fx_dependency_is_memoized_and_shared)
{
    std::atomic<int> fx_resolve_count{0};

    // Wrap the market resolver so every nested ctx.resolve() call routes
    // through the counting wrapper (resolve_market_node_with passes this
    // same wrapper as the nested resolver, not the bare one -- otherwise
    // the count would only see the top-level key).
    keyed_graph_builder<std::string>             builder;
    keyed_graph_builder<std::string>::resolver_fn counting_resolver;
    counting_resolver =
        [&fx_resolve_count, &counting_resolver](
            const std::string& key, keyed_graph_builder<std::string>& ctx, std::string& out_name,
            node_work& out_work) -> graph_status
    {
        if (key == "fx_spot")
        {
            ++fx_resolve_count;
        }
        return resolve_market_node_with(key, ctx, out_name, out_work, counting_resolver);
    };

    node_id sink_id;
    ASSERT_TRUE(builder.resolve(std::string("portfolio_npv"), counting_resolver, sink_id).ok());

    std::shared_ptr<dependency_graph> g;
    ASSERT_TRUE(builder.build(g).ok());

    // fx_spot is needed by discount_curve and by swap_pricing_engine, but
    // resolve() memoizes, so its resolver ran exactly once.
    EXPECT_EQ(fx_resolve_count.load(), 1);

    graph_executor                        executor;
    std::unordered_map<node_id, std::any> results;
    ASSERT_TRUE(executor.run(*g, results).ok());

    node_id fx_id;
    ASSERT_TRUE(builder.resolve(std::string("fx_spot"), counting_resolver, fx_id).ok());
    EXPECT_DOUBLE_EQ(std::any_cast<const fx_spot&>(results.at(fx_id)).rate, 1.08);
}

// The same frozen graph can be executed more than once on the same
// executor (the persistent pool is reused), and results are identical.
TEST(GraphMarketData, same_graph_runs_repeatedly_on_one_executor)
{
    std::unordered_map<std::string, node_id> ids;
    std::shared_ptr<dependency_graph>        g = build_market_graph(ids);
    ASSERT_NE(g, nullptr);

    graph_executor executor(graph_executor_options{/*number_of_threads=*/4});

    double first_npv = 0.0;
    for (int run = 0; run < 3; ++run)
    {
        std::unordered_map<node_id, std::any> results;
        ASSERT_TRUE(executor.run(*g, results).ok()) << "run " << run;
        const auto& npv =
            std::any_cast<const portfolio_npv&>(results.at(ids["portfolio_npv"]));
        if (run == 0)
        {
            first_npv = npv.value;
        }
        else
        {
            EXPECT_DOUBLE_EQ(npv.value, first_npv) << "run " << run;
        }
    }
}

// A failing market-data node (bad quote) propagates: the sink is skipped
// and the status names the failed node.
TEST(GraphMarketData, bad_quote_fails_and_skips_pricing)
{
    keyed_graph_builder<std::string>             builder;
    keyed_graph_builder<std::string>::resolver_fn failing_resolver;
    failing_resolver =
        [&failing_resolver](
            const std::string& key, keyed_graph_builder<std::string>& ctx, std::string& out_name,
            node_work& out_work) -> graph_status
    {
        if (key == "usd_deposit")
        {
            out_name = key;
            out_work = [](const std::vector<std::any>&) -> std::any
            { throw std::runtime_error("negative deposit rate"); };
            return graph_status::success();
        }
        // Nested resolves must route through this wrapper too, so a node
        // that depends on usd_deposit sees the failing work, not the good
        // one the bare resolver would produce.
        return resolve_market_node_with(key, ctx, out_name, out_work, failing_resolver);
    };

    node_id sink_id;
    ASSERT_TRUE(builder.resolve(std::string("portfolio_npv"), failing_resolver, sink_id).ok());

    std::shared_ptr<dependency_graph> g;
    ASSERT_TRUE(builder.build(g).ok());

    graph_executor                        executor;
    std::unordered_map<node_id, std::any> results;
    const graph_execution_status          status = executor.run(*g, results);

    ASSERT_FALSE(status.ok());
    EXPECT_EQ(status.code(), graph_execution_status_code::node_failed);

    node_id usd_id;
    ASSERT_TRUE(builder.resolve(std::string("usd_deposit"), failing_resolver, usd_id).ok());
    EXPECT_EQ(status.failed_node(), usd_id);
    EXPECT_NE(status.message().find("negative deposit rate"), std::string::npos);

    // Every node still has an entry; the failed and downstream ones are empty.
    ASSERT_EQ(results.size(), 7U);
    node_id discount_id, engine_id, npv_id, ois_curve_id;
    ASSERT_TRUE(builder.resolve(std::string("discount_curve"), failing_resolver, discount_id).ok());
    ASSERT_TRUE(builder.resolve(std::string("swap_pricing_engine"), failing_resolver, engine_id).ok());
    ASSERT_TRUE(builder.resolve(std::string("portfolio_npv"), failing_resolver, npv_id).ok());
    ASSERT_TRUE(builder.resolve(std::string("ois_curve"), failing_resolver, ois_curve_id).ok());

    EXPECT_FALSE(results.at(discount_id).has_value());
    EXPECT_FALSE(results.at(engine_id).has_value());
    EXPECT_FALSE(results.at(npv_id).has_value());
    // The independent ois branch still ran.
    EXPECT_TRUE(results.at(ois_curve_id).has_value());
}
