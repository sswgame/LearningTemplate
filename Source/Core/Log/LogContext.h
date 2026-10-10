/**
 * @file LogContext.h
 * @brief 로그 문맥 — 지금 스레드가 처리하는 요청의 추적 id(128 비트)와 주체(계정 id)입니다. 문맥이 있는 스레드의 로그 줄에 `[trace=… acct=…]` 가 붙습니다.
 * @details 스레드 로컬이다(Java MDC · .NET `Activity.Current` 와 같은 자리). 비동기 경계(저장소 일 · 작업 큐)를 넘을 때는 넘기는 쪽이 `getCurrent()` 를
 *          복사해 들고 받는 쪽이 `ScopedLogContext` 로 다시 건다(`IServiceStoreWork` 가 그 예). 게임 · 렌더 · 에디터 스레드는 문맥이 없어 줄이 바이트 하나
 *          다르지 않다. 추적 id 는 W3C `traceparent` 의 trace-id 와 같은 크기다. 지표 라벨에는 추적 id · 계정 id 를 넣지 않는다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
    /** @brief 요청 추적 id 128 비트입니다(0 = 없음). */
    struct LogTraceID
    {
        uint64 _high{ 0 };
        uint64 _low{ 0 };

        constexpr bool isValid() const { return ( _high | _low ) != 0; }
        constexpr bool operator==( const LogTraceID& other ) const { return _high == other._high && _low == other._low; }
        constexpr bool operator!=( const LogTraceID& other ) const { return ( *this == other ) == false; }

        /** @brief 무작위로 만듭니다(0 이 나오지 않게). 보안 값이 아니다 — 줄을 가르는 표다. */
        SW_API static LogTraceID makeRandom();
    };
} // namespace sw

namespace sw
{
    /** @brief 로그 문맥 하나입니다. */
    struct LogContext
    {
        /** @brief `formatTag` 가 쓰는 최대 길이(널 포함) — `[trace=<32 자> acct=<16 자>] ` 는 63 자다. */
        static constexpr int32 kMaxTagSize = 64;

        LogTraceID _traceID{};
        uint64     _principalID{ 0 }; ///< 계정 id(0 = 없음)

        bool isEmpty() const { return _traceID.isValid() == false && _principalID == 0; }

        /**
         * @brief 로그 줄 꼬리표 `[trace=<16 진 32> acct=<16 진 16>] `(있는 칸만)를 @p pOutBuffer 에 널로 끝나게 씁니다. 빈 문맥이면 빈 글입니다.
         * @return 쓴 글자 수(널 제외). @p capacity 가 `kMaxTagSize` 보다 작으면 빈 글로 0 입니다.
         */
        SW_API int32 formatTag( utf8* pOutBuffer, int32 capacity ) const;

        /** @brief 이 스레드의 문맥입니다(없으면 빈 문맥). */
        SW_API static const LogContext& getCurrent();
    };
} // namespace sw

namespace sw
{
    /** @class ScopedLogContext @brief 사는 동안 이 스레드의 문맥을 바꾸고, 사라질 때 앞의 문맥으로 되돌립니다. */
    class SW_API ScopedLogContext
    {
    public:
        explicit ScopedLogContext( const LogContext& context );
        ~ScopedLogContext();

        ScopedLogContext( const ScopedLogContext& )            = delete;
        ScopedLogContext& operator=( const ScopedLogContext& ) = delete;

    private:
        LogContext _previous;
    };
} // namespace sw
