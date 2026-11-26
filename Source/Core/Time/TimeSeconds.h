/**
 * @file TimeSeconds.h
 * @brief 시간 길이(초)를 어느 시계에서 왔는지로 나눈 형식 셋입니다 — 게임 시간(`GameSeconds`, 정지 · 배율이 곱해진다), 실제 시간(`RealSeconds`), 고정 스텝 하나(`StepSeconds`).
 * @details 셋 다 float32 하나 크기의 값이고, 서로 · float 와 암시적으로 바뀌지 않는다. 결함 이력(scratchpad unify/history.md 모양 1 — 커밋 13)의 꼴은 모두
 *          "이 자리에 어느 시계의 시간을 넣는가" 를 부르는 쪽이 고르다 틀린 것이다(입력 장치에 게임 시간을, 자막에 실제 시간을, 서버 틱 대신 프레임 시간을).
 *          받는 자리가 형식을 정하면 틀린 시계는 컴파일되지 않는다. 언리얼은 `FApp::GetDeltaTime` 과 `FPlatformTime::Seconds` · 유니티는 `Time.deltaTime` 과
 *          `Time.unscaledDeltaTime` · `fixedDeltaTime` 을 이름으로만 가른다 — 여기서는 형식으로 가른다(`std::chrono::duration` 의 시계 꼬리표와 같은 생각).
 *          길이를 풀어 수식에 쓸 때는 `count()` 를 눈에 보이게 적는다. 같은 형식끼리만 더하고 빼고 견주며, 배율(float)을 곱할 수 있다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include <type_traits>

namespace sw
{
    struct GameClockTag;
    struct RealClockTag;
    struct StepClockTag;

    /** @brief 시계 꼬리표가 붙은 초 길이입니다. 직접 쓰지 않고 아래 별칭을 씁니다. */
    template <typename TClockTag>
    class TimeSeconds
    {
    public:
        constexpr TimeSeconds() noexcept = default;
        constexpr explicit TimeSeconds( float32 seconds ) noexcept
            : _seconds{ seconds }
        {
        }
        /**
         * @brief 고정 스텝 하나는 게임 시간의 한 토막이라 게임 시간으로 넓어집니다(한 방향 — 유니티 `Time.fixedDeltaTime` 도 배율이 곱해진 게임 시간이다).
         * @details 그래서 쿨다운 · 체력 재생처럼 프레임과 걸음 어디서든 흘리는 값은 `GameSeconds` 하나만 받는다. 거꾸로(프레임 시간을 한 걸음 자리에)는 막는다 —
         *          걸음 함수가 프레임 시간을 받으면 걸음이 없는 프레임 · 여러 걸음 프레임이 어긋난다(결함 511b3359 · 41653ecd).
         */
        template <typename TFromTag, typename TToTag = TClockTag, typename = std::enable_if_t<std::is_same_v<TToTag, GameClockTag> && std::is_same_v<TFromTag, StepClockTag>>>
        constexpr TimeSeconds( TimeSeconds<TFromTag> step ) noexcept // NOLINT(google-explicit-constructor, hicpp-explicit-conversions) 한 방향 넓힘이 이 형식의 뜻이다
            : _seconds{ step.count() }
        {
        }

        /** @brief 초입니다 — 이 길이를 수식에 풀어 쓸 때 부른다. */
        [[nodiscard]] constexpr float32 count() const noexcept { return _seconds; }

        friend constexpr TimeSeconds operator+( TimeSeconds left, TimeSeconds right ) noexcept { return TimeSeconds{ left._seconds + right._seconds }; }
        friend constexpr TimeSeconds operator-( TimeSeconds left, TimeSeconds right ) noexcept { return TimeSeconds{ left._seconds - right._seconds }; }
        friend constexpr TimeSeconds operator*( TimeSeconds time, float32 scale ) noexcept { return TimeSeconds{ time._seconds * scale }; }
        friend constexpr TimeSeconds operator*( float32 scale, TimeSeconds time ) noexcept { return TimeSeconds{ time._seconds * scale }; }
        /** @brief 같은 시계의 두 길이의 비율입니다. */
        friend constexpr float32 operator/( TimeSeconds left, TimeSeconds right ) noexcept { return left._seconds / right._seconds; }
        constexpr TimeSeconds&   operator+=( TimeSeconds other ) noexcept
        {
            _seconds += other._seconds;
            return *this;
        }
        constexpr TimeSeconds& operator-=( TimeSeconds other ) noexcept
        {
            _seconds -= other._seconds;
            return *this;
        }
        friend constexpr bool operator==( TimeSeconds left, TimeSeconds right ) noexcept { return left._seconds == right._seconds; }
        friend constexpr bool operator!=( TimeSeconds left, TimeSeconds right ) noexcept { return left._seconds != right._seconds; }
        friend constexpr bool operator<( TimeSeconds left, TimeSeconds right ) noexcept { return left._seconds < right._seconds; }
        friend constexpr bool operator<=( TimeSeconds left, TimeSeconds right ) noexcept { return left._seconds <= right._seconds; }
        friend constexpr bool operator>( TimeSeconds left, TimeSeconds right ) noexcept { return left._seconds > right._seconds; }
        friend constexpr bool operator>=( TimeSeconds left, TimeSeconds right ) noexcept { return left._seconds >= right._seconds; }

    private:
        float32 _seconds{ 0.0f };
    };

    /** @brief 게임 시간 — 정지면 0, 배율(슬로 모션 · 빨리 감기)이 곱해진 프레임 시간입니다(유니티 `Time.deltaTime`). 게임 규칙 · 씬 틱이 받는다. */
    using GameSeconds = TimeSeconds<GameClockTag>;
    /** @brief 실제 시간 — 정지 · 배율과 무관한 경과입니다(유니티 `Time.unscaledDeltaTime`). 입력 장치 · 정지 메뉴 · 오디오 장치 · 편집기가 받는다. */
    using RealSeconds = TimeSeconds<RealClockTag>;
    /** @brief 고정 스텝 하나의 길이입니다(유니티 `Time.fixedDeltaTime`). 물리 · 결정적 시뮬레이션의 한 걸음이 받는다. */
    using StepSeconds = TimeSeconds<StepClockTag>;

    static_assert( sizeof( GameSeconds ) == sizeof( float32 ) && sizeof( RealSeconds ) == sizeof( float32 ) && sizeof( StepSeconds ) == sizeof( float32 ), "a time length is one float" );
} // namespace sw
