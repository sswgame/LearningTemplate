/**
 * @file MemoryLeakDetector.cpp
 * @brief Thin wrappers around CRT / LSan (no global new/delete hooks).
 */
#include "MemoryLeakDetector.h"

#include <cstdio>

#if defined( SW_PLATFORM_WINDOWS ) && defined( SW_DEBUG ) && !defined( SW_SHIPPING )
	#define SW_HAS_CRT_LEAK_CHECK 1
	#include <crtdbg.h>
#elif ( defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_MACOS ) ) && defined( SW_DEBUG ) && !defined( SW_SHIPPING )
	#if defined( __has_feature )
		#if __has_feature( address_sanitizer )
			#define SW_HAS_LSAN_LEAK_CHECK 1
		#endif
	#endif
	#if defined( __SANITIZE_ADDRESS__ )
		#define SW_HAS_LSAN_LEAK_CHECK 1
	#endif
#endif

#if defined( SW_HAS_LSAN_LEAK_CHECK )
extern "C" void __lsan_do_leak_check( void );
#endif

namespace sw
{
	void enableMemoryLeakChecks()
	{
#if defined( SW_HAS_CRT_LEAK_CHECK )
		int flags = _CrtSetDbgFlag( _CRTDBG_REPORT_FLAG );
		// Track CRT heap; dump on demand via reportMemoryLeaks() after teardown.
		flags |= _CRTDBG_ALLOC_MEM_DF;
		flags &= ~_CRTDBG_LEAK_CHECK_DF; // avoid a second dump at exit after report()
		_CrtSetDbgFlag( flags );

		_CrtSetReportMode( _CRT_WARN, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG );
		_CrtSetReportFile( _CRT_WARN, _CRTDBG_FILE_STDERR );
		_CrtSetReportMode( _CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG );
		_CrtSetReportFile( _CRT_ERROR, _CRTDBG_FILE_STDERR );
#elif defined( SW_HAS_LSAN_LEAK_CHECK )
		// ASan/LSan is already active via the toolchain; report() triggers an explicit check.
		(void)0;
#else
		(void)0;
#endif
	}

	int32 reportMemoryLeaks( const char* phaseTag )
	{
		const char* phase = ( phaseTag != nullptr && phaseTag[0] != '\0' ) ? phaseTag : "shutdown";

#if defined( SW_HAS_CRT_LEAK_CHECK )
		std::fprintf( stderr, "[MemoryLeak] %s — CRT _CrtDumpMemoryLeaks()\n", phase );
		return static_cast<int32>( _CrtDumpMemoryLeaks() );

#elif defined( SW_HAS_LSAN_LEAK_CHECK )
		std::fprintf( stderr, "[MemoryLeak] %s — __lsan_do_leak_check()\n", phase );
		__lsan_do_leak_check();
		return 0;

#else
	#if defined( SW_DEBUG ) && !defined( SW_SHIPPING )
		std::fprintf( stderr,
					  "[MemoryLeak] %s — no in-process checker.\n"
					  "  Windows Debug CRT: rebuild Debug.\n"
					  "  Linux: cmake -DSW_ENABLE_SANITIZER=ON  OR  valgrind --leak-check=full ./App\n",
					  phase );
	#else
		(void)phase;
	#endif
		return 0;
#endif
	}
} // namespace sw
