#include "pch.h"

#include "Engine/Object/Component/3D/DirectionalLightComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Shader/Binding/GPULight.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 이 TU 의 기본값 모음입니다. **익명 네임스페이스에 상수를 그냥 두면 안 됩니다.**
         *        유니티 빌드(CI-*)는 여러 .cpp 를 한 TU 로 합치고, 그러면 세 라이트 컴포넌트의
         *        `kDefaultColor` 가 같은 익명 네임스페이스에서 재정의됩니다(AGENTS.md 의 Internal 규칙).
         */
        struct DirectionalLightComponentInternal
        {
            /// @brief 기본 주광 값입니다.
            static constexpr float3  kDefaultColor{ 1.0f, 0.82f, 0.62f };
            static constexpr float32 kDefaultIntensity{ 1.35f };
            static constexpr float32 kDefaultAmbient{ 0.28f };
            static constexpr float32 kDefaultShadowExtent{ 2.0f / 0.9f };
            static constexpr float32 kDefaultShadowDistance{ 2.0f };
            /// @brief 로컬 기본 빛 방향입니다(위에서 비스듬히). 월드 회전이 이것을 돌립니다.
            static constexpr float3 kDefaultDirection{ -0.35f, -0.85f, -0.25f };
            /// @brief 깊이 바이어스(텍셀 수). 노멀 오프셋이 기울기를 맡으므로 깊이 쪽은 텍셀 하나면 된다.
            static constexpr float32 kShadowDepthBiasTexels{ 1.0f };
            /// @brief 노멀 오프셋(텍셀 수). 받는 점을 노멀 쪽으로 h 띄우면 빛 광선 위 깊이 여유가 h / sin(고도) 다 — 고도 20° 바닥에서 여드름 없이 지나는 값이다.
            static constexpr float32 kShadowNormalOffsetTexels{ 2.0f };
            /// @brief 그늘 세기 — 1 이면 그늘이 완전히 검다.
            static constexpr float32 kShadowStrength{ 0.45f };
            static constexpr float32 kDefaultShadowViewDistance{ 0.0f };
            /// @brief 맞춘 볼륨의 가로 · 세로를 이 길이(m)의 배수로 올린다 — 카메라가 돌거나 줌할 때 크기가 매 프레임 조금씩 바뀌어 텍셀 격자가 떨리는 것을 줄인다.
            static constexpr float32 kShadowFitQuantum{ 2.0f };
            /// @brief 맞춘 볼륨의 먼 쪽 깊이에 더하는 여유(m) — 띠 경계에 걸친 면이 원평면에 잘리지 않게.
            static constexpr float32 kShadowFitDepthMargin{ 1.0f };
            /// @brief 절두체 모서리 열둘. 꼭짓점 번호의 비트 0 = x, 1 = y, 2 = 먼 면.
            static constexpr uint32 kArrFrustumEdge[12][2] = {
                {0, 1},
                {2, 3},
                {0, 2},
                {1, 3},
                {4, 5},
                {6, 7},
                {4, 6},
                {5, 7},
                {0, 4},
                {1, 5},
                {2, 6},
                {3, 7}
            };
            /// @brief 띠로 자른 절두체의 꼭짓점 상한 — 꼭짓점 여덟 + 모서리 열둘 × 경계면 둘.
            static constexpr uint32 kMaxFitPoint = 8 + 12 * 2;
        };
    } // namespace

    float4 DirectionalShadowProjection::computeShaderParams() const
    {
        using Internal                  = DirectionalLightComponentInternal;
        const float32 depthRange        = MathUtil::max( _depthRange, MathUtil::kEpsilon );
        const float32 resolution        = static_cast<float32>( MathUtil::max( _resolution, 1u ) );
        const float32 depthBiasWorld    = _texelWorldSize * Internal::kShadowDepthBiasTexels;
        const float32 normalOffsetWorld = _texelWorldSize * Internal::kShadowNormalOffsetTexels;
        return float4{ depthBiasWorld / depthRange, Internal::kShadowStrength, normalOffsetWorld, 1.0f / resolution };
    }

    DirectionalLightComponent::DirectionalLightComponent()
        : LightComponent( shaderslot::kLightTypeDirectional, DirectionalLightComponentInternal::kDefaultColor,
                          DirectionalLightComponentInternal::kDefaultIntensity )
        , _ambient{ DirectionalLightComponentInternal::kDefaultAmbient }
        , _shadowExtent{ DirectionalLightComponentInternal::kDefaultShadowExtent }
        , _shadowDistance{ DirectionalLightComponentInternal::kDefaultShadowDistance }
        , _shadowViewDistance{ DirectionalLightComponentInternal::kDefaultShadowViewDistance }
        , _shadowReceiverMinHeight{ 0.0f }
        , _shadowReceiverMaxHeight{ 0.0f }
        , _bCastShadow{ SW_TRUE }
        , _reservedLight{ 0 }
    {
    }

    float3 DirectionalLightComponent::getLightDirection() const
    {
        return computeLightDirection( DirectionalLightComponentInternal::kDefaultDirection );
    }

    void DirectionalLightComponent::setAmbient( float32 ambient )
    {
        _ambient = MathUtil::max( ambient, 0.0f );
        onPropertyChanged( hashed_string( "_ambient" ) );
    }

    void DirectionalLightComponent::setShadowExtent( float32 extent )
    {
        _shadowExtent = MathUtil::max( extent, 0.01f );
        onPropertyChanged( hashed_string( "_shadowExtent" ) );
    }

    void DirectionalLightComponent::setShadowDistance( float32 distance )
    {
        _shadowDistance = MathUtil::max( distance, 0.01f );
        onPropertyChanged( hashed_string( "_shadowDistance" ) );
    }

    void DirectionalLightComponent::setShadowViewDistance( float32 distance )
    {
        _shadowViewDistance = MathUtil::max( distance, 0.0f );
        onPropertyChanged( hashed_string( "_shadowViewDistance" ) );
    }

    void DirectionalLightComponent::setShadowReceiverHeightRange( float32 minHeight, float32 maxHeight )
    {
        _shadowReceiverMinHeight = MathUtil::min( minHeight, maxHeight );
        _shadowReceiverMaxHeight = MathUtil::max( minHeight, maxHeight );
        onPropertyChanged( hashed_string( "_shadowReceiverMinHeight" ) );
        onPropertyChanged( hashed_string( "_shadowReceiverMaxHeight" ) );
    }

    void DirectionalLightComponent::setCastShadow( bool bCastShadow )
    {
        _bCastShadow = bCastShadow ? SW_TRUE : SW_FALSE;
        onPropertyChanged( hashed_string( "_bCastShadow" ) );
    }

    float4x4 DirectionalLightComponent::buildShadowViewProj() const
    {
        const float3 lightDir = getLightDirection();

        // 라이트를 빛이 오는 쪽에 두고 빛 방향으로 원점을 내려다본다. up 이 빛과 거의 나란하면
        // side 축이 사라지므로 다른 축으로 갈아탄다.
        const float3 up  = MathUtil::abs( lightDir._y ) > 0.99f ? float3::Forward : float3::Up;
        const float3 eye = lightDir * -_shadowDistance;

        // **깊이 범위는 눈을 기준으로 잡는다.** 눈이 원점에서 거리만큼 떨어져 원점을 보고 있으므로 씬의 뷰 z 는
        // 거리 언저리다. 주의: `(-거리, +거리)` 로 잡으면 `createOrthographic` 은 `z' = (z_view - near) / (far - near)` 라
        // 씬 전체가 z' ≈ 1(원평면)로 뭉개지고, 깊이 비교가 늘 "가려지지 않음" 이 되어 **그림자가 지지 않는다**.
        // 원점에서 반경 `_shadowExtent` 안의 점은 뷰 z 가 [거리 - 반경, 거리 + 반경] 이므로 그대로 쓴다.
        // (거리가 반경보다 작으면 near 가 음수가 되는데, 직교 투영에는 문제가 되지 않는다. 선형 사상일 뿐이다.)
        const float32 extent    = _shadowExtent * 2.0f;
        const float32 nearPlane = _shadowDistance - _shadowExtent;
        const float32 farPlane  = _shadowDistance + _shadowExtent;
        return float4x4::createLookAt( eye, float3::Zero, up ) *
               float4x4::createOrthographic( extent, extent, nearPlane, farPlane );
    }

    DirectionalShadowProjection DirectionalLightComponent::buildShadowProjection( uint32 shadowMapResolution ) const
    {
        DirectionalShadowProjection projection{};
        projection._viewProj       = buildShadowViewProj();
        projection._resolution     = MathUtil::max( shadowMapResolution, 1u );
        projection._texelWorldSize = _shadowExtent * 2.0f / static_cast<float32>( projection._resolution );
        projection._depthRange     = _shadowExtent * 2.0f; // buildShadowViewProj 의 [거리 - 반경, 거리 + 반경]
        return projection;
    }

    DirectionalShadowProjection DirectionalLightComponent::buildShadowProjectionForView( const float4x4& cameraViewProj, uint32 shadowMapResolution ) const
    {
        using Internal = DirectionalLightComponentInternal;
        if ( _shadowViewDistance <= 0.0f )
            return buildShadowProjection( shadowMapResolution );

        // (1) 카메라 절두체의 꼭짓점 여덟 — NDC 상자(x · y ∈ [-1, 1], z ∈ [0, 1])를 역행렬로 되돌린다. 직교 · 원근이 같은 식이다.
        const float4x4 invViewProj = cameraViewProj.invert();
        float3         arrCorner[8];
        for ( uint32 cornerIndex = 0; cornerIndex < 8; ++cornerIndex )
        {
            const float4  ndc{ ( cornerIndex & 1u ) != 0 ? 1.0f : -1.0f, ( cornerIndex & 2u ) != 0 ? 1.0f : -1.0f, ( cornerIndex & 4u ) != 0 ? 1.0f : 0.0f, 1.0f };
            const float4  world    = float4::transform( ndc, invViewProj );
            const float32 inverseW = MathUtil::abs( world._w ) > MathUtil::kEpsilon ? 1.0f / world._w : 1.0f;
            arrCorner[cornerIndex] = float3{ world._x * inverseW, world._y * inverseW, world._z * inverseW };
        }

        // (2) 그림자 거리 — 먼 꼭짓점을 모서리 광선을 따라 가까운 꼭짓점에서 `_shadowViewDistance` 까지로 당긴다(유니티 Shadow Distance).
        for ( uint32 cornerIndex = 0; cornerIndex < 4; ++cornerIndex )
        {
            const float3  ray    = arrCorner[cornerIndex + 4] - arrCorner[cornerIndex];
            const float32 length = ray.getLength();
            if ( length > _shadowViewDistance )
                arrCorner[cornerIndex + 4] = arrCorner[cornerIndex] + ray * ( _shadowViewDistance / length );
        }

        // (3) 받는 높이 띠로 자른다. 절두체 ∩ 띠는 볼록 다면체이고, 꼭짓점은 띠 안의 꼭짓점과 모서리 열둘이 띠의 두 경계면을 지나는 점이다.
        const bool bHeightBand = _shadowReceiverMaxHeight > _shadowReceiverMinHeight;
        float3     arrPoint[Internal::kMaxFitPoint];
        uint32     pointCount = 0;
        for ( const float3& corner : arrCorner )
        {
            if ( bHeightBand == false || ( corner._y >= _shadowReceiverMinHeight && corner._y <= _shadowReceiverMaxHeight ) )
                arrPoint[pointCount++] = corner;
        }
        if ( bHeightBand )
        {
            const float32 arrPlaneHeight[2] = { _shadowReceiverMinHeight, _shadowReceiverMaxHeight };
            for ( const auto& edge : Internal::kArrFrustumEdge )
            {
                const float3& from = arrCorner[edge[0]];
                const float3& to   = arrCorner[edge[1]];
                const float32 rise = to._y - from._y;
                if ( MathUtil::abs( rise ) <= MathUtil::kEpsilon )
                    continue;
                for ( const float32 planeHeight : arrPlaneHeight )
                {
                    const float32 t = ( planeHeight - from._y ) / rise;
                    if ( t > 0.0f && t < 1.0f )
                        arrPoint[pointCount++] = from + ( to - from ) * t;
                }
            }
        }
        // 카메라가 띠를 보지 않으면(하늘만 본다) 받는 면이 없다 — 고정 볼륨으로 돌아간다.
        if ( pointCount == 0 )
            return buildShadowProjection( shadowMapResolution );

        // (4) 빛 공간 상자 — 회전만 있는 뷰(눈 = 원점)로 옮겨 x · y · 깊이의 최소 · 최대를 잰다.
        const float3   lightDir  = getLightDirection();
        const float3   up        = MathUtil::abs( lightDir._y ) > 0.99f ? float3::Forward : float3::Up;
        const float4x4 lightView = float4x4::createLookAt( float3::Zero, lightDir, up );
        float3         minPoint  = float3::transform( arrPoint[0], lightView );
        float3         maxPoint  = minPoint;
        for ( uint32 pointIndex = 1; pointIndex < pointCount; ++pointIndex )
        {
            const float3 lightPoint = float3::transform( arrPoint[pointIndex], lightView );
            minPoint                = float3::min( minPoint, lightPoint );
            maxPoint                = float3::max( maxPoint, lightPoint );
        }

        // (5) 크기를 2 m 단위로 올리고(+ 한 칸 여유 — 스냅이 왼쪽 · 아래로 텍셀 하나까지 밀어도 덮는다) 원점을 텍셀 격자에 맞춘다. 팬하면 크기가
        //     그대로라 같은 월드 점이 같은 텍셀 자리에 떨어지고 가장자리가 기어 다니지 않는다(언리얼 · 유니티 CSM 의 텍셀 스냅).
        const uint32  resolution = MathUtil::max( shadowMapResolution, 1u );
        const float32 width      = ( MathUtil::ceil( ( maxPoint._x - minPoint._x ) / Internal::kShadowFitQuantum ) + 1.0f ) * Internal::kShadowFitQuantum;
        const float32 height     = ( MathUtil::ceil( ( maxPoint._y - minPoint._y ) / Internal::kShadowFitQuantum ) + 1.0f ) * Internal::kShadowFitQuantum;
        const float32 texelX     = width / static_cast<float32>( resolution );
        const float32 texelY     = height / static_cast<float32>( resolution );
        const float32 left       = MathUtil::floor( minPoint._x / texelX ) * texelX;
        const float32 bottom     = MathUtil::floor( minPoint._y / texelY ) * texelY;
        // 빛 쪽으로 `_shadowDistance` 만큼 당긴다 — 받는 상자보다 빛에 가까운 가리는 물체(띠 위 · 화면 밖의 나무 · 레일)가 가까운 면에 잘리지 않게.
        const float32 nearPlane = minPoint._z - _shadowDistance;
        const float32 farPlane  = maxPoint._z + Internal::kShadowFitDepthMargin;

        DirectionalShadowProjection projection{};
        projection._viewProj       = lightView * float4x4::createOrthographicOffCenter( left, left + width, bottom, bottom + height, nearPlane, farPlane );
        projection._resolution     = resolution;
        projection._texelWorldSize = MathUtil::max( texelX, texelY );
        projection._depthRange     = farPlane - nearPlane;
        return projection;
    }

    void DirectionalLightComponent::writeGPULightKindFields( GPULight& outLight ) const
    {
        const float3 direction     = getLightDirection();
        outLight._directionType._x = direction._x;
        outLight._directionType._y = direction._y;
        outLight._directionType._z = direction._z;
    }
} // namespace sw
