#pragma once
/**
 * @file MemoryLeakDetector.h
 * @brief Cross-platform entry points for native leak checkers.
 *
 * Not a custom allocator. Delegates to:
 * - Windows Debug: CRT debug heap (post-init baseline vs shutdown size)
 * - Linux/macOS Debug + ASan: LeakSanitizer (__lsan_do_leak_check)
 * - Otherwise: Debug guidance for Valgrind / SW_ENABLE_SANITIZER
 *
 * Call order:
 * 1) enableMemoryLeakChecks() early in startup
 * 2) captureMemoryLeakBaseline() after App init
 * 3) Tear down all owned state, then reportMemoryLeaks()
 *
 * Windows note: after teardown, _CrtMemDumpAllObjectsSince(baseline) is unsafe
 * (checkpoint block headers may have been freed). We only flag a leak when the
 * heap is still larger than the post-init baseline.
 */

#include "Core/Common/CommonMacros.h"
#include "Core/Common/Types.h"

namespace sw
{
	/** @brief Enable platform leak tracking early in process startup. */
	SW_API void enableMemoryLeakChecks();

	/**
	 * @brief Snapshot heap after intentional process-lifetime allocations.
	 * @details Subsequent reportMemoryLeaks() compares total heap size to this snapshot.
	 */
	SW_API void captureMemoryLeakBaseline();

	/**
	 * @brief Run the platform leak report (call after full teardown).
	 * @return Non-zero if the heap grew vs baseline (Windows) / otherwise 0.
	 */
	SW_API int32 reportMemoryLeaks( const utf8* phaseTag = "shutdown" );
} // namespace sw
