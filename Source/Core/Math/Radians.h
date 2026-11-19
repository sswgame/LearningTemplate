/**
 * @file Radians.h
 * @brief 각 하나를 라디안으로 담는 값 형식 `Radians` 입니다 — 칸의 단위를 이름 끝말 · 주석이 아니라 형식이 말합니다.
 * @details 엔진이 쥐는 각은 라디안이다. 사람이 쓰는 데이터(XML)와 인스펙터는 도로 쓰고 보이므로, 예전에는 같은 `float32` 각이 자리마다 도이기도
 *          라디안이기도 했다(무기 반동 · 리그 관절 제한 · 짝 애니메이션 요가 읽은 뒤 손으로 `* kDegreeToRadian`). 이 형식은 그 변환을 입구 둘에만 둔다.
 *          - **암시 변환이 없다.** `float32` 에서 만들 때는 `fromRadians` · `fromDegrees` 중 하나를 이름으로 고르고, 꺼낼 때도 `radians()` · `degrees()` 로 고른다.
 *            그래서 도 값을 라디안 칸에 그냥 넣는 코드는 컴파일되지 않는다.
 *          - **바깥 표현은 도다.** 리플렉션의 성분 규칙(`PackedValueTraits<Radians>`)이 도를 내놓으므로 XML 글 · 인스펙터 드래그 · 콘솔 숫자 · `Min`/`Max` 경계는
 *            모두 도다. 메모리 · 이진 직렬화는 라디안 `float32` 비트 그대로다.
 *          - 상용 엔진 대조: Godot 는 라디안으로 담고 인스펙터만 도로 보인다(`PROPERTY_HINT_RANGE` 의 `radians_as_degrees`). 언리얼 `FRotator` 는 도로 담는다.
 *            이 엔진은 계산이 라디안이므로 Godot 쪽을 따르되, 손으로 쓰는 파일도 도로 적게 해 데이터 작성자가 한 단위만 본다.
 */
#pragma once
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MathUtil.h"

namespace sw
{
    /**
     * @struct Radians
     * @brief 라디안 각 하나입니다. 크기 · 정렬은 `float32` 와 같습니다(이진 직렬화가 비트를 그대로 옮긴다).
     */
    struct Radians final
    {
        /** @brief 1 도의 라디안입니다(float64 — 변환을 float64 로 해 데이터 글 왕복과 같은 값이 된다). */
        static constexpr float64 kRadiansPerDegree = MathUtil::kPi64 / 180.0;
        /** @brief 1 라디안의 도입니다. */
        static constexpr float64 kDegreesPerRadian = 180.0 / MathUtil::kPi64;

        /** @brief 0 라디안입니다. */
        constexpr Radians() noexcept = default;

        /** @brief 라디안 값 @p radians 를 담습니다. */
        [[nodiscard]] static constexpr Radians fromRadians( const float32 radians ) noexcept
        {
            Radians result{};
            result._radians = radians;
            return result;
        }

        /** @brief 도 값 @p degrees 를 라디안으로 바꿔 담습니다. 곱은 float64 로 해 데이터 글 왕복(`PackedValueTraits<Radians>`)과 같은 값이 된다. */
        [[nodiscard]] static constexpr Radians fromDegrees( const float32 degrees ) noexcept
        {
            return fromRadians( static_cast<float32>( static_cast<float64>( degrees ) * kRadiansPerDegree ) );
        }

        /** @brief 라디안 값입니다. */
        [[nodiscard]] constexpr float32 radians() const noexcept { return _radians; }

        /** @brief 도 값입니다. */
        [[nodiscard]] constexpr float32 degrees() const noexcept { return static_cast<float32>( static_cast<float64>( _radians ) * kDegreesPerRadian ); }

        [[nodiscard]] constexpr Radians        operator-() const noexcept { return fromRadians( -_radians ); }
        [[nodiscard]] constexpr Radians        operator+( const Radians other ) const noexcept { return fromRadians( _radians + other._radians ); }
        [[nodiscard]] constexpr Radians        operator-( const Radians other ) const noexcept { return fromRadians( _radians - other._radians ); }
        [[nodiscard]] constexpr Radians        operator*( const float32 scale ) const noexcept { return fromRadians( _radians * scale ); }
        [[nodiscard]] constexpr Radians        operator/( const float32 divisor ) const noexcept { return fromRadians( _radians / divisor ); }
        [[nodiscard]] friend constexpr Radians operator*( const float32 scale, const Radians angle ) noexcept { return angle * scale; }

        constexpr Radians& operator+=( const Radians other ) noexcept
        {
            _radians += other._radians;
            return *this;
        }
        constexpr Radians& operator-=( const Radians other ) noexcept
        {
            _radians -= other._radians;
            return *this;
        }

        [[nodiscard]] constexpr bool operator==( const Radians other ) const noexcept { return _radians == other._radians; }
        [[nodiscard]] constexpr bool operator!=( const Radians other ) const noexcept { return _radians != other._radians; }
        [[nodiscard]] constexpr bool operator<( const Radians other ) const noexcept { return _radians < other._radians; }
        [[nodiscard]] constexpr bool operator<=( const Radians other ) const noexcept { return _radians <= other._radians; }
        [[nodiscard]] constexpr bool operator>( const Radians other ) const noexcept { return _radians > other._radians; }
        [[nodiscard]] constexpr bool operator>=( const Radians other ) const noexcept { return _radians >= other._radians; }

    private:
        float32 _radians{ 0.0f };
    };

    static_assert( sizeof( Radians ) == sizeof( float32 ) && std::is_trivially_copyable_v<Radians>, "Radians 는 float32 하나다(이진 직렬화가 비트를 옮긴다)" );
} // namespace sw
