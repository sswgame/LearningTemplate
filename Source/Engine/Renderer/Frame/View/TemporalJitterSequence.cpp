#include "pch.h"

#include "Engine/Renderer/Frame/View/TemporalJitterSequence.h"

namespace sw
{
    namespace
    {
        /** @brief 밑 @p base 의 Halton 수열 @p index 번째 값(0..1)입니다(radical inverse). */
        float32 computeHaltonValue( uint32 index, uint32 base )
        {
            float32 fraction = 1.0f;
            float32 result   = 0.0f;
            while ( index > 0u )
            {
                fraction /= static_cast<float32>( base );
                result += fraction * static_cast<float32>( index % base );
                index /= base;
            }
            return result;
        }
    } // namespace
} // namespace sw

namespace sw
{
    TemporalJitterSequence::TemporalJitterSequence()
        : _sampleIndex{ 0 }
    {
    }

    float2 TemporalJitterSequence::advance( uint32 width, uint32 height, uint32 phaseCount )
    {
        const uint32 cycle       = phaseCount > 0u ? phaseCount : kSampleCount;
        const float2 pixelOffset = getPixelOffset( _sampleIndex % cycle );
        _sampleIndex             = ( _sampleIndex + 1u ) % cycle;
        if ( width == 0u || height == 0u )
            return float2{};
        // 픽셀 하나는 NDC 로 2 / 크기다.
        return float2{ pixelOffset._x * 2.0f / static_cast<float32>( width ), pixelOffset._y * 2.0f / static_cast<float32>( height ) };
    }

    float2 TemporalJitterSequence::getPixelOffset( uint32 sampleIndex )
    {
        // 수열 번호는 1 부터다(0 번은 두 밑 모두 0 이라 한 자리에 몰린다 — 언리얼 TAA 와 같다).
        const uint32 haltonIndex = sampleIndex + 1u;
        return float2{ computeHaltonValue( haltonIndex, 2u ) - 0.5f, computeHaltonValue( haltonIndex, 3u ) - 0.5f };
    }

    uint32 TemporalJitterSequence::computePhaseCount( float32 renderScale )
    {
        if ( renderScale >= 1.0f || renderScale <= 0.0f )
            return kSampleCount;
        const float32 phases      = static_cast<float32>( kSampleCount ) / ( renderScale * renderScale );
        const uint32  wholePhases = static_cast<uint32>( phases );
        const uint32  count       = static_cast<float32>( wholePhases ) < phases ? wholePhases + 1u : wholePhases;
        return count < kSampleCount ? kSampleCount : ( count > kMaxSampleCount ? kMaxSampleCount : count );
    }

    float4x4 TemporalJitterSequence::applyToViewProjection( const float4x4& viewProj, const float2& ndcOffset )
    {
        float4x4 jittered = viewProj;
        jittered._11 += ndcOffset._x * viewProj._14;
        jittered._21 += ndcOffset._x * viewProj._24;
        jittered._31 += ndcOffset._x * viewProj._34;
        jittered._41 += ndcOffset._x * viewProj._44;
        jittered._12 += ndcOffset._y * viewProj._14;
        jittered._22 += ndcOffset._y * viewProj._24;
        jittered._32 += ndcOffset._y * viewProj._34;
        jittered._42 += ndcOffset._y * viewProj._44;
        return jittered;
    }
} // namespace sw
