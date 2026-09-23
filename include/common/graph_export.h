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

#if defined(GRAPH_STATIC_DEFINE)
#define GRAPH_API
#define GRAPH_VISIBILITY

#elif defined(GRAPH_SHARED_DEFINE)
#if defined(_WIN32) || defined(__CYGWIN__)
#ifdef GRAPH_BUILDING_DLL
#define GRAPH_API __declspec(dllexport)
#else
#define GRAPH_API __declspec(dllimport)
#endif
#define GRAPH_VISIBILITY
#elif defined(__GNUC__) && __GNUC__ >= 4
#define GRAPH_API __attribute__((visibility("default")))
#define GRAPH_VISIBILITY __attribute__((visibility("default")))
#else
#define GRAPH_API
#define GRAPH_VISIBILITY
#endif

#else
#define GRAPH_API
#define GRAPH_VISIBILITY
#endif
