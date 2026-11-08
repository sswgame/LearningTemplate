#include "pch.h"

#include "Engine/Renderer/Lighting/LensFlareSpriteBuffer.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"

namespace sw
{
    namespace
    {
        struct LensFlareSpriteBufferInternal
        {
            /// @brief 카메라 바로 앞 · 뒤를 가르는 w 의 바닥입니다(0 으로 나누지 않게).
            static constexpr float32 kMinClipW = 1.0e-5f;
            /// @brief 화면비의 바닥입니다(높이가 너비보다 훨씬 큰 창에서 0 으로 나누지 않게).
            static constexpr float32 kMinAspect = 1.0e-3f;
        };
    } // namespace

    bool LensFlareSpriteBuffer::projectLight( const LensFlareRequest& request, const float4x4& viewProj, float3& outNdcDepth )
    {
        using Internal = LensFlareSpriteBufferInternal;
        // 방향광은 빛이 오는 쪽(나아가는 방향의 반대) 무한히 먼 점이다 — w 0 의 방향으로 투영한다. 깊이는 먼 평면(1)이다.
        const float4 source = request._bDirectional != SW_FALSE ? float4{ -request._direction._x, -request._direction._y, -request._direction._z, 0.0f }
                                                                : float4{ request._position._x, request._position._y, request._position._z, 1.0f };
        const float4 clip   = float4::transform( source, viewProj );
        if ( clip._w <= Internal::kMinClipW )
            return false;
        const float32 depth = request._bDirectional != SW_FALSE ? 1.0f : clip._z / clip._w;
        outNdcDepth         = float3{ clip._x / clip._w, clip._y / clip._w, depth };
        const bool bInside  = MathUtil::abs( outNdcDepth._x ) <= 1.0f && MathUtil::abs( outNdcDepth._y ) <= 1.0f && depth >= 0.0f && depth <= 1.0f;
        return bInside;
    }

    float32 LensFlareSpriteBuffer::computeEdgeFade( const float2& ndc, float32 edgeFade )
    {
        const float32 distanceToEdge = 1.0f - MathUtil::max( MathUtil::abs( ndc._x ), MathUtil::abs( ndc._y ) );
        if ( edgeFade <= 0.0f )
            return distanceToEdge >= 0.0f ? 1.0f : 0.0f;
        return MathUtil::saturate( distanceToEdge / edgeFade );
    }

    float2 LensFlareSpriteBuffer::computeElementCenter( const float2& lightNdc, float32 axisPosition )
    {
        return lightNdc * ( 1.0f - axisPosition );
    }

    float32 LensFlareSpriteBuffer::computeVisibility( const float32* pSampleDepth, uint32 sampleCount, float32 lightDepth )
    {
        if ( pSampleDepth == nullptr || sampleCount == 0 )
            return 0.0f;
        uint32 visibleCount = 0;
        for ( uint32 sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex )
        {
            visibleCount += pSampleDepth[sampleIndex] >= lightDepth - kDepthBias ? 1u : 0u;
        }
        return static_cast<float32>( visibleCount ) / static_cast<float32>( sampleCount );
    }

    void LensFlareSpriteBuffer::buildSprites( const vector<LensFlareRequest>& listRequest, const float4x4& viewProj, float32 aspect, vector<LensFlareSprite>& outListSprite )
    {
        outListSprite.clear();
        const float32 safeAspect = MathUtil::max( aspect, LensFlareSpriteBufferInternal::kMinAspect );
        for ( const LensFlareRequest& request : listRequest )
        {
            float3 ndcDepth{};
            if ( request._pAsset == nullptr || projectLight( request, viewProj, ndcDepth ) == false )
                continue;
            const LensFlareAsset& asset    = *request._pAsset;
            const float2          lightNdc = float2{ ndcDepth._x, ndcDepth._y };
            const float32         fade     = computeEdgeFade( lightNdc, asset._edgeFade );
            if ( fade <= 0.0f )
                continue;
            // NDC → UV(셰이더 Transform::computeWorldPositionFromDepth 의 역): u = x / 2 + 1/2, v = 1/2 - y / 2.
            const float4 lightUvKernel{ lightNdc._x * 0.5f + 0.5f, 0.5f - lightNdc._y * 0.5f, asset._occlusionRadius / safeAspect, asset._occlusionRadius };
            for ( const LensFlareElement& element : asset._listElement )
            {
                const float2    center = computeElementCenter( lightNdc, element._axisPosition );
                const float32   halfY  = element._size * 2.0f; // 화면 높이 비율 → NDC(높이 2)
                LensFlareSprite sprite;
                sprite._centerHalfSize = float4{ center._x, center._y, halfY / safeAspect, halfY };
                sprite._colorShape     = float4{ request._color._x * element._color._x, request._color._y * element._color._y, request._color._z * element._color._z,
                                             static_cast<float32>( element._shape ) };
                sprite._lightUvKernel  = lightUvKernel;
                sprite._depthFade      = float4{ ndcDepth._z, fade, 0.0f, 0.0f };
                outListSprite.push_back( sprite );
            }
        }
    }

    void LensFlareSpriteBuffer::prepare( IRHIDevice* pDevice, const vector<LensFlareRequest>& listRequest, const float4x4& viewProj, float32 aspect )
    {
        _spriteCount = 0;
        if ( pDevice == nullptr || listRequest.empty() )
            return;
        buildSprites( listRequest, viewProj, aspect, _listSpriteScratch );
        if ( _listSpriteScratch.empty() )
            return;
        constexpr RHIBufferUsage kReadUsage = RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource;
        const uint32             stride     = static_cast<uint32>( sizeof( LensFlareSprite ) );
        const uint32             count      = static_cast<uint32>( _listSpriteScratch.size() );
        if ( _buffer.ensureCapacity( pDevice, stride, count, kReadUsage, true, false, _listSpriteScratch.data() ) == false )
        {
            // 다른 모양으로 대신 그리지 않는다 — 이번 프레임의 플레어가 빠지고 까닭은 한 번만 남긴다.
            if ( _bReportedFailure == SW_FALSE )
                SW_LOG_ERROR_ID( "LensFlareSpriteBuffer.CreateFailed", "Failed to create the lens flare sprite buffer (%# sprites) - lens flares are not drawn", count );
            _bReportedFailure = SW_TRUE;
            return;
        }
        _buffer.upload( pDevice, _listSpriteScratch.data(), count * stride );
        _spriteCount = count;
    }

    void LensFlareSpriteBuffer::release( IRHIDevice* pDevice )
    {
        _buffer.release( pDevice );
        _spriteCount = 0;
    }
} // namespace sw
