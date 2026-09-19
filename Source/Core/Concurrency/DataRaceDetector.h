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
        constexpr RaceDetectContext() noexcept = default;
        /** @brief 카운터만 버리며 락은 없습니다. */
        ~RaceDetectContext() = default;

#if defined( SW_DEBUG )
        // 복사 및 이동 허용 (컨테이너 복사 시 컨텍스트 자체는 초기 상태 0으로 복사)
        /** @brief 카운터를 공유하지 않고 0으로 초기화된 컨텍스트로 복사합니다. */
        constexpr RaceDetectContext( const RaceDetectContext& ) noexcept
            : _state{ 0 } {}
        /** @brief 카운터를 공유하지 않고 0으로 리셋합니다. */
        RaceDetectContext& operator=( const RaceDetectContext& )
        {
            _state.store( 0 );
            return *this;
        }
        /** @brief 카운터를 가져오지 않고 0으로 초기화된 컨텍스트로 이동합니다. */
        constexpr RaceDetectContext( RaceDetectContext&& ) noexcept
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
        RaceDetectContext( const RaceDetectContext& )                = default;
        RaceDetectContext& operator=( const RaceDetectContext& )     = default;
        RaceDetectContext( RaceDetectContext&& ) noexcept            = default;
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
#if defined( SW_DEBUG )
        /** @brief 레이스 메시지와 콜스택을 Fatal 로 남깁니다. */
        void triggerDataRace( const utf8* pMessage );

        /** @brief 지금 들어와 있는 것이 **나 자신**인지 — 그렇다면 재진입이지 레이스가 아닙니다. */
        bool isOwnedByCurrentThread() const;

        // 하위 16비트: Reader Count (최대 65535)
        // 상위 16비트: Writer Count (최대 65535)
        atomic<uint32> _state{ 0 };

        /**
         * @brief 지금 이 컨텍스트에 **처음 들어온 스레드**의 id 해시. 아무도 없으면 0.
         *
         * @details **데이터 레이스는 정의상 두 스레드가 필요하다.** 그런데 이 검출기에는 스레드 개념이
         *          없어서, 한 스레드가 가드를 잡은 채 **같은 객체의 다른 가드 메서드를 부르기만 해도**
         *          레이스로 보고했다 — 예: `sw::set::operator=(initializer_list)` 는 쓰기 가드를 잡고
         *          `clear()`·`insert()` 를 부르는데 그 둘도 가드를 잡는다. 그래서 Debug 빌드에서
         *          **`sw::set` 에 초기화 리스트를 대입하기만 해도 없는 레이스가 떴다.**
         *
         *          같은 자리가 `map` 에도 있고, 쓰기 가드 안에서 const 메서드를 부르는 모든 경우가
         *          같은 모양이다 — 즉 "가드 메서드가 가드 메서드를 부르지 않게 조심한다" 로는 막히지
         *          않는 **구조적** 오탐이었다. 주인 스레드를 기억해 재진입을 구분한다.
         *
         * @note 이것은 **오탐만 줄인다.** 다른 스레드가 들어오면 id 가 다르므로 그대로 보고된다.
         */
        atomic<uint64> _ownerThreadId{ 0 };
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
#if defined( SW_DEBUG )
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
#if defined( SW_DEBUG )
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
#if defined( SW_DEBUG )
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
