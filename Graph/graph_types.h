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
#include <functional>
#include <string>
#include <vector>

namespace graph
{

/// Dense, sequentially-assigned identifier for a node within one graph_builder.
using node_id = std::size_t;

/// A node's unit of work. Receives its dependencies' results, in the order
/// those dependencies were declared via graph_builder::depends_on, and
/// returns this node's own (type-erased) result.
using node_work = std::function<std::any(const std::vector<std::any>& inputs)>;

/// Outcome of graph_builder::build().
enum class graph_build_status_code
{
    ok,
    cycle_detected,
    unknown_node_reference
};

/**
 * @brief Result of graph_builder::build().
 *
 * A small status/result type in place of exceptions (root /CLAUDE.md's
 * error-handling default for new application code). Factory methods are
 * named success()/failure() rather than profiler::profiler_status's
 * Ok()/Error() precedent (ThirdParty/Profiler/native/core/profiler_status.h):
 * that PascalCase naming does not itself follow the project's snake_case
 * function convention, so it isn't repeated here.
 */
class graph_status
{
public:
    static graph_status success() { return graph_status(graph_build_status_code::ok, {}); }

    static graph_status failure(graph_build_status_code code, std::string message)
    {
        return graph_status(code, std::move(message));
    }

    bool                    ok() const { return code_ == graph_build_status_code::ok; }
    graph_build_status_code code() const { return code_; }
    const std::string&      message() const { return message_; }

    explicit operator bool() const { return ok(); }

private:
    graph_status(graph_build_status_code code, std::string message)
        : code_(code), message_(std::move(message))
    {
    }

    graph_build_status_code code_;
    std::string             message_;
};

/// Outcome of graph_executor::run().
enum class graph_execution_status_code
{
    ok,
    node_failed,
    cancelled
};

/**
 * @brief Result of graph_executor::run().
 *
 * Node work functions may now throw; graph_executor catches the first
 * exception (per run() call) instead of letting it escape a worker thread
 * (which would otherwise call std::terminate() -- see
 * graph_executor.h's class doc comment). failed_node()/message() identify
 * that first failure; every node depending, directly or transitively, on
 * the failed node is skipped rather than run with incomplete inputs.
 */
class graph_execution_status
{
public:
    static graph_execution_status success()
    {
        return graph_execution_status(graph_execution_status_code::ok, 0, {});
    }

    static graph_execution_status failure(node_id failed_node, std::string message)
    {
        return graph_execution_status(
            graph_execution_status_code::node_failed, failed_node, std::move(message));
    }

    static graph_execution_status cancelled(std::string message)
    {
        return graph_execution_status(
            graph_execution_status_code::cancelled, 0, std::move(message));
    }

    bool                        ok() const { return code_ == graph_execution_status_code::ok; }
    graph_execution_status_code code() const { return code_; }
    node_id                     failed_node() const { return failed_node_; }
    const std::string&          message() const { return message_; }

    explicit operator bool() const { return ok(); }

private:
    graph_execution_status(
        graph_execution_status_code code, node_id failed_node, std::string message)
        : code_(code), failed_node_(failed_node), message_(std::move(message))
    {
    }

    graph_execution_status_code code_;
    node_id                     failed_node_;
    std::string                 message_;
};

}  // namespace graph
