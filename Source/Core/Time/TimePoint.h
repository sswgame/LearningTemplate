/**
 * @file TimePoint.h
 * @brief 시각의 종류를 형식으로 나눕니다 — 유닉스 시각(`UnixMilliseconds`, 1970-01-01 UTC 기준)과 단조 시각(`MonotonicMilliseconds`, 기준점 없음).
 * @details 둘 다 정수 밀리초를 싸지만 서로 바뀌지 않는다. 서명 토큰의 `iat` · `exp`, 인증서 유효 기간처럼 바깥 세계와 견주는 자리는 `UnixMilliseconds` 만 받아
 *          단조 시각(`IClock`)을 넣으면 컴파일이 안 된다. 정수에서 만들 때는 명시적 생성자만 있어 "이 정수가 유닉스 시각이다" 를 그 자리에 적게 한다.
 *          (구글 영수증 제공자가 단조 시각을 JWT `iat` 로 보내던 결함이 이 모양이다.)
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Types.h"

namespace sw
{
    /** @brief 유닉스 시각(UTC, 밀리초)입니다. 서버 루프의 `nowMs` 와 `WallClock` 이 이 시각입니다. */
    class UnixMilliseconds
    {
    public:
        constexpr explicit UnixMilliseconds( int64 count ) noexcept
            : _count{ count }
        {
        }

        constexpr int64 count() const noexcept { return _count; }
        /** @brief 초로 내림한 값입니다(JWT 의 `iat` · `exp`, 인증서 검증 시각). */
        constexpr int64 toSeconds() const noexcept { return _count / constant::kMillisecondsPerSecond; }

    private:
        int64 _count;
    };
} // namespace sw

namespace sw
{
    /** @brief 단조 시각(밀리초)입니다. 기준점이 없어 두 시각의 차이나 같은 시계로 만든 만료 시각과의 비교로만 씁니다. */
    class MonotonicMilliseconds
    {
    public:
        constexpr explicit MonotonicMilliseconds( int64 count ) noexcept
            : _count{ count }
        {
        }

        constexpr int64 count() const noexcept { return _count; }

    private:
        int64 _count;
    };
} // namespace sw
