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

#ifndef GRAPH_PORTABLE_MACROS_INCLUDED_
#define GRAPH_PORTABLE_MACROS_INCLUDED_

#if defined(_MSC_VER) && _MSC_VER < 1910
#error GRAPH requires MSVC++ 15.0 (Visual Studio 2017) or newer for C++17 support
#endif

#if !defined(__clang__) && defined(__GNUC__) && \
    (__GNUC__ < 7 || (__GNUC__ == 7 && __GNUC_MINOR__ < 1))
#error GRAPH requires GCC 7.1 or newer for C++17 support
#endif

#if defined(__clang__) && (__clang_major__ < 5)
#error GRAPH requires Clang 5.0 or newer for C++17 support
#endif

#if defined(_MSC_VER)
#define GRAPH_FORCE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define GRAPH_FORCE_INLINE inline __attribute__((always_inline))
#else
#define GRAPH_FORCE_INLINE inline
#endif

#if __cplusplus >= 201703L
#define GRAPH_UNUSED [[maybe_unused]]
#elif defined(__GNUC__) || defined(__clang__)
#define GRAPH_UNUSED __attribute__((unused))
#elif defined(_MSC_VER)
#define GRAPH_UNUSED __pragma(warning(suppress : 4100))
#else
#define GRAPH_UNUSED
#endif

#endif  // GRAPH_PORTABLE_MACROS_INCLUDED_
