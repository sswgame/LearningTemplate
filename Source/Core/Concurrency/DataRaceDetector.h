/**
 * @file DataRaceDetector.h
 * @brief 컨테이너 래퍼용 런타임 데이터 레이스 탐지 (디버그 전용).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Concurrency/atomic.h"

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) RaceDetectContext — 읽기/쓰기 카운트. 동시 write 또는 read+write 면 Fatal
	//    컨테이너 멤버(_raceCtx)는 Debug 전용. Release 에서는 공간을 차지하지 않음
	// ------------------------------------------------------------------------------
	/**
	 * @class RaceDetectContext
	 * @brief STL 래퍼 내부에 두어 동시 읽기/쓰기를 추적합니다. 레이스면 Fatal 로그입니다.
	 */
	class SW_API RaceDetectContext
	{
	public:
		/** @brief 카운터를 0으로 둡니다. */
		RaceDetectContext() = default;
		/** @brief 카운터만 버리며 락은 없습니다. */
		~RaceDetectContext() = default;

#if SW_DEBUG
		// 복사 및 이동 허용 (컨테이너 복사 시 컨텍스트 자체는 초기 상태 0으로 복사)
		/** @brief 카운터를 공유하지 않고 0으로 초기화된 컨텍스트로 복사합니다. */
		RaceDetectContext( const RaceDetectContext& )
			: _state{ 0 } {}
		/** @brief 카운터를 공유하지 않고 0으로 리셋합니다. */
		RaceDetectContext& operator=( const RaceDetectContext& )
		{
			_state.store( 0 );
			return *this;
		}
		/** @brief 카운터를 가져오지 않고 0으로 초기화된 컨텍스트로 이동합니다. */
		RaceDetectContext( RaceDetectContext&& ) noexcept
			: _state{ 0 } {}
		/** @brief 카운터를 가져오지 않고 0으로 리셋합니다. */
		RaceDetectContext& operator=( RaceDetectContext&& ) noexcept
		{
			_state.store( 0 );
			return *this;
		}

		/** @brief 읽기 카운트를 올리고, 쓰기가 있으면 레이스로 보고합니다. */
		void enterRead();
		/** @brief 읽기 카운트를 내립니다. */
		void exitRead();

		/** @brief 쓰기 카운트를 올리고, 다른 접근이 있으면 레이스로 보고합니다. */
		void enterWrite();
		/** @brief 쓰기 카운트를 내립니다. */
		void exitWrite();
#else
		RaceDetectContext( const RaceDetectContext& )				 = default;
		RaceDetectContext& operator=( const RaceDetectContext& )	 = default;
		RaceDetectContext( RaceDetectContext&& ) noexcept			 = default;
		RaceDetectContext& operator=( RaceDetectContext&& ) noexcept = default;

		// Release 빌드에서는 비용 없음
		/** @brief Release 에서는 읽기 추적을 하지 않습니다. */
		SW_INLINE void enterRead() {}
		/** @brief Release 에서는 읽기 추적을 하지 않습니다. */
		SW_INLINE void exitRead() {}

		/** @brief Release 에서는 쓰기 추적을 하지 않습니다. */
		SW_INLINE void enterWrite() {}
		/** @brief Release 에서는 쓰기 추적을 하지 않습니다. */
		SW_INLINE void exitWrite() {}
#endif

	private:
#if SW_DEBUG
		/** @brief 레이스 메시지와 콜스택을 Fatal 로 남깁니다. */
		void triggerDataRace( const utf8* message );

		// 하위 16비트: Reader Count (최대 65535)
		// 상위 16비트: Writer Count (최대 65535)
		atomic<uint32> _state{ 0 };
#endif
	};

	// ------------------------------------------------------------------------------
	// 2) ScopedRaceRead / ScopedRaceWrite — RAII 로 enter/exit
	// ------------------------------------------------------------------------------
	/**
	 * @struct ScopedRaceRead
	 * @brief 스코프 동안 읽기 접근을 기록합니다.
	 */
	struct ScopedRaceRead
	{
#if SW_DEBUG
		RaceDetectContext& _ctx;
		/** @brief ctx 에 읽기 진입을 알립니다. */
		SW_INLINE explicit ScopedRaceRead( RaceDetectContext& ctx )
			: _ctx{ ctx }
		{
			_ctx.enterRead();
		}

		/** @brief 읽기 진입을 끝냅니다. */
		SW_INLINE ~ScopedRaceRead() { _ctx.exitRead(); }
#else
		/** @brief Release 에서는 추적하지 않습니다. */
		SW_INLINE explicit ScopedRaceRead( const RaceDetectContext& ) {}
#endif
	};

	/**
	 * @struct ScopedRaceWrite
	 * @brief 스코프 동안 쓰기 접근을 기록합니다.
	 */
	struct ScopedRaceWrite
	{
#if SW_DEBUG
		RaceDetectContext& _ctx;
		/** @brief ctx 에 쓰기 진입을 알립니다. */
		SW_INLINE explicit ScopedRaceWrite( RaceDetectContext& ctx )
			: _ctx{ ctx }
		{
			_ctx.enterWrite();
		}

		/** @brief 쓰기 진입을 끝냅니다. */
		SW_INLINE ~ScopedRaceWrite() { _ctx.exitWrite(); }
#else
		/** @brief Release 에서는 추적하지 않습니다. */
		SW_INLINE explicit ScopedRaceWrite( const RaceDetectContext& ) {}
#endif
	};
} // namespace sw

// ------------------------------------------------------------------------------
// 3) 컨테이너 훅 — Release/C++17 에서 멤버·락이 바이너리에 남지 않게 한다
//    [[no_unique_address]] 는 C++20 전용이므로 쓰지 않는다
// ------------------------------------------------------------------------------
#if SW_DEBUG
	#define SW_RACE_CTX_MEMBER mutable ::sw::RaceDetectContext _raceCtx{};

	#define SW_SCOPED_RACE_READ() \
		const ::sw::ScopedRaceRead SW_CONCAT( _swRaceRead_, __LINE__ ) { _raceCtx }
	#define SW_SCOPED_RACE_WRITE() \
		const ::sw::ScopedRaceWrite SW_CONCAT( _swRaceWrite_, __LINE__ ) { _raceCtx }
	#define SW_SCOPED_RACE_READ_OTHER( otherObj ) \
		const ::sw::ScopedRaceRead SW_CONCAT( _swRaceReadOther_, __LINE__ ) { ( otherObj )._raceCtx }
	#define SW_SCOPED_RACE_WRITE_OTHER( otherObj ) \
		const ::sw::ScopedRaceWrite SW_CONCAT( _swRaceWriteOther_, __LINE__ ) { ( otherObj )._raceCtx }
#else
	#define SW_RACE_CTX_MEMBER
	#define SW_SCOPED_RACE_READ()
	#define SW_SCOPED_RACE_WRITE()
	#define SW_SCOPED_RACE_READ_OTHER( otherObj )
	#define SW_SCOPED_RACE_WRITE_OTHER( otherObj )
#endif
