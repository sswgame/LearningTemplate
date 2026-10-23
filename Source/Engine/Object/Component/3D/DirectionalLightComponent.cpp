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
         *        `kDefaultColor` 가 같은 익명 네임스페이스에서 재정의됩니다(docs/agents/Helpers.md 의 Internal 규칙).
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
            static constexpr uint32  kDefaultShadowCascadeCount{ 1 };
            /// @brief 맞춘 볼륨의 지름을 올리는 단위(m)의 상한 — 줌할 때 크기가 매 프레임 조금씩 바뀌어 텍셀 격자가 떨리는 것을 줄인다.
            static constexpr float32 kShadowFitQuantum{ 2.0f };
            /// @brief 단위는 지름의 이 분의 일보다 크지 않다 — 몇 m 짜리 가까운 캐스케이드가 2 m 단위로 부풀지 않게.
            static constexpr float32 kShadowFitQuantumDivisions{ 8.0f };
            /// @brief 맞춘 볼륨의 먼 쪽 깊이에 더하는 여유(m) — 띠 경계에 걸친 면이 원평면에 잘리지 않게.
            static constexpr float32 kShadowFitDepthMargin{ 1.0f };
            /// @brief 캐스케이드 분할에서 로그 분할의 몫(나머지는 균등) — GPU Gems 3 의 실용 분할(언리얼 CascadeDistributionExponent · 유니티 분할 비율의 자리).
            static constexpr float32 kShadowCascadeSplitLambda{ 0.75f };
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

            /** @brief 맞춘 캐스케이드 하나의 빛 공간 정사각형과 깊이 구간입니다. */
            struct FittedBox
            {
                float32 _left{ 0.0f };
                float32 _bottom{ 0.0f };
                float32 _size{ 0.0f };
                float32 _minDepth{ 0.0f };
                float32 _maxDepth{ 0.0f };
            };

            /**
             * @brief 절두체 조각(꼭짓점 여덟) ∩ 받는 높이 띠의 꼭짓점을 모읍니다. 띠가 없으면 꼭짓점 여덟 그대로입니다.
             * @details 절두체 ∩ 띠는 볼록 다면체이고, 꼭짓점은 띠 안의 꼭짓점과 모서리 열둘이 띠의 두 경계면을 지나는 점이다.
             */
            static uint32 collectBandPoints( const float3 ( &arrCorner )[8], bool bHeightBand, float32 minHeight, float32 maxHeight, float3 ( &outArrPoint )[kMaxFitPoint] )
            {
                uint32 pointCount = 0;
                for ( const float3& corner : arrCorner )
                {
                    if ( bHeightBand == false || ( minHeight <= corner._y && corner._y <= maxHeight ) )
                        outArrPoint[pointCount++] = corner;
                }
                if ( bHeightBand == false )
                    return pointCount;
                const float32 arrPlaneHeight[2] = { minHeight, maxHeight };
                for ( const auto& edge : kArrFrustumEdge )
                {
                    const float3& from = arrCorner[edge[0]];
                    const float3& to   = arrCorner[edge[1]];
                    const float32 rise = to._y - from._y;
                    if ( MathUtil::abs( rise ) <= MathUtil::kEpsilon )
                        continue;
                    for ( const float32 planeHeight : arrPlaneHeight )
                    {
                        const float32 t = ( planeHeight - from._y ) / rise;
                        if ( 0.0f < t && t < 1.0f )
                            outArrPoint[pointCount++] = from + ( to - from ) * t;
                    }
                }
                return pointCount;
            }

            /**
             * @brief 점들을 감싸는 경계 구를 빛 공간 정사각형으로 맞춥니다. 크기는 반지름만 보므로 카메라가 돌아도 그대로입니다.
             * @details 중심은 꼭짓점 평균이다 — 점 집합이 카메라와 함께 돌면 평균도 같이 돌아 반지름이 변하지 않는다(최소 구는 순서에 따라 흔들리는 근사가 많다).
             *          지름은 양자 단위로 올리고(+ 한 칸 여유 — 스냅이 왼쪽 · 아래로 텍셀 하나까지 밀어도 덮는다) 왼쪽 아래를 텍셀 격자에 맞춘다. 팬하면
             *          크기가 그대로라 같은 월드 점이 같은 텍셀 자리에 떨어지고 가장자리가 기어 다니지 않는다(언리얼 · 유니티 CSM 의 텍셀 스냅).
             *          깊이는 점들의 실제 구간이다(깊이는 스냅하지 않으므로 반짝임과 무관하고, 좁을수록 정밀하다).
             */
            static FittedBox fitStableBox( const float3* pPoint, uint32 pointCount, const float4x4& lightView, uint32 resolution )
            {
                float3 center = float3::Zero;
                for ( uint32 pointIndex = 0; pointIndex < pointCount; ++pointIndex )
                {
                    center = center + pPoint[pointIndex];
                }
                center                  = center * ( 1.0f / static_cast<float32>( pointCount ) );
                float32   radiusSquared = 0.0f;
                FittedBox box{};
                box._minDepth = MathUtil::kMaxFloat;
                box._maxDepth = -MathUtil::kMaxFloat;
                for ( uint32 pointIndex = 0; pointIndex < pointCount; ++pointIndex )
                {
                    radiusSquared            = MathUtil::max( radiusSquared, ( pPoint[pointIndex] - center ).getLengthSquared() );
                    const float32 pointDepth = float3::transform( pPoint[pointIndex], lightView )._z;
                    box._minDepth            = MathUtil::min( box._minDepth, pointDepth );
                    box._maxDepth            = MathUtil::max( box._maxDepth, pointDepth );
                }
                const float32 diameter    = MathUtil::sqrt( radiusSquared ) * 2.0f;
                const float32 quantum     = MathUtil::clamp( diameter / kShadowFitQuantumDivisions, MathUtil::kEpsilon, kShadowFitQuantum );
                box._size                 = ( MathUtil::ceil( diameter / quantum ) + 1.0f ) * quantum;
                const float32 texel       = box._size / static_cast<float32>( resolution );
                const float3  lightCenter = float3::transform( center, lightView );
                box._left                 = MathUtil::floor( ( lightCenter._x - box._size * 0.5f ) / texel ) * texel;
                box._bottom               = MathUtil::floor( ( lightCenter._y - box._size * 0.5f ) / texel ) * texel;
                return box;
            }

            /** @brief 캐스케이드 경계 @p splitIndex 의 깊이 비율(0 = 가까운 면, 1 = 그림자 거리)입니다 — 로그 분할과 균등 분할을 섞는다. */
            static float32 computeSplitFraction( uint32 splitIndex, uint32 cascadeCount, float32 nearDepth, float32 farDepth )
            {
                const float32 fraction    = static_cast<float32>( splitIndex ) / static_cast<float32>( cascadeCount );
                const float32 safeNear    = MathUtil::max( nearDepth, MathUtil::kEpsilon );
                const float32 logDepth    = safeNear * MathUtil::pow( farDepth / safeNear, fraction );
                const float32 linearDepth = nearDepth + ( farDepth - nearDepth ) * fraction;
                const float32 depth       = kShadowCascadeSplitLambda * logDepth + ( 1.0f - kShadowCascadeSplitLambda ) * linearDepth;
                return MathUtil::clamp( ( depth - nearDepth ) / MathUtil::max( farDepth - nearDepth, MathUtil::kEpsilon ), 0.0f, 1.0f );
            }

            /** @brief 빛이 나아가는 방향을 내려다보는 빛 공간의 위 방향입니다. 빛과 거의 나란하면 다른 축으로 갈아탄다. */
            static float3 computeLightUp( const float3& lightDir ) { return MathUtil::abs( lightDir._y ) > 0.99f ? float3::Forward : float3::Up; }

            /** @brief 카메라 절두체의 꼭짓점 여덟 — NDC 상자(x · y ∈ [-1, 1], z ∈ [0, 1])를 역행렬로 되돌린다. 직교 · 원근이 같은 식이다. */
            static void computeFrustumCorners( const float4x4& cameraViewProj, float3 ( &outArrCorner )[8] )
            {
                const float4x4 invViewProj = cameraViewProj.invert();
                for ( uint32 cornerIndex = 0; cornerIndex < 8; ++cornerIndex )
                {
                    const float4  ndc{ ( cornerIndex & 1u ) != 0 ? 1.0f : -1.0f, ( cornerIndex & 2u ) != 0 ? 1.0f : -1.0f, ( cornerIndex & 4u ) != 0 ? 1.0f : 0.0f, 1.0f };
                    const float4  world       = float4::transform( ndc, invViewProj );
                    const float32 inverseW    = MathUtil::abs( world._w ) > MathUtil::kEpsilon ? 1.0f / world._w : 1.0f;
                    outArrCorner[cornerIndex] = float3{ world._x * inverseW, world._y * inverseW, world._z * inverseW };
                }
            }

            /** @brief 그림자 거리로 자른 절두체의 깊이 구간입니다. */
            struct ShadowDepthRange
            {
                float32 _nearDepth{ 0.0f };
                float32 _maxDepth{ 0.0f };
                bool    _bPerspective{ false };
            };

            /**
             * @brief 먼 면을 그림자 거리(@p shadowViewDistance)까지 당깁니다. 원근 카메라는 클립 w 가 곧 뷰 깊이라 가까운 · 먼 꼭짓점의 w 로 깊이를 재고 먼 면을 그림자 거리까지 당긴다(유니티 Shadow Distance).
             *        직교 카메라는 w 가 늘 1 이다 — 먼 꼭짓점을 모서리 광선을 따라 가까운 꼭짓점에서 @p shadowViewDistance 까지로 당긴다.
             */
            static ShadowDepthRange limitFrustumToShadowDistance( const float4x4& cameraViewProj, float32 shadowViewDistance, float3 ( &inoutArrCorner )[8] )
            {
                ShadowDepthRange range{};
                range._nearDepth       = float4::transform( float4{ inoutArrCorner[0]._x, inoutArrCorner[0]._y, inoutArrCorner[0]._z, 1.0f }, cameraViewProj )._w;
                const float32 farDepth = float4::transform( float4{ inoutArrCorner[4]._x, inoutArrCorner[4]._y, inoutArrCorner[4]._z, 1.0f }, cameraViewProj )._w;
                range._bPerspective    = MathUtil::abs( farDepth - range._nearDepth ) > MathUtil::kEpsilon;
                range._maxDepth        = farDepth;
                if ( range._bPerspective )
                {
                    range._maxDepth           = MathUtil::clamp( shadowViewDistance, range._nearDepth, farDepth );
                    const float32 farFraction = ( range._maxDepth - range._nearDepth ) / ( farDepth - range._nearDepth );
                    for ( uint32 cornerIndex = 0; cornerIndex < 4; ++cornerIndex )
                    {
                        inoutArrCorner[cornerIndex + 4] = inoutArrCorner[cornerIndex] + ( inoutArrCorner[cornerIndex + 4] - inoutArrCorner[cornerIndex] ) * farFraction;
                    }
                }
                else
                {
                    for ( uint32 cornerIndex = 0; cornerIndex < 4; ++cornerIndex )
                    {
                        const float3  ray    = inoutArrCorner[cornerIndex + 4] - inoutArrCorner[cornerIndex];
                        const float32 length = ray.getLength();
                        if ( length > shadowViewDistance )
                            inoutArrCorner[cornerIndex + 4] = inoutArrCorner[cornerIndex] + ray * ( shadowViewDistance / length );
                    }
                }
                return range;
            }

            /** @brief 캐스케이드 정사각형들을 덮는 기준 볼륨(빛 공간 정사각형과 깊이 구간)입니다. */
            struct BaseVolume
            {
                float32 _left{ 0.0f };
                float32 _bottom{ 0.0f };
                float32 _side{ 0.0f };
                float32 _nearPlane{ 0.0f };
                float32 _farPlane{ 0.0f };
            };

            /**
             * @brief 캐스케이드 정사각형들을 덮는 정사각형(하나면 그 캐스케이드 그대로)을 만듭니다. 깊이는 모든 캐스케이드가 같은 구간을 쓴다:
             *        빛 쪽으로 @p shadowDistance 만큼 당겨 받는 상자보다 빛에 가까운 가리는 물체(띠 위 · 화면 밖의 나무 · 레일)가 가까운 면에 잘리지 않게 한다.
             */
            static BaseVolume coverBoxes( const FittedBox* pBox, uint32 boxCount, float32 shadowDistance )
            {
                float32 left     = pBox[0]._left;
                float32 bottom   = pBox[0]._bottom;
                float32 right    = pBox[0]._left + pBox[0]._size;
                float32 top      = pBox[0]._bottom + pBox[0]._size;
                float32 minDepth = pBox[0]._minDepth;
                float32 maxBoxZ  = pBox[0]._maxDepth;
                for ( uint32 boxIndex = 1; boxIndex < boxCount; ++boxIndex )
                {
                    left     = MathUtil::min( left, pBox[boxIndex]._left );
                    bottom   = MathUtil::min( bottom, pBox[boxIndex]._bottom );
                    right    = MathUtil::max( right, pBox[boxIndex]._left + pBox[boxIndex]._size );
                    top      = MathUtil::max( top, pBox[boxIndex]._bottom + pBox[boxIndex]._size );
                    minDepth = MathUtil::min( minDepth, pBox[boxIndex]._minDepth );
                    maxBoxZ  = MathUtil::max( maxBoxZ, pBox[boxIndex]._maxDepth );
                }
                BaseVolume volume{};
                volume._side      = MathUtil::max( right - left, top - bottom );
                volume._left      = ( boxCount == 1 ) ? left : ( left + right - volume._side ) * 0.5f;
                volume._bottom    = ( boxCount == 1 ) ? bottom : ( bottom + top - volume._side ) * 0.5f;
                volume._nearPlane = minDepth - shadowDistance;
                volume._farPlane  = maxBoxZ + kShadowFitDepthMargin;
                return volume;
            }
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

    float4 DirectionalShadowProjection::computeCascadeShaderValue( uint32 cascadeIndex ) const
    {
        if ( cascadeIndex >= _cascadeCount || cascadeIndex >= kMaxCascade )
            return float4{ 1.0f, 0.0f, 0.0f, -DirectionalShadowCascade::kUnboundedDepth }; // 쓰지 않는 칸 — 어떤 깊이도 고르지 않는다
        const DirectionalShadowCascade& cascade = _arrCascade[cascadeIndex];
        return float4{ cascade._scale, cascade._offsetX, cascade._offsetY, cascade._splitDepth };
    }

    float4x4 DirectionalShadowProjection::computeCascadeViewProj( uint32 cascadeIndex ) const
    {
        const float4 value = computeCascadeShaderValue( cascadeIndex );
        // 행벡터 규약: clip' = clip · M — x' = x · 배율 + w · 이동, 깊이와 w 는 그대로다(직교라 w = 1).
        const float4x4 cascadeTransform{ value._x, 0.0f, 0.0f, 0.0f, 0.0f, value._x, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, value._y, value._z, 0.0f, 1.0f };
        return _viewProj * cascadeTransform;
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
        , _shadowCascadeCount{ DirectionalLightComponentInternal::kDefaultShadowCascadeCount }
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

    void DirectionalLightComponent::setShadowCascadeCount( uint32 count )
    {
        _shadowCascadeCount = MathUtil::clamp( count, 1u, DirectionalShadowProjection::kMaxCascade );
        onPropertyChanged( hashed_string( "_shadowCascadeCount" ) );
    }

    void DirectionalLightComponent::setCastShadow( bool bCastShadow )
    {
        _bCastShadow = bCastShadow ? SW_TRUE : SW_FALSE;
        onPropertyChanged( hashed_string( "_bCastShadow" ) );
    }

    float4x4 DirectionalLightComponent::computeShadowViewProj() const
    {
        const float3 lightDir = getLightDirection();

        // 라이트를 빛이 오는 쪽에 두고 빛 방향으로 원점을 내려다본다. up 이 빛과 거의 나란하면
        // side 축이 사라지므로 다른 축으로 갈아탄다.
        const float3 up  = DirectionalLightComponentInternal::computeLightUp( lightDir );
        const float3 eye = lightDir * -_shadowDistance;

        // **깊이 범위는 눈을 기준으로 잡는다.** 눈이 원점에서 거리만큼 떨어져 원점을 보고 있으므로 씬의 뷰 z 는
        // 거리 언저리다. 주의: `(-거리, +거리)` 로 잡으면 `makeOrthographic` 은 `z' = (z_view - near) / (far - near)` 라
        // 씬 전체가 z' ≈ 1(원평면)로 뭉개지고, 깊이 비교가 늘 "가려지지 않음" 이 되어 **그림자가 지지 않는다**.
        // 원점에서 반경 `_shadowExtent` 안의 점은 뷰 z 가 [거리 - 반경, 거리 + 반경] 이므로 그대로 쓴다.
        // (거리가 반경보다 작으면 near 가 음수가 되는데, 직교 투영에는 문제가 되지 않는다. 선형 사상일 뿐이다.)
        const float32 extent    = _shadowExtent * 2.0f;
        const float32 nearPlane = _shadowDistance - _shadowExtent;
        const float32 farPlane  = _shadowDistance + _shadowExtent;
        return float4x4::makeLookAt( eye, float3::Zero, up ) *
               float4x4::makeOrthographic( extent, extent, nearPlane, farPlane );
    }

    DirectionalShadowProjection DirectionalLightComponent::computeShadowProjection( uint32 shadowMapResolution ) const
    {
        const float3                lightDir = getLightDirection();
        DirectionalShadowProjection projection{};
        projection._viewProj                      = computeShadowViewProj();
        projection._lightView                     = float4x4::makeLookAt( lightDir * -_shadowDistance, float3::Zero, DirectionalLightComponentInternal::computeLightUp( lightDir ) );
        projection._lightSpaceMin                 = float3{ -_shadowExtent, -_shadowExtent, _shadowDistance - _shadowExtent }; // computeShadowViewProj 의 상자
        projection._lightSpaceMax                 = float3{ _shadowExtent, _shadowExtent, _shadowDistance + _shadowExtent };
        projection._resolution                    = MathUtil::max( shadowMapResolution, 1u );
        projection._texelWorldSize                = _shadowExtent * 2.0f / static_cast<float32>( projection._resolution );
        projection._depthRange                    = _shadowExtent * 2.0f; // computeShadowViewProj 의 [거리 - 반경, 거리 + 반경]
        projection._cascadeCount                  = 1;
        projection._arrCascade[0]._texelWorldSize = projection._texelWorldSize;
        return projection;
    }

    DirectionalShadowProjection DirectionalLightComponent::computeShadowProjectionForView( const float4x4& cameraViewProj, uint32 shadowMapResolution, uint32 maxCascadeCount ) const
    {
        using Internal = DirectionalLightComponentInternal;
        if ( _shadowViewDistance <= 0.0f )
            return computeShadowProjection( shadowMapResolution );

        // (1) 카메라 절두체의 꼭짓점 여덟, (2) 그림자 거리까지 자른 깊이 구간.
        float3 arrCorner[8];
        Internal::computeFrustumCorners( cameraViewProj, arrCorner );
        const Internal::ShadowDepthRange depthRange   = Internal::limitFrustumToShadowDistance( cameraViewProj, _shadowViewDistance, arrCorner );
        const float32                    nearDepth    = depthRange._nearDepth;
        const float32                    maxDepth     = depthRange._maxDepth;
        const bool                       bPerspective = depthRange._bPerspective;

        // (3) 캐스케이드마다 — 절두체 조각 ∩ 받는 높이 띠를 경계 구로 감싸 빛 공간 정사각형을 맞춘다. 직교 카메라는 깊이가 텍셀 크기를 바꾸지 않으므로 하나다.
        const uint32   requestedCount = bPerspective ? MathUtil::min( maxCascadeCount, _shadowCascadeCount ) : 1u;
        const uint32   cascadeCount   = MathUtil::clamp( requestedCount, 1u, DirectionalShadowProjection::kMaxCascade );
        const uint32   resolution     = MathUtil::max( shadowMapResolution, 1u );
        const float3   lightDir       = getLightDirection();
        const float4x4 lightView      = float4x4::makeLookAt( float3::Zero, lightDir, Internal::computeLightUp( lightDir ) );
        const bool     bHeightBand    = _shadowReceiverMaxHeight > _shadowReceiverMinHeight;

        DirectionalShadowProjection projection{};
        Internal::FittedBox         arrBox[DirectionalShadowProjection::kMaxCascade];
        uint32                      fittedCount   = 0;
        float32                     startFraction = 0.0f;
        for ( uint32 cascadeIndex = 0; cascadeIndex < cascadeCount; ++cascadeIndex )
        {
            const float32 endFraction = ( cascadeIndex + 1 == cascadeCount ) ? 1.0f : Internal::computeSplitFraction( cascadeIndex + 1, cascadeCount, nearDepth, maxDepth );
            float3        arrSlice[8];
            for ( uint32 cornerIndex = 0; cornerIndex < 4; ++cornerIndex )
            {
                const float3 edge         = arrCorner[cornerIndex + 4] - arrCorner[cornerIndex];
                arrSlice[cornerIndex]     = arrCorner[cornerIndex] + edge * startFraction;
                arrSlice[cornerIndex + 4] = arrCorner[cornerIndex] + edge * endFraction;
            }
            float3       arrPoint[Internal::kMaxFitPoint];
            const uint32 pointCount = Internal::collectBandPoints( arrSlice, bHeightBand, _shadowReceiverMinHeight, _shadowReceiverMaxHeight, arrPoint );
            // 띠를 보지 않는 조각(하늘만 본다)에는 받는 면이 없다. 조각은 깊이 순서라 뒤의 조각도 대개 같다 — 여기서 멈춘다.
            if ( pointCount == 0 )
                break;
            arrBox[fittedCount]               = Internal::fitStableBox( arrPoint, pointCount, lightView, resolution );
            DirectionalShadowCascade& cascade = projection._arrCascade[fittedCount];
            cascade._splitDepth               = bPerspective ? nearDepth + ( maxDepth - nearDepth ) * endFraction : DirectionalShadowCascade::kUnboundedDepth;
            cascade._texelWorldSize           = arrBox[fittedCount]._size / static_cast<float32>( resolution );
            ++fittedCount;
            startFraction = endFraction;
        }
        // 카메라가 띠를 보지 않으면(하늘만 본다) 받는 면이 없다 — 고정 볼륨으로 돌아간다.
        if ( fittedCount == 0 )
            return computeShadowProjection( shadowMapResolution );

        // (4) 기준 볼륨 — 캐스케이드 정사각형들을 덮는 정사각형.
        const Internal::BaseVolume base       = Internal::coverBoxes( arrBox, fittedCount, _shadowDistance );
        const float32              side       = base._side;
        const float32              baseLeft   = base._left;
        const float32              baseBottom = base._bottom;
        const float32              nearPlane  = base._nearPlane;
        const float32              farPlane   = base._farPlane;

        projection._viewProj       = lightView * float4x4::makeOrthographicOffCenter( baseLeft, baseLeft + side, baseBottom, baseBottom + side, nearPlane, farPlane );
        projection._lightView      = lightView;
        projection._lightSpaceMin  = float3{ baseLeft, baseBottom, nearPlane };
        projection._lightSpaceMax  = float3{ baseLeft + side, baseBottom + side, farPlane };
        projection._resolution     = resolution;
        projection._texelWorldSize = side / static_cast<float32>( resolution );
        projection._depthRange     = farPlane - nearPlane;
        projection._cascadeCount   = fittedCount;

        // (5) 캐스케이드 c 의 NDC = 기준 NDC · (기준 폭 / c 폭) + 2 (기준 중심 − c 중심) / c 폭. 하나뿐이면 정확히 배율 1 · 이동 0 이다(행렬이 같다).
        if ( fittedCount > 1 )
        {
            const float32 baseCenterX = baseLeft + side * 0.5f;
            const float32 baseCenterY = baseBottom + side * 0.5f;
            for ( uint32 cascadeIndex = 0; cascadeIndex < fittedCount; ++cascadeIndex )
            {
                const Internal::FittedBox& box     = arrBox[cascadeIndex];
                DirectionalShadowCascade&  cascade = projection._arrCascade[cascadeIndex];
                cascade._scale                     = side / box._size;
                cascade._offsetX                   = 2.0f * ( baseCenterX - ( box._left + box._size * 0.5f ) ) / box._size;
                cascade._offsetY                   = 2.0f * ( baseCenterY - ( box._bottom + box._size * 0.5f ) ) / box._size;
            }
        }
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
