#include "pch.h"

#include "Core/Concurrency/DataRaceDetector.h"

#if SW_DEBUG
	#include "Core/Container/string.h"
	#include "Core/Log/Logger.h"
	#include "Core/Process/CallStackCapture.h"

namespace sw
{
	SW_LOG_CALLER( "DataRaceDetector" );

	namespace
	{
		static constexpr uint32 kWriterBitShift = 16;
		static constexpr uint32 kCountMask		= 0xFFFF;
		static constexpr uint32 kWriterUnit		= 1u << kWriterBitShift;
	} // namespace

	/**
	 * @brief 읽기 작업 시작 진입점
	 */
	void RaceDetectContext::enterRead()
	{
		// 하위 16비트(Reader 카운트)를 원자적으로 1 증가
		uint32 oldState = _state.fetch_add( 1, std::memory_order_acquire );
		uint32 writers	= oldState >> kWriterBitShift;

		// 이미 활성화된 Writer가 있다면 Read/Write 충돌
		if ( writers > 0 )
			triggerDataRace( "Concurrent read/write access detected! (Reader entered while Writer is active)" );
	}

	/**
	 * @brief 읽기 작업 완료 후 퇴출점
	 */
	void RaceDetectContext::exitRead()
	{
		_state.fetch_sub( 1, std::memory_order_release );
	}

	/**
	 * @brief 독점 쓰기 작업 시작 진입점
	 */
	void RaceDetectContext::enterWrite()
	{
		// 상위 16비트(Writer 카운트)를 원자적으로 1 증가
		uint32 oldState = _state.fetch_add( kWriterUnit, std::memory_order_acquire );
		uint32 readers	= oldState & kCountMask;
		uint32 writers	= oldState >> kWriterBitShift;

		// 다른 Writer가 이미 활성화되어 있으면 Write/Write 충돌
		if ( writers > 0 )
			triggerDataRace( "Concurrent write/write access detected! (Writer entered while another Writer is active)" );
		// Reader가 활성화되어 있으면 Read/Write 충돌
		else if ( readers > 0 )
			triggerDataRace( "Concurrent read/write access detected! (Writer entered while Reader is active)" );
	}

	/**
	 * @brief 독점 쓰기 작업 완료 후 퇴출점
	 */
	void RaceDetectContext::exitWrite()
	{
		_state.fetch_sub( kWriterUnit, std::memory_order_release );
	}

	/**
	 * @brief 레이스 컨디션 감지 시 콜스택을 캡처하고 디버거를 중단합니다.
	 */
	void RaceDetectContext::triggerDataRace( const utf8* pMessage )
	{
		uint32	  state	  = _state.load( std::memory_order_relaxed );
		uint32	  readers = state & kCountMask;
		uint32	  writers = ( state >> kWriterBitShift ) & kCountMask;
		CallStack callStack;
		CallStackCapture::capture( callStack, 1 );
		string stackTrace = CallStackCapture::symbolize( callStack );

		SW_LOG_ERROR( "%s (ctx: %p, readers: %u, writers: %u)", pMessage, this, readers, writers );

		size_t start{ 0 };
		while ( start < stackTrace.size() )
		{
			size_t end = stackTrace.find( '\n', start );
			if ( end == string::npos )
				end = stackTrace.size();
			string line = stackTrace.substr( start, end - start );
			if ( line.empty() == false )
			{
				SW_LOG_ERROR( "  %s", line.c_str() );
			}
			start = end + 1;
		}

		SW_DEBUG_BREAK();
	}
} // namespace sw

#endif
