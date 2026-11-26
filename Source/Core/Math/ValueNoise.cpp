#include "pch.h"

#include "Core/Math/ValueNoise.h"

#include "Core/Hash/HashUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    namespace
    {
        struct ValueNoiseInternal
        {
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
            /** @brief 1D 값 노이즈의 입력 한계입니다. int32 로 바꿀 때 넘치지 않게 자른다. */
            static constexpr float32 kValue1DInputLimit = 1.0e6f;
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
    } // namespace

    float32 ValueNoise::hashLattice( int32 x, int32 z, uint32 seed )
    {
        return HashUtil::toUnitFloat( HashUtil::hashCoord( x, z, seed ) );
    }

    float32 ValueNoise::sampleValue( float32 x, float32 z, uint32 seed )
    {
        const float32 floorX  = MathUtil::floor( x );
        const float32 floorZ  = MathUtil::floor( z );
        const int32   cellX   = static_cast<int32>( floorX );
        const int32   cellZ   = static_cast<int32>( floorZ );
        const float32 tx      = MathUtil::smoothstep( 0.0f, 1.0f, x - floorX );
        const float32 tz      = MathUtil::smoothstep( 0.0f, 1.0f, z - floorZ );
        const float32 v00     = hashLattice( cellX, cellZ, seed );
        const float32 v10     = hashLattice( cellX + 1, cellZ, seed );
        const float32 v01     = hashLattice( cellX, cellZ + 1, seed );
        const float32 v11     = hashLattice( cellX + 1, cellZ + 1, seed );
        const float32 rowNear = v00 + ( v10 - v00 ) * tx;
        const float32 rowFar  = v01 + ( v11 - v01 ) * tx;
        return rowNear + ( rowFar - rowNear ) * tz;
    }

    float32 ValueNoise::sampleFractal( float32 x, float32 z, uint32 seed, int32 octaveCount )
    {
        float32 sum       = 0.0f;
        float32 weight    = 0.0f;
        float32 amplitude = 1.0f;
        float32 frequency = 1.0f;
        for ( int32 octave = 0; octave < MathUtil::max( 1, octaveCount ); ++octave )
        {
            sum += amplitude * sampleValue( x * frequency, z * frequency, seed + static_cast<uint32>( octave ) * 7919u );
            weight += amplitude;
            amplitude *= 0.5f;
            frequency *= 2.0f;
        }
        return sum / weight;
    }

    float32 ValueNoise::sampleValue1D( float32 x, uint32 seed )
    {
        const float32 safeX  = MathUtil::clamp( x, -ValueNoiseInternal::kValue1DInputLimit, ValueNoiseInternal::kValue1DInputLimit );
        const float32 cell   = MathUtil::floor( safeX );
        const float32 ratio  = safeX - cell;
        const int32   index  = static_cast<int32>( cell );
        const float32 valueA = ValueNoiseInternal::hashToUnit( seed * HashUtil::kGoldenRatio32 + static_cast<uint32>( index ) );
        const float32 valueB = ValueNoiseInternal::hashToUnit( seed * HashUtil::kGoldenRatio32 + static_cast<uint32>( index + 1 ) );
        const float32 weight = MathUtil::smoothstep( 0.0f, 1.0f, ratio );
        return ( valueA + ( valueB - valueA ) * weight ) * 2.0f - 1.0f;
    }

    float32 ValueNoise::sampleValue3D( const float3& point, uint32 seed )
    {
        using Internal       = ValueNoiseInternal;
        const float32 floorX = MathUtil::floor( point._x );
        const float32 floorY = MathUtil::floor( point._y );
        const float32 floorZ = MathUtil::floor( point._z );
        const int32   x      = static_cast<int32>( floorX );
        const int32   y      = static_cast<int32>( floorY );
        const int32   z      = static_cast<int32>( floorZ );
        const float32 tx     = MathUtil::smoothstep( 0.0f, 1.0f, point._x - floorX );
        const float32 ty     = MathUtil::smoothstep( 0.0f, 1.0f, point._y - floorY );
        const float32 tz     = MathUtil::smoothstep( 0.0f, 1.0f, point._z - floorZ );

        const float32 value00 = Internal::lerp( Internal::getLatticeValue3D( x, y, z, seed ), Internal::getLatticeValue3D( x + 1, y, z, seed ), tx );
        const float32 value10 = Internal::lerp( Internal::getLatticeValue3D( x, y + 1, z, seed ), Internal::getLatticeValue3D( x + 1, y + 1, z, seed ), tx );
        const float32 value01 = Internal::lerp( Internal::getLatticeValue3D( x, y, z + 1, seed ), Internal::getLatticeValue3D( x + 1, y, z + 1, seed ), tx );
        const float32 value11 = Internal::lerp( Internal::getLatticeValue3D( x, y + 1, z + 1, seed ), Internal::getLatticeValue3D( x + 1, y + 1, z + 1, seed ), tx );
        return Internal::lerp( Internal::lerp( value00, value10, ty ), Internal::lerp( value01, value11, ty ), tz );
    }

    float32 ValueNoise::samplePerlin1D( float32 x, uint32 seed )
    {
        const float32 floorValue = MathUtil::floor( x );
        const int32   lattice    = static_cast<int32>( floorValue );
        const float32 fraction   = x - floorValue;
        const float32 left       = ValueNoiseInternal::computeGradient( lattice, seed ) * fraction;
        const float32 right      = ValueNoiseInternal::computeGradient( lattice + 1, seed ) * ( fraction - 1.0f );
        // 1D 펄린의 값은 ±0.5 안이다. 두 배 해서 진폭이 곧 최대 흔들림이 되게 한다.
        return MathUtil::lerp( left, right, ValueNoiseInternal::computeFade( fraction ) ) * 2.0f;
    }
} // namespace sw
