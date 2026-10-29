/**
 * @file MeshModifierNoise.h
 * @brief 메시 모디파이어들이 함께 쓰는 결정적 값 노이즈(정수 해시 + 부드러운 보간)입니다.
 * @details 32 비트 정수 연산만 쓰므로 같은 입력이면 어느 기계에서나 같은 값이다.
 *          예전에 Noise · Crumple · VertNoise · ZStretchNoise · VertexAnim 의 익명 네임스페이스에 복사돼 있던 함수를
 *          비트 단위로 같은 수치 그대로 옮겼다 — 식을 바꾸면 저장된 씬의 모양이 바뀐다.
 *          3D 격자 노이즈(`valueNoise3D`, 범위 [-1,1])와 VertexAnim 이 쓰는 1 차원 시간 노이즈(`valueNoise1D`, 범위 [-1,1])는
 *          해시가 서로 달라 이름을 나눠 둔다.
 */
#pragma once
#include "Core/Hash/HashUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/VectorMath.h"

namespace sw::editor::MeshModifierNoise
{
    /** @brief 정수 격자점 (x, y, z) 와 시드를 32 비트 값으로 섞습니다(murmur3 마무리 섞기). */
    inline uint32 hashLattice( int32 x, int32 y, int32 z, uint32 seed )
    {
        uint32 hash = seed * 0x9E3779B1u;
        hash ^= static_cast<uint32>( x ) * 0x85EBCA6Bu;
        hash = ( hash << 13 ) | ( hash >> 19 );
        hash ^= static_cast<uint32>( y ) * 0xC2B2AE35u;
        hash = ( hash << 13 ) | ( hash >> 19 );
        hash ^= static_cast<uint32>( z ) * 0x27D4EB2Fu;
        hash ^= hash >> 16;
        hash *= 0x85EBCA6Bu;
        hash ^= hash >> 13;
        hash *= 0xC2B2AE35u;
        hash ^= hash >> 16;
        return hash;
    }

    /** @brief 격자점 값 [-1, 1] 입니다. */
    inline float32 getLatticeValue( int32 x, int32 y, int32 z, uint32 seed )
    {
        return static_cast<float32>( hashLattice( x, y, z, seed ) & 0xFFFFu ) / 32767.5f - 1.0f;
    }

    /** @brief smoothstep 곡선 t²(3 − 2t) 입니다. */
    inline float32 smooth( float32 t )
    {
        return t * t * ( 3.0f - 2.0f * t );
    }

    /** @brief 선형 보간입니다. */
    inline float32 lerp( float32 from, float32 to, float32 t )
    {
        return from + ( to - from ) * t;
    }

    /** @brief 3D 값 노이즈 [-1, 1] 입니다(격자점 해시 + 부드러운 삼선형 보간). */
    inline float32 valueNoise3D( const float3& point, uint32 seed )
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

        const float32 value00 = lerp( getLatticeValue( x, y, z, seed ), getLatticeValue( x + 1, y, z, seed ), tx );
        const float32 value10 = lerp( getLatticeValue( x, y + 1, z, seed ), getLatticeValue( x + 1, y + 1, z, seed ), tx );
        const float32 value01 = lerp( getLatticeValue( x, y, z + 1, seed ), getLatticeValue( x + 1, y, z + 1, seed ), tx );
        const float32 value11 = lerp( getLatticeValue( x, y + 1, z + 1, seed ), getLatticeValue( x + 1, y + 1, z + 1, seed ), tx );
        return lerp( lerp( value00, value10, ty ), lerp( value01, value11, ty ), tz );
    }

    /** @brief 정수 하나를 [0, 1) 로 섞어 냅니다(1 차원 노이즈 · 정점 위상용). */
    inline float32 hashToUnit( uint32 value )
    {
        return static_cast<float32>( HashUtil::mix32( value ) & 0x00ffffffu ) / 16777216.0f;
    }

    /** @brief 씨앗별 1 차원 값 노이즈 [-1, 1] 입니다(정수 칸 사이 smoothstep 보간). */
    inline float32 valueNoise1D( uint32 seed, float32 time )
    {
        const float32 safeTime = MathUtil::clamp( time, -1.0e6f, 1.0e6f ); // int32 로 바꿀 때 넘치지 않게
        const float32 cell     = MathUtil::floor( safeTime );
        const float32 ratio    = safeTime - cell;
        const int32   index    = static_cast<int32>( cell );
        const float32 valueA   = hashToUnit( seed * HashUtil::kGoldenRatio32 + static_cast<uint32>( index ) );
        const float32 valueB   = hashToUnit( seed * HashUtil::kGoldenRatio32 + static_cast<uint32>( index + 1 ) );
        const float32 weight   = ratio * ratio * ( 3.0f - 2.0f * ratio );
        return ( valueA + ( valueB - valueA ) * weight ) * 2.0f - 1.0f;
    }
} // namespace sw::editor::MeshModifierNoise
