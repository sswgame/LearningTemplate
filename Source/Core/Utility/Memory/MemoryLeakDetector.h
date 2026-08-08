#pragma once
/**
 * @file MemoryLeakDetector.h
 * @brief Cross-platform entry points for native leak checkers.
 *
 * Not a custom allocator. Delegates to:
 * - Windows Debug: CRT debug heap (_CrtSetDbgFlag / _CrtDumpMemoryLeaks)
 * - Linux/macOS Debug + ASan: LeakSanitizer (__lsan_do_leak_check)
 * - Otherwise: Debug guidance for Valgrind / SW_ENABLE_SANITIZER
 */

#include "Core/Common/CommonMacros.h"
#include "Core/Common/Types.h"

namespace sw
{
	/** @brief Enable platform leak tracking early in process startup. */
	SW_API void enableMemoryLeakChecks();

	/**
	 * @brief Run the platform leak report (call after full teardown).
	 * @return Windows CRT: _CrtDumpMemoryLeaks result; otherwise 0.
	 */
	SW_API int32 reportMemoryLeaks( const char* phaseTag = "shutdown" );
} // namespace sw
