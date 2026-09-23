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

#include <cstdlib>

#include <gtest/gtest.h>

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define XSIGMA_GRAPH_TEST_HAS_ASAN 1
#endif
#endif
#if !defined(XSIGMA_GRAPH_TEST_HAS_ASAN) && defined(__SANITIZE_ADDRESS__)
#define XSIGMA_GRAPH_TEST_HAS_ASAN 1
#endif

// GraphCxxTests transitively links libtbb (Graph -> Parallel::Parallel, which PUBLIC-links
// Tbb::tbb whenever PARALLEL_ENABLE_TBB=ON) even though Graph's own executor never calls a
// single TBB API -- graph_executor.cpp builds its thread pool from plain std::thread (see
// threaded_callback_queue.cpp). On macOS/arm64 under AddressSanitizer, this binary has
// reproducibly crashed (confirmed across independent CI runs, always this target, never any
// of the other TBB-linked test binaries built in the same job) during ordinary process exit
// with "AddressSanitizer: SEGV ... in tbb::detail::r1::__TBB_InitOnce::~__TBB_InitOnce()"
// after every gtest case had already reported PASSED -- i.e. the crash is entirely within
// (Homebrew-packaged, not vendored in ThirdParty/) TBB's own static-destruction teardown,
// unrelated to anything Graph's tests exercise. This matches a long-standing oneTBB
// limitation where a process that links TBB without ever starting its scheduler can race
// with the library's own exit-time cleanup (see uxlfoundation/oneTBB issue #977).
//
// RUN_ALL_TESTS() has already reported every assertion by the time this would matter, and
// AddressSanitizer aborts the process immediately (well before this line) on any real defect
// it detects during the run -- so the only thing skipped by terminating via _Exit() instead
// of falling through to static destructors is this known-bad, unrelated third-party teardown
// path. Scoped to Apple+ASan specifically: macOS does not support LeakSanitizer's exit-time
// checks at all (see the CI workflow's own exclusion of the "leak" sanitizer for macOS), so
// no leak-detection coverage is lost by skipping normal exit here.
int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    const int result = RUN_ALL_TESTS();
#if defined(__APPLE__) && defined(XSIGMA_GRAPH_TEST_HAS_ASAN)
    std::_Exit(result);
#endif
    return result;
}
