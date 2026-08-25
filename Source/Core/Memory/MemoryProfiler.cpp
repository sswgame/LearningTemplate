#include "pch.h"

#include "Core/Memory/MemoryProfiler.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
#include "Core/String/formatString.h"

#if defined( SW_PLATFORM_WINDOWS ) && defined( SW_DEBUG ) && !defined( SW_SHIPPING )
	#define SW_HAS_CRT_LEAK_CHECK 1
	#include "Core/Container/vector.h"
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
extern "C" int32 __lsan_do_recoverable_leak_check( void );
#endif

namespace sw
{
	namespace
	{
#if defined( SW_HAS_CRT_LEAK_CHECK )
		_CrtMemState s_leakBaseline{};
		bool		 s_bHasLeakBaseline{ false };

		bool heapGrewVsBaseline( const _CrtMemState& now )
		{
			return now.lSizes[_NORMAL_BLOCK] > s_leakBaseline.lSizes[_NORMAL_BLOCK] ||
				   now.lCounts[_NORMAL_BLOCK] > s_leakBaseline.lCounts[_NORMAL_BLOCK] ||
				   now.lSizes[_CLIENT_BLOCK] > s_leakBaseline.lSizes[_CLIENT_BLOCK] ||
				   now.lCounts[_CLIENT_BLOCK] > s_leakBaseline.lCounts[_CLIENT_BLOCK];
		}
#endif

		template <typename... Args>
		void printLeakMessage( const utf8* format, Args&&... args )
		{
			utf8 buf[constant::kMaxBuffer1024]{};
			formatstring( buf, static_cast<uint32>( sizeof( buf ) ), format, std::forward<Args>( args )... );
			std::fputs( buf, stderr );
			std::fputc( '\n', stderr );
		}

		thread_local bool			 t_bIsInsideProfiler = false;
		std::atomic<MemoryProfiler*> s_activeProfiler{ nullptr };
		thread_local MemoryTag		 t_currentMemoryTag = MemoryTag::Unknown;

	} // namespace

	const utf8* MemoryProfiler::getMemoryTagName( MemoryTag tag )
	{
		switch ( tag )
		{
			case MemoryTag::Unknown:
				return "Unknown";
			case MemoryTag::Core:
				return "Core";
			case MemoryTag::Engine:
				return "Engine";
			case MemoryTag::Graphics:
				return "Graphics";
			case MemoryTag::Physics:
				return "Physics";
			case MemoryTag::Audio:
				return "Audio";
			case MemoryTag::Game:
				return "Game";
			case MemoryTag::Editor:
				return "Editor";
			case MemoryTag::MaxTags:
				return "MaxTags";
			default:
				return "Invalid";
		}
	}

	void MemoryProfiler::setCurrentMemoryTag( MemoryTag tag )
	{
		t_currentMemoryTag = tag;
	}

	MemoryTag MemoryProfiler::getCurrentMemoryTag()
	{
		return t_currentMemoryTag;
	}

	void MemoryProfiler::enableMemoryLeakChecks()
	{
#if defined( SW_HAS_CRT_LEAK_CHECK )
		int32 flags = _CrtSetDbgFlag( _CRTDBG_REPORT_FLAG );
		flags |= _CRTDBG_ALLOC_MEM_DF;
		flags &= ~_CRTDBG_LEAK_CHECK_DF;
		_CrtSetDbgFlag( flags );

		_CrtSetReportMode( _CRT_WARN, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG );
		_CrtSetReportFile( _CRT_WARN, _CRTDBG_FILE_STDERR );
		_CrtSetReportMode( _CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG );
		_CrtSetReportFile( _CRT_ERROR, _CRTDBG_FILE_STDERR );

		s_bHasLeakBaseline = false;
#endif
	}

	void MemoryProfiler::captureMemoryLeakBaseline()
	{
#if defined( SW_HAS_CRT_LEAK_CHECK )
		_CrtMemCheckpoint( &s_leakBaseline );
		s_bHasLeakBaseline = true;
		printLeakMessage( "[MemoryLeak] CRT baseline captured (post-init): %# normal bytes in %# blocks.",
						  static_cast<uint64>( s_leakBaseline.lSizes[_NORMAL_BLOCK] ),
						  static_cast<uint64>( s_leakBaseline.lCounts[_NORMAL_BLOCK] ) );
#endif
	}

	int32 MemoryProfiler::reportMemoryLeaks( const utf8* phaseTag )
	{
		const utf8* phase = ( phaseTag != nullptr && phaseTag[0] != '\0' ) ? phaseTag : "shutdown";

#if defined( SW_HAS_CRT_LEAK_CHECK )
		if ( s_bHasLeakBaseline )
		{
			_CrtMemState now{};
			_CrtMemCheckpoint( &now );

			if ( heapGrewVsBaseline( now ) )
			{
				_CrtMemState diff{};
				_CrtMemDifference( &diff, &s_leakBaseline, &now );

				printLeakMessage( "[MemoryLeak] %# — heap larger than post-init baseline (%# -> %# normal bytes).",
								  phase,
								  static_cast<uint64>( s_leakBaseline.lSizes[_NORMAL_BLOCK] ),
								  static_cast<uint64>( now.lSizes[_NORMAL_BLOCK] ) );
				_CrtMemDumpStatistics( &diff );
				_CrtDumpMemoryLeaks();
				return 1;
			}

			printLeakMessage( "[MemoryLeak] %# — no CRT leaks (normal %# -> %# bytes).",
							  phase,
							  static_cast<uint64>( s_leakBaseline.lSizes[_NORMAL_BLOCK] ),
							  static_cast<uint64>( now.lSizes[_NORMAL_BLOCK] ) );
			return 0;
		}

		printLeakMessage( "[MemoryLeak] %# — CRT _CrtDumpMemoryLeaks() (no baseline)", phase );
		return _CrtDumpMemoryLeaks();

#elif defined( SW_HAS_LSAN_LEAK_CHECK )
		printLeakMessage( "[MemoryLeak] %# — __lsan_do_recoverable_leak_check()", phase );
		return __lsan_do_recoverable_leak_check() != 0 ? 1 : 0;

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

	MemoryProfiler::MemoryProfiler()
		: _bInitialized{ false }
		, _bTrackingEnabled{ true }
		, _bDetailedTrackingEnabled{ false }
	{
	}

	MemoryProfiler::~MemoryProfiler()
	{
		shutdown();
	}

	void MemoryProfiler::initialize()
	{
		if ( _bInitialized.exchange( true ) )
			return;

		CallStackCapture::initialize();

		MemoryProfiler* pExpected{ nullptr };
		s_activeProfiler.compare_exchange_strong( pExpected, this, std::memory_order_acq_rel, std::memory_order_relaxed );
	}

	void MemoryProfiler::shutdown()
	{
		if ( _bInitialized.exchange( false ) == false )
			return;

		// 훅이 더 이상 이 인스턴스를 보지 않게 먼저 해제합니다.
		auto pExpected = this;
		s_activeProfiler.compare_exchange_strong( pExpected, nullptr, std::memory_order_acq_rel, std::memory_order_relaxed );

		_bTrackingEnabled.store( false, std::memory_order_relaxed );
		CallStackCapture::shutdown();
	}

	MemoryProfiler* MemoryProfiler::getActive()
	{
		return s_activeProfiler.load( std::memory_order_acquire );
	}

	void MemoryProfiler::setTrackingEnabled( bool bEnabled )
	{
		_bTrackingEnabled.store( bEnabled, std::memory_order_relaxed );
	}

	void MemoryProfiler::setDetailedTrackingEnabled( bool bEnabled )
	{
		_bDetailedTrackingEnabled.store( bEnabled, std::memory_order_relaxed );
	}

	uint64 MemoryProfiler::recordAllocation( void* ptr, size_t size, MemoryTag tag )
	{
		(void)ptr;
		if ( _bTrackingEnabled.load( std::memory_order_relaxed ) == false )
			return 0;

		if ( t_bIsInsideProfiler )
			return 0; // 방어 로직: 프로파일러 내부에서 해시 맵 할당 시 재귀 방지

		uint32 tagIdx = static_cast<uint32>( tag );
		if ( tagIdx >= static_cast<uint32>( MemoryTag::MaxTags ) )
			tagIdx = 0;

		_arrStats[tagIdx]._totalAllocatedBytes.fetch_add( size, std::memory_order_relaxed );
		_arrStats[tagIdx]._currentAllocatedBytes.fetch_add( size, std::memory_order_relaxed );
		_arrStats[tagIdx]._currentAllocationCount.fetch_add( 1, std::memory_order_relaxed );

		uint64 outHash{ 0 };

		if ( _bDetailedTrackingEnabled.load( std::memory_order_relaxed ) )
		{
			CallStack stack;
			// skipFrames: capture(0), recordAllocation(1), operator new(2) — 상위 2프레임을 건너뜁니다.
			CallStackCapture::capture( stack, 2 );
			outHash = stack.hash;

			t_bIsInsideProfiler = true;
			{
				std::scoped_lock<mutex> lock{ _stackMapMutex };
				auto&					info = _mapCallStackAllocInfo[outHash];
				if ( info._stack.frameCount == 0 )
					info._stack = stack;
				info._currentBytes += size;
				info._currentCount++;
			}
			t_bIsInsideProfiler = false;
		}

		return outHash;
	}

	void MemoryProfiler::recordFree( void* ptr, size_t size, MemoryTag tag, uint64 callStackHash )
	{
		(void)ptr;
		if ( _bTrackingEnabled.load( std::memory_order_relaxed ) == false )
			return;

		if ( t_bIsInsideProfiler )
			return; // 방어 로직: 프로파일러 내부에서 해시 맵 노드 해제 시 재귀 방지

		uint32 tagIdx = static_cast<uint32>( tag );
		if ( tagIdx >= static_cast<uint32>( MemoryTag::MaxTags ) )
			tagIdx = 0;

		_arrStats[tagIdx]._totalFreedBytes.fetch_add( size, std::memory_order_relaxed );
		_arrStats[tagIdx]._currentAllocatedBytes.fetch_sub( size, std::memory_order_relaxed );
		_arrStats[tagIdx]._currentAllocationCount.fetch_sub( 1, std::memory_order_relaxed );

		if ( _bDetailedTrackingEnabled.load( std::memory_order_relaxed ) && callStackHash != 0 )
		{
			t_bIsInsideProfiler = true;
			{
				std::scoped_lock<mutex> lock{ _stackMapMutex };
				auto					it = _mapCallStackAllocInfo.find( callStackHash );
				if ( it != _mapCallStackAllocInfo.end() )
				{
					if ( it->second._currentBytes >= size )
						it->second._currentBytes -= size;
					else
						it->second._currentBytes = 0;

					if ( it->second._currentCount > 0 )
						it->second._currentCount--;
				}
			}
			t_bIsInsideProfiler = false;
		}
	}

	const MemoryProfileStats& MemoryProfiler::getStats( MemoryTag tag ) const
	{
		uint32 tagIdx = static_cast<uint32>( tag );
		if ( tagIdx >= static_cast<uint32>( MemoryTag::MaxTags ) )
			tagIdx = 0;
		return _arrStats[tagIdx];
	}

	vector<CallStackAllocInfo> MemoryProfiler::getTopCallStacks() const
	{
		vector<CallStackAllocInfo> listResult;
		t_bIsInsideProfiler = true;
		{
			std::scoped_lock<mutex> lock{ _stackMapMutex };
			listResult.reserve( _mapCallStackAllocInfo.size() );
			for ( const auto& [hash, info] : _mapCallStackAllocInfo )
			{
				if ( info._currentBytes > 0 )
					listResult.push_back( info );
			}
		}
		t_bIsInsideProfiler = false;

		std::sort( listResult.begin(), listResult.end(), []( const CallStackAllocInfo& infoA, const CallStackAllocInfo& infoB )
		{ return infoA._currentBytes > infoB._currentBytes; } );

		return listResult;
	}
} // namespace sw
