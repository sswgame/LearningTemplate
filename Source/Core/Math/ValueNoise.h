/**
 * @file Core/Math/ValueNoise.h
 * @brief 시드 고정 노이즈(1D · 2D · 3D 값 노이즈와 1D 펄린)입니다. 지형 높이, 바이옴, 구름, 바람, 카메라 흔들림, 메시 변형처럼 부드럽게 변하는 절차 값을 만듭니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    /**
     * @struct ValueNoise
     * @brief 격자 꼭짓점에 정수 해시로 값을 두고 smoothstep 으로 보간합니다. 같은 시드면 같은 빌드의 어느 기계에서나 같은 값입니다.
     * @details 장르를 가리지 않는 기반이다(복셀 지형, 농장의 들판 높낮이, 공원 지형, 2D 동굴). 언리얼 `FMath::PerlinNoise1D/2D/3D` 처럼 Core 에 둔다.
     *          차원마다 격자 해시가 다르다.
     *          2D 는 축약을 끈 Math TU(`sw_mark_deterministic_sources`)의 줄 밖 함수라 구성 · 플랫폼을 넘어 같은 비트다(결정성 물리가 부른다).
     *          1D · 3D · 펄린은 헤더 인라인이라 부르는 TU 의 축약 설정을 따른다. 카메라 흔들림 · 메시 모디파이어의 옛 식(GameFramework TU 에서 Release 는
     *          `a * b + c` 가 FMA 로 합쳐진다)과 같은 비트를 내려고 일부러 그렇게 둔다. 결정성 TU 에서 부르면 옛 결과와 끝 비트가 다를 수 있다. 식을 바꾸지 않는다.
     */
    struct SW_API ValueNoise
    {
        /** @brief 정수 격자점의 값(0..1)입니다. `HashUtil::hashCoord` 를 씁니다. */
        static float32 hashLattice( int32 x, int32 z, uint32 seed );
        /** @brief 한 옥타브(0..1)입니다. */
        static float32 sampleValue( float32 x, float32 z, uint32 seed );
        /** @brief 옥타브를 겹친 값(0..1)입니다. 옥타브마다 주파수 ×2 · 진폭 ×0.5. */
        static float32 sampleFractal( float32 x, float32 z, uint32 seed, int32 octaveCount );

        /** @brief 1D 값 노이즈(-1..1)입니다. 정수 칸 사이를 smoothstep 으로 보간합니다. x 는 ±1e6 으로 자릅니다. */
        [[nodiscard]] static float32 sampleValue1D( float32 x, uint32 seed )
        {
            const float32 safeX  = MathUtil::clamp( x, -kValue1DInputLimit, kValue1DInputLimit ); // int32 로 바꿀 때 넘치지 않게
            const float32 cell   = MathUtil::floor( safeX );
            const float32 ratio  = safeX - cell;
            const int32   index  = static_cast<int32>( cell );
            const float32 valueA = hashToUnit( seed * HashUtil::kGoldenRatio32 + static_cast<uint32>( index ) );
            const float32 valueB = hashToUnit( seed * HashUtil::kGoldenRatio32 + static_cast<uint32>( index + 1 ) );
            const float32 weight = ratio * ratio * ( kSmoothstepA - kSmoothstepB * ratio );
            return ( valueA + ( valueB - valueA ) * weight ) * 2.0f - 1.0f;
        }

        /** @brief 3D 값 노이즈(-1..1)입니다. 격자점 해시(murmur3 마무리 섞기)를 smoothstep 삼선형 보간합니다. */
        [[nodiscard]] static float32 sampleValue3D( const float3& point, uint32 seed )
        {
            const float32 floorX = MathUtil::floor( point._x );
            const float32 floorY = MathUtil::floor( point._y );
            const float32 floorZ = MathUtil::floor( point._z );
            const int32   x      = static_cast<int32>( floorX );
            const int32   y      = static_cast<int32>( floorY );
            const int32   z      = static_cast<int32>( floorZ );
            const float32 tx     = smooth( point._x - floorX );
            const float32 ty     = smooth( point._y - floorY );
            const float32 tz     = smooth( point._z - floorZ );

            const float32 value00 = lerp( getLatticeValue3D( x, y, z, seed ), getLatticeValue3D( x + 1, y, z, seed ), tx );
            const float32 value10 = lerp( getLatticeValue3D( x, y + 1, z, seed ), getLatticeValue3D( x + 1, y + 1, z, seed ), tx );
            const float32 value01 = lerp( getLatticeValue3D( x, y, z + 1, seed ), getLatticeValue3D( x + 1, y, z + 1, seed ), tx );
            const float32 value11 = lerp( getLatticeValue3D( x, y + 1, z + 1, seed ), getLatticeValue3D( x + 1, y + 1, z + 1, seed ), tx );
            return lerp( lerp( value00, value10, ty ), lerp( value01, value11, ty ), tz );
        }

        /** @brief 1D 펄린(기울기) 노이즈(-1..1)입니다. 정수 격자점에서 0 이고, 페이드는 6t⁵ − 15t⁴ + 10t³ 입니다. */
        [[nodiscard]] static float32 samplePerlin1D( float32 x, uint32 seed )
        {
            const float32 floorValue = MathUtil::floor( x );
            const int32   lattice    = static_cast<int32>( floorValue );
            const float32 fraction   = x - floorValue;
            const float32 left       = computeGradient( lattice, seed ) * fraction;
            const float32 right      = computeGradient( lattice + 1, seed ) * ( fraction - 1.0f );
            // 1D 펄린의 값은 ±0.5 안이다. 두 배 해서 진폭이 곧 최대 흔들림이 되게 한다.
            return MathUtil::lerp( left, right, computeFade( fraction ) ) * 2.0f;
        }

    private:
        /** @brief 3D 격자 해시의 시드 · 축 곱수입니다. */
        static constexpr uint32 kLatticeSeedMultiplier = 0x9E3779B1u;
        static constexpr uint32 kLatticeMultiplierX    = 0x85EBCA6Bu;
        static constexpr uint32 kLatticeMultiplierY    = 0xC2B2AE35u;
        static constexpr uint32 kLatticeMultiplierZ    = 0x27D4EB2Fu;
        /** @brief murmur3 마무리 섞기(fmix32)의 곱수입니다. */
        static constexpr uint32 kFinalMixMultiplier0 = 0x85EBCA6Bu;
        static constexpr uint32 kFinalMixMultiplier1 = 0xC2B2AE35u;
        /** @brief 1D 펄린 기울기 해시의 격자 · 시드 곱수입니다. */
        static constexpr uint32 kGradientLatticeMultiplier = 0x27D4EB2Du;
        static constexpr uint32 kGradientSeedMultiplier    = 0x165667B1u;
        /** @brief 해시 아래 16 비트(0..65535)를 -1..1 로 옮기는 반폭입니다. */
        static constexpr float32 kLatticeHalfRange = 32767.5f;
        /** @brief 해시 아래 24 비트를 [0, 1) 로 옮기는 마스크와 나눔수(2²⁴)입니다. */
        static constexpr uint32  kUnitMantissaMask  = 0x00ffffffu;
        static constexpr float32 kUnitMantissaScale = 16777216.0f;
        /** @brief 1D 값 노이즈의 입력 한계입니다. */
        static constexpr float32 kValue1DInputLimit = 1.0e6f;
        /** @brief smoothstep t²(3 − 2t) 의 계수입니다. `MathUtil::smoothstep` 으로 바꾸지 않는다(옛 식과 축약 모양이 같아야 한다). */
        static constexpr float32 kSmoothstepA = 3.0f;
        static constexpr float32 kSmoothstepB = 2.0f;
        /** @brief 펄린 개선판 페이드 6t⁵ − 15t⁴ + 10t³ 의 계수입니다. */
        static constexpr float32 kFadeCoefficient5 = 6.0f;
        static constexpr float32 kFadeCoefficient4 = 15.0f;
        static constexpr float32 kFadeCoefficient3 = 10.0f;

        /** @brief 3D 격자점 (x, y, z) 와 시드를 32 비트로 섞습니다(murmur3 마무리 섞기). */
        static uint32 hashLattice3D( int32 x, int32 y, int32 z, uint32 seed )
        {
            uint32 hash = seed * kLatticeSeedMultiplier;
            hash ^= static_cast<uint32>( x ) * kLatticeMultiplierX;
            hash = ( hash << 13 ) | ( hash >> 19 );
            hash ^= static_cast<uint32>( y ) * kLatticeMultiplierY;
            hash = ( hash << 13 ) | ( hash >> 19 );
            hash ^= static_cast<uint32>( z ) * kLatticeMultiplierZ;
            hash ^= hash >> 16;
            hash *= kFinalMixMultiplier0;
            hash ^= hash >> 13;
            hash *= kFinalMixMultiplier1;
            hash ^= hash >> 16;
            return hash;
        }

        static float32 getLatticeValue3D( int32 x, int32 y, int32 z, uint32 seed )
        {
            return static_cast<float32>( hashLattice3D( x, y, z, seed ) & 0xFFFFu ) / kLatticeHalfRange - 1.0f;
        }

        static float32 smooth( float32 t ) { return t * t * ( kSmoothstepA - kSmoothstepB * t ); }

        /** @brief from + (to − from)t 입니다. `MathUtil::lerp`(x(1−a) + ya)와 비트가 갈리므로 3D 값 노이즈는 이 식을 지킨다. */
        static float32 lerp( float32 from, float32 to, float32 t ) { return from + ( to - from ) * t; }

        /** @brief 정수 하나를 [0, 1) 로 섞습니다. `HashUtil::toUnitFloat` 는 [0, 1] 이라 1D 값 노이즈와 비트가 다르다. */
        static float32 hashToUnit( uint32 value ) { return static_cast<float32>( HashUtil::mix32( value ) & kUnitMantissaMask ) / kUnitMantissaScale; }

        /** @brief 1D 펄린 격자점의 기울기(-1..1)입니다. */
        static float32 computeGradient( int32 lattice, uint32 seed )
        {
            uint32 hash = static_cast<uint32>( lattice ) * kGradientLatticeMultiplier ^ seed * kGradientSeedMultiplier;
            hash ^= hash >> 15;
            hash *= kFinalMixMultiplier0;
            hash ^= hash >> 13;
            hash *= kFinalMixMultiplier1;
            hash ^= hash >> 16;
            return static_cast<float32>( hash & 0xFFFFu ) / kLatticeHalfRange - 1.0f;
        }

        static float32 computeFade( float32 t ) { return t * t * t * ( t * ( t * kFadeCoefficient5 - kFadeCoefficient4 ) + kFadeCoefficient3 ); }
    };
} // namespace sw
