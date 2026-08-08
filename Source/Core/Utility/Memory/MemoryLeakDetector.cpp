/**
 * @file MemoryLeakDetector.cpp
 * @brief Thin wrappers around CRT / LSan (no global new/delete hooks).
 */
#include "MemoryLeakDetector.h"

#include "Core/Utility/String/formatString.h"

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
	namespace
	{
		template <typename... Args>
		void printLeakMessage( const utf8* format, Args&&... args )
		{
			utf8 buf[512]{};
			formatstring( buf, static_cast<uint32>( sizeof( buf ) ), format, std::forward<Args>( args )... );
			std::fputs( buf, stderr );
			std::fputc( '\n', stderr );
		}
	} // namespace

#if defined( SW_HAS_CRT_LEAK_CHECK )
	namespace
	{
		_CrtMemState _s_baseline{};
		bool		 _s_bHasBaseline = false;

		/**
		 * @brief Compare absolute checkpoint sizes (NOT _CrtMemDifference result).
		 * @details diff.lCounts/lSizes are size_t; a shrink wraps to a huge positive and
		 *          looks like growth if you only test `> 0`.
		 */
		bool heapGrewVsBaseline( const _CrtMemState& now )
		{
			return now.lSizes[_NORMAL_BLOCK] > _s_baseline.lSizes[_NORMAL_BLOCK] ||
				   now.lCounts[_NORMAL_BLOCK] > _s_baseline.lCounts[_NORMAL_BLOCK] ||
				   now.lSizes[_CLIENT_BLOCK] > _s_baseline.lSizes[_CLIENT_BLOCK] ||
				   now.lCounts[_CLIENT_BLOCK] > _s_baseline.lCounts[_CLIENT_BLOCK];
		}
	} // namespace
#endif

	void enableMemoryLeakChecks()
	{
#if defined( SW_HAS_CRT_LEAK_CHECK )
		int32 flags = _CrtSetDbgFlag( _CRTDBG_REPORT_FLAG );
		flags |= _CRTDBG_ALLOC_MEM_DF;
		flags &= ~_CRTDBG_LEAK_CHECK_DF; // avoid a second dump at exit after report()
		_CrtSetDbgFlag( flags );

		_CrtSetReportMode( _CRT_WARN, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG );
		_CrtSetReportFile( _CRT_WARN, _CRTDBG_FILE_STDERR );
		_CrtSetReportMode( _CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG );
		_CrtSetReportFile( _CRT_ERROR, _CRTDBG_FILE_STDERR );

		_s_bHasBaseline = false;
#elif defined( SW_HAS_LSAN_LEAK_CHECK )
		(void)0;
#else
		(void)0;
#endif
	}

	void captureMemoryLeakBaseline()
	{
#if defined( SW_HAS_CRT_LEAK_CHECK )
		_CrtMemCheckpoint( &_s_baseline );
		_s_bHasBaseline = true;
		printLeakMessage( "[MemoryLeak] CRT baseline captured (post-init): %# normal bytes in %# blocks.",
						  static_cast<uint64>( _s_baseline.lSizes[_NORMAL_BLOCK] ),
						  static_cast<uint64>( _s_baseline.lCounts[_NORMAL_BLOCK] ) );
#else
		(void)0;
#endif
	}

	int32 reportMemoryLeaks( const utf8* phaseTag )
	{
		const utf8* phase = ( phaseTag != nullptr && phaseTag[0] != '\0' ) ? phaseTag : "shutdown";

#if defined( SW_HAS_CRT_LEAK_CHECK )
		if ( _s_bHasBaseline )
		{
			_CrtMemState now{};
			_CrtMemCheckpoint( &now );

			if ( heapGrewVsBaseline( now ) )
			{
				_CrtMemState diff{};
				_CrtMemDifference( &diff, &_s_baseline, &now );

				printLeakMessage( "[MemoryLeak] %# — heap larger than post-init baseline (%# -> %# normal bytes).",
								  phase,
								  static_cast<uint64>( _s_baseline.lSizes[_NORMAL_BLOCK] ),
								  static_cast<uint64>( now.lSizes[_NORMAL_BLOCK] ) );
				_CrtMemDumpStatistics( &diff );
				_CrtDumpMemoryLeaks();
				return 1;
			}

			printLeakMessage( "[MemoryLeak] %# — no CRT leaks (normal %# -> %# bytes).",
							  phase,
							  static_cast<uint64>( _s_baseline.lSizes[_NORMAL_BLOCK] ),
							  static_cast<uint64>( now.lSizes[_NORMAL_BLOCK] ) );
			return 0;
		}

		printLeakMessage( "[MemoryLeak] %# — CRT _CrtDumpMemoryLeaks() (no baseline)", phase );
		return static_cast<int32>( _CrtDumpMemoryLeaks() );

#elif defined( SW_HAS_LSAN_LEAK_CHECK )
		printLeakMessage( "[MemoryLeak] %# — __lsan_do_leak_check()", phase );
		__lsan_do_leak_check();
		return 0;

#else
	#if defined( SW_DEBUG ) && !defined( SW_SHIPPING )
		printLeakMessage( "[MemoryLeak] %# — no in-process checker. Windows Debug CRT: rebuild Debug. "
						  "Linux: cmake -DSW_ENABLE_SANITIZER=ON OR valgrind --leak-check=full ./App",
						  phase );
	#else
		(void)phase;
	#endif
		return 0;
#endif
	}
} // namespace sw
