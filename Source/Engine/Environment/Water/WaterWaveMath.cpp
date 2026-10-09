#include "pch.h"

#include "Engine/Environment/Water/WaterWaveMath.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"

namespace sw
{
    namespace
    {
        struct WaterWaveMathInternal
        {
            /** @brief 파도 하나의 항 — 방향 · 파수 · 진폭 · Q · 위상입니다. 셰이더 `swComputeGerstnerTerm` 과 같은 순서로 계산한다(부동소수 결과를 맞추려고). */
            struct WaveTerm
            {
                float2  _direction{};
                float32 _waveNumber{ 0.0f };
                float32 _amplitude{ 0.0f };
                float32 _sharpness{ 0.0f };
                float32 _phase{ 0.0f };
            };

            static bool makeTerm( const float4& wave, uint32 activeCount, const float2& origin, float32 time, float32 gravity, WaveTerm& outTerm )
            {
                if ( wave._z <= 0.0f || wave._y <= 0.0f || activeCount == 0 )
                    return false;
                outTerm._direction  = float2{ MathUtil::cos( wave._x ), MathUtil::sin( wave._x ) };
                outTerm._waveNumber = MathUtil::kTwoPi / wave._y;
                outTerm._amplitude  = wave._z;
                outTerm._sharpness  = wave._w / ( outTerm._waveNumber * wave._z * static_cast<float32>( activeCount ) );
                const float32 speed = MathUtil::sqrt( gravity * outTerm._waveNumber );
                outTerm._phase      = outTerm._waveNumber * ( outTerm._direction._x * origin._x + outTerm._direction._y * origin._y ) - speed * time;
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    uint32 WaterWaveMath::countActiveWaves( const float4 ( &arrWave )[shaderslot::kGerstnerWaveCount] )
    {
        uint32 count{ 0 };
        for ( const float4& wave : arrWave )
        {
            count += ( wave._z > 0.0f && wave._y > 0.0f ) ? 1u : 0u;
        }
        return count;
    }

    float3 WaterWaveMath::computeDisplacement( const float2& origin, float32 time, float32 gravity, const float4 ( &arrWave )[shaderslot::kGerstnerWaveCount] )
    {
        using Internal           = WaterWaveMathInternal;
        const uint32 activeCount = countActiveWaves( arrWave );
        float3       displacement{};
        for ( const float4& wave : arrWave )
        {
            Internal::WaveTerm term;
            if ( Internal::makeTerm( wave, activeCount, origin, time, gravity, term ) == false )
                continue;
            const float32 cosine = MathUtil::cos( term._phase );
            const float32 sine   = MathUtil::sin( term._phase );
            displacement._x += term._sharpness * term._amplitude * term._direction._x * cosine;
            displacement._y += term._amplitude * sine;
            displacement._z += term._sharpness * term._amplitude * term._direction._y * cosine;
        }
        return displacement;
    }

    float3 WaterWaveMath::computeNormal( const float2& origin, float32 time, float32 gravity, const float4 ( &arrWave )[shaderslot::kGerstnerWaveCount] )
    {
        // 옮겨 간 면의 두 접선(원점 x · z 로 편미분)의 외적이다 — GPU Gems 의 근사(옮겨 간 자리에서 위상을 다시 잰다)가 아니라 정확한 노멀이다.
        using Internal           = WaterWaveMathInternal;
        const uint32 activeCount = countActiveWaves( arrWave );
        float3       tangentX{ 1.0f, 0.0f, 0.0f };
        float3       tangentZ{ 0.0f, 0.0f, 1.0f };
        for ( const float4& wave : arrWave )
        {
            Internal::WaveTerm term;
            if ( Internal::makeTerm( wave, activeCount, origin, time, gravity, term ) == false )
                continue;
            const float32 cosine     = MathUtil::cos( term._phase );
            const float32 sine       = MathUtil::sin( term._phase );
            const float32 slope      = term._waveNumber * term._amplitude;
            const float32 crest      = term._sharpness * slope * sine;
            const float32 directionX = term._direction._x;
            const float32 directionZ = term._direction._y;
            tangentX._x -= crest * directionX * directionX;
            tangentX._y += slope * directionX * cosine;
            tangentX._z -= crest * directionX * directionZ;
            tangentZ._x -= crest * directionX * directionZ;
            tangentZ._y += slope * directionZ * cosine;
            tangentZ._z -= crest * directionZ * directionZ;
        }
        return tangentZ.cross( tangentX ).normalize();
    }

    float32 WaterWaveMath::computeSurfaceHeight( const float2& position, float32 time, float32 gravity, const float4 ( &arrWave )[shaderslot::kGerstnerWaveCount], uint32 iterationCount, float2* pOutOrigin )
    {
        // 원점 p 는 p + 변위(p).xz = 위치 를 풀어 찾는다. Q 가 고리를 막는 범위라 수평 변위의 기울기가 1 보다 작아 고정점 반복이 수렴한다.
        float2 origin = position;
        for ( uint32 iteration = 0; iteration < iterationCount; ++iteration )
        {
            const float3 displacement = computeDisplacement( origin, time, gravity, arrWave );
            origin                    = float2{ position._x - displacement._x, position._y - displacement._z };
        }
        if ( pOutOrigin != nullptr )
            *pOutOrigin = origin;
        return computeDisplacement( origin, time, gravity, arrWave )._y;
    }
} // namespace sw
