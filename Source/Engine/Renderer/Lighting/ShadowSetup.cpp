#include "pch.h"

#include "Engine/Renderer/Lighting/ShadowSetup.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/3D/DirectionalLightComponent.h"

namespace sw
{
    namespace
    {
        struct ShadowSetupInternal
        {
            /// @brief 점광 · 스폿 칸 가장자리의 여유 텍셀 — PCF 3x3(쌍선형 2x2 탭)이 칸 밖을 읽지 않게 시야각을 넓힌다.
            static constexpr float32 kLocalGuardTexels = 3.0f;
            /// @brief 점광 · 스폿 그림자의 가까운 면 — 반경의 이 비율(최소 kLocalMinNearPlane m).
            static constexpr float32 kLocalNearFraction = 0.01f;
            static constexpr float32 kLocalMinNearPlane = 0.05f;
            /// @brief 스폿 그림자 시야각 반각의 상한(라디안, 80°) — 원뿔이 90°에 가까우면 탄젠트가 끝없이 커진다.
            static constexpr float32 kMaxSpotHalfAngle = 1.3962634f;
            /// @brief 점광 · 스폿의 노멀 오프셋 · 깊이 바이어스(텍셀 수) — 원근 칸은 텍셀 크기가 거리에 비례하므로 셰이더가 거리를 곱한다.
            static constexpr float32 kLocalNormalOffsetTexels = 2.0f;
            static constexpr float32 kLocalDepthBiasTexels    = 1.5f;
            /// @brief 점광 · 스폿 그림자의 세기 — 가려진 점은 그 빛을 받지 않는다(방향광의 그늘 세기와 달리 그 빛 하나의 몫만 지운다).
            static constexpr float32 kLocalShadowStrength = 1.0f;

            /// @brief 점광 면 여섯의 시선 축과 위 방향 — 셰이더의 면 고르기(+X −X +Y −Y +Z −Z)와 같은 순서다.
            static constexpr float3 kArrPointFaceAxis[shaderslot::kPointShadowFaceCount] = {
                { 1.0f,  0.0f,  0.0f},
                {-1.0f,  0.0f,  0.0f},
                { 0.0f,  1.0f,  0.0f},
                { 0.0f, -1.0f,  0.0f},
                { 0.0f,  0.0f,  1.0f},
                { 0.0f,  0.0f, -1.0f}
            };
            static constexpr float3 kArrPointFaceUp[shaderslot::kPointShadowFaceCount] = {
                {0.0f, 1.0f,  0.0f},
                {0.0f, 1.0f,  0.0f},
                {0.0f, 0.0f, -1.0f},
                {0.0f, 0.0f,  1.0f},
                {0.0f, 1.0f,  0.0f},
                {0.0f, 1.0f,  0.0f}
            };

            /**
             * @brief 칸 안의 NDC 를 아틀라스 NDC 로 옮기는 행렬입니다(클립 공간에 곱한다 — 원근에서도 맞다).
             * @details 칸 uv = NDC · (0.5, −0.5) + 0.5 를 아틀라스 uv = 원점 + 칸 uv · 배율 로 옮기면 x' = s · x + (2 ox + s − 1), y' = s · y + (1 − 2 oy − s) 다.
             */
            static float4x4 makeAtlasPlacement( const ShadowAtlasCell& cell, uint32 atlasResolution )
            {
                const float32 inverseResolution = 1.0f / static_cast<float32>( MathUtil::max( atlasResolution, 1u ) );
                const float32 scale             = static_cast<float32>( cell._size ) * inverseResolution;
                const float32 originU           = static_cast<float32>( cell._x ) * inverseResolution;
                const float32 originV           = static_cast<float32>( cell._y ) * inverseResolution;
                const float32 offsetX           = 2.0f * originU + scale - 1.0f;
                const float32 offsetY           = 1.0f - 2.0f * originV - scale;
                return float4x4{ scale, 0.0f, 0.0f, 0.0f, 0.0f, scale, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, offsetX, offsetY, 0.0f, 1.0f };
            }

            /** @brief 행벡터 규약 행렬의 행 넷을 라이트 버퍼 원소 하나에 담습니다(셰이더가 `float4x4( 행 넷 )` 으로 되돌린다). */
            static GPULight makeMatrixElement( const float4x4& matrix )
            {
                GPULight element{};
                element._positionRadius = float4{ matrix._11, matrix._12, matrix._13, matrix._14 };
                element._colorIntensity = float4{ matrix._21, matrix._22, matrix._23, matrix._24 };
                element._directionType  = float4{ matrix._31, matrix._32, matrix._33, matrix._34 };
                element._params         = float4{ matrix._41, matrix._42, matrix._43, matrix._44 };
                return element;
            }

            /** @brief 원소의 빛 종류입니다(`_directionType.w`). */
            static uint32 getLightType( const GPULight& light ) { return static_cast<uint32>( light._directionType._w + 0.5f ); }

            /** @brief 칸을 나눠 줄 차례 하나 — 우선순위와 빛 원소 번호입니다. */
            struct PriorityEntry
            {
                float32 _priority{ 0.0f };
                uint32  _lightIndex{ 0 };
            };

            /** @brief 우선순위가 높은 쪽이 앞입니다(정렬 비교). */
            static bool isHigherPriority( const PriorityEntry& first, const PriorityEntry& second ) { return first._priority > second._priority; }

            /** @brief 빛 공간 상자에 경계 구들을 더한 상자의 직교 뷰-투영입니다 — 그림자 패스 컬링이 쓴다. */
            static float4x4 makeUnionCullViewProj( const float4x4& lightView, const float3& boxMin, const float3& boxMax, const vector<float4>& listBound )
            {
                float3 unionMin = boxMin;
                float3 unionMax = boxMax;
                for ( const float4& bound : listBound )
                {
                    const float3 center = float3::transform( float3{ bound._x, bound._y, bound._z }, lightView );
                    const float3 extent{ bound._w, bound._w, bound._w };
                    unionMin = float3::min( unionMin, center - extent );
                    unionMax = float3::max( unionMax, center + extent );
                }
                return lightView * float4x4::makeOrthographicOffCenter( unionMin._x, unionMax._x, unionMin._y, unionMax._y, unionMin._z, unionMax._z );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ShadowAtlasLayout ShadowAtlasLayout::make( uint32 atlasResolution, uint32 cascadeCount, uint32 localTileCount )
    {
        ShadowAtlasLayout layout{};
        layout._atlasResolution = MathUtil::max( atlasResolution, 4u );
        uint32 cascades         = MathUtil::clamp( cascadeCount, 1u, shaderslot::kShadowCascadeMax );
        if ( localTileCount > 0 )
            cascades = MathUtil::min( cascades, kMaxCascadeWithLocalShadow );
        layout._cascadeCount  = cascades;
        layout._bQuadrant     = ( cascades > 1 || localTileCount > 0 ) ? SW_TRUE : SW_FALSE;
        layout._localCapacity = localTileCount > 0 ? ( shaderslot::kShadowCascadeMax - cascades ) * kLocalCellsPerQuadrant : 0u;
        return layout;
    }

    ShadowAtlasCell ShadowAtlasLayout::getCascadeCell( uint32 cascadeIndex ) const
    {
        if ( _bQuadrant == SW_FALSE )
            return ShadowAtlasCell{ 0u, 0u, _atlasResolution };
        const uint32 half = _atlasResolution / 2u;
        return ShadowAtlasCell{ ( cascadeIndex & 1u ) * half, ( cascadeIndex >> 1u ) * half, half };
    }

    ShadowAtlasCell ShadowAtlasLayout::getLocalCell( uint32 tileIndex ) const
    {
        const uint32 quadrant = _cascadeCount + tileIndex / kLocalCellsPerQuadrant;
        const uint32 sub      = tileIndex % kLocalCellsPerQuadrant;
        const uint32 half     = _atlasResolution / 2u;
        const uint32 quarter  = _atlasResolution / 4u;
        return ShadowAtlasCell{ ( quadrant & 1u ) * half + ( sub & 1u ) * quarter, ( quadrant >> 1u ) * half + ( sub >> 1u ) * quarter, quarter };
    }
} // namespace sw

namespace sw
{
    uint32 ShadowSetupUtil::countLocalTiles( const vector<GPULight>& listLight, const vector<uint32>& listShadowLightIndex )
    {
        uint32 tileCount = 0;
        for ( const uint32 lightIndex : listShadowLightIndex )
        {
            if ( lightIndex >= listLight.size() )
                continue;
            const uint32 lightType = ShadowSetupInternal::getLightType( listLight[lightIndex] );
            if ( lightType == shaderslot::kLightTypePoint )
                tileCount += shaderslot::kPointShadowFaceCount;
            else if ( lightType == shaderslot::kLightTypeSpot )
                tileCount += 1u;
        }
        return tileCount;
    }

    float32 ShadowSetupUtil::computeLocalShadowPriority( const GPULight& light, const float4x4& viewProj )
    {
        const float4  clip   = float4::transform( float4{ light._positionRadius._x, light._positionRadius._y, light._positionRadius._z, 1.0f }, viewProj );
        const float32 radius = MathUtil::max( light._positionRadius._w, 0.0f );
        // 구 전체가 카메라 뒤 — 그 빛이 닿는 곳은 화면에 없다.
        if ( clip._w + radius <= 0.0f )
            return 0.0f;
        return radius / MathUtil::max( clip._w, MathUtil::max( radius * kLocalPriorityMinDepthFraction, MathUtil::kEpsilon ) );
    }

    void ShadowSetupUtil::populateLocalShadows( const ShadowAtlasLayout& layout, const float4x4* pPriorityViewProj, const vector<uint32>& listShadowLightIndex,
                                                vector<GPULight>& inoutListLight, FrameShadowData& outFrame )
    {
        using Internal               = ShadowSetupInternal;
        const uint32 atlasResolution = layout.getAtlasResolution();
        uint32       usedTileCount   = 0;

        // 칸이 모자랄 때 화면에 크게 보이는 빛이 먼저 받는다. 같은 값은 목록 순서를 지킨다(stable).
        vector<Internal::PriorityEntry> listOrder;
        listOrder.reserve( listShadowLightIndex.size() );
        for ( const uint32 lightIndex : listShadowLightIndex )
        {
            if ( lightIndex >= inoutListLight.size() )
                continue;
            const float32 priority = pPriorityViewProj != nullptr ? computeLocalShadowPriority( inoutListLight[lightIndex], *pPriorityViewProj ) : 0.0f;
            listOrder.push_back( Internal::PriorityEntry{ priority, lightIndex } );
        }
        VectorUtil::stableSort( listOrder, Internal::isHigherPriority );

        for ( const Internal::PriorityEntry& entry : listOrder )
        {
            const uint32 lightIndex = entry._lightIndex;
            GPULight&    light      = inoutListLight[lightIndex];
            const uint32 lightType  = Internal::getLightType( light );
            const bool   bPoint     = lightType == shaderslot::kLightTypePoint;
            const bool   bSpot      = lightType == shaderslot::kLightTypeSpot;
            if ( bPoint == false && bSpot == false )
                continue;
            const uint32 tileCount = bPoint ? shaderslot::kPointShadowFaceCount : 1u;
            // 칸이 모자라면 이 빛은 그림자 없이 비춘다. 뒤의 스폿(칸 하나)은 아직 들어갈 수 있으므로 멈추지 않는다.
            if ( usedTileCount + tileCount > layout.getLocalCapacity() )
                continue;

            const float3  position{ light._positionRadius._x, light._positionRadius._y, light._positionRadius._z };
            const float32 radius    = MathUtil::max( light._positionRadius._w, Internal::kLocalMinNearPlane * 2.0f );
            const float32 nearPlane = MathUtil::max( radius * Internal::kLocalNearFraction, Internal::kLocalMinNearPlane );
            light._params._x        = 1.0f;
            light._params._w        = static_cast<float32>( usedTileCount * shaderslot::kShadowTileElementCount );

            for ( uint32 faceIndex = 0; faceIndex < tileCount; ++faceIndex )
            {
                const ShadowAtlasCell cell     = layout.getLocalCell( usedTileCount + faceIndex );
                const float32         cellSize = static_cast<float32>( MathUtil::max( cell._size, 8u ) );
                // 칸 가장자리에 여유 텍셀을 두도록 시야각을 넓힌다 — 면을 고른 점은 90° 원뿔 안이라 여유 밖으로 나가지 않는다.
                const float32 guardScale = cellSize / ( cellSize - 2.0f * Internal::kLocalGuardTexels );
                float3        forward    = Internal::kArrPointFaceAxis[faceIndex];
                float3        up         = Internal::kArrPointFaceUp[faceIndex];
                float32       tanHalf    = guardScale;
                if ( bSpot )
                {
                    forward                 = float3{ light._directionType._x, light._directionType._y, light._directionType._z };
                    up                      = MathUtil::abs( forward._y ) > 0.99f ? float3::Forward : float3::Up;
                    const float32 outerHalf = MathUtil::min( MathUtil::acos( MathUtil::clamp( light._params._y, -1.0f, 1.0f ) ), Internal::kMaxSpotHalfAngle );
                    tanHalf                 = MathUtil::tan( outerHalf ) * guardScale;
                }
                const float32  fieldOfView = 2.0f * MathUtil::atan2( tanHalf, 1.0f );
                const float4x4 viewProj    = float4x4::makeLookAt( position, position + forward, up ) *
                                          float4x4::makePerspectiveFieldOfView( fieldOfView, 1.0f, nearPlane, radius );
                outFrame._listLocalTile.push_back( ShadowTileDraw{ viewProj, cell } );

                // 셰이딩은 아틀라스 자리를 접은 행렬로 투영한다 — 클립 공간 변환이라 원근 나눗셈 뒤에도 칸 안에 떨어진다.
                outFrame._listTileElement.push_back( Internal::makeMatrixElement( viewProj * Internal::makeAtlasPlacement( cell, atlasResolution ) ) );
                const float32 inverseResolution = 1.0f / static_cast<float32>( atlasResolution );
                const float32 texelAngle        = 2.0f * tanHalf / cellSize; // 거리 1 m 에서 텍셀 하나의 월드 길이
                GPULight      info{};
                // 쌍선형 2x2 탭이 칸 밖을 읽지 않는 uv 범위 — 칸 가장자리에서 반 텍셀 안쪽.
                info._positionRadius = float4{ ( static_cast<float32>( cell._x ) + 0.5f ) * inverseResolution, ( static_cast<float32>( cell._y ) + 0.5f ) * inverseResolution,
                                               ( static_cast<float32>( cell._x + cell._size ) - 0.5f ) * inverseResolution,
                                               ( static_cast<float32>( cell._y + cell._size ) - 0.5f ) * inverseResolution };
                info._colorIntensity = float4{ texelAngle * Internal::kLocalNormalOffsetTexels, texelAngle * Internal::kLocalDepthBiasTexels, inverseResolution,
                                               Internal::kLocalShadowStrength };
                outFrame._listTileElement.push_back( info );
            }
            outFrame._listLocalBound.push_back( float4{ position._x, position._y, position._z, radius } );
            usedTileCount += tileCount;
        }
    }

    ViewShadowData ShadowSetupUtil::makeViewShadow( const DirectionalShadowProjection& projection, const ShadowAtlasLayout& layout, const vector<float4>& listLocalBound )
    {
        ViewShadowData data{};
        data._lightViewProj = projection._viewProj;
        data._cullViewProj  = listLocalBound.empty()
                                ? projection._viewProj
                                : ShadowSetupInternal::makeUnionCullViewProj( projection._lightView, projection._lightSpaceMin, projection._lightSpaceMax, listLocalBound );
        data._shadowParams  = projection.computeShaderParams();
        data._cascadeCount  = MathUtil::clamp( MathUtil::min( projection._cascadeCount, layout.getCascadeCount() ), 1u, shaderslot::kShadowCascadeMax );
        data._cascadeLayout = float4{ static_cast<float32>( data._cascadeCount ), layout.getCascadeTileScale(), kCascadeBlendFraction, 0.0f };
        for ( uint32 cascadeIndex = 0; cascadeIndex < shaderslot::kShadowCascadeMax; ++cascadeIndex )
        {
            const bool bUsed                   = cascadeIndex < data._cascadeCount;
            data._arrCascade[cascadeIndex]     = projection.computeCascadeShaderValue( bUsed ? cascadeIndex : shaderslot::kShadowCascadeMax );
            data._arrCascadeTile[cascadeIndex] = ShadowTileDraw{ bUsed ? projection.computeCascadeViewProj( cascadeIndex ) : float4x4{}, layout.getCascadeCell( cascadeIndex ) };
        }
        data._bValid = SW_TRUE;
        return data;
    }

    ViewShadowData ShadowSetupUtil::makeUnshadowedView( const ShadowAtlasLayout& layout )
    {
        // 행렬은 기본값으로 둔다 — 그림자를 받는 방향광(`params.x`)이 없으므로 셰이더는 이 볼륨을 읽지 않는다. 캐스케이드 하나 · 깊이 끝 무한이라 고르기만 성립한다.
        ViewShadowData data           = ViewShadowData{};
        data._cascadeCount            = 1;
        data._cascadeLayout           = float4{ 1.0f, layout.getCascadeTileScale(), kCascadeBlendFraction, 0.0f };
        data._arrCascade[0]           = float4{ 1.0f, 0.0f, 0.0f, DirectionalShadowCascade::kUnboundedDepth };
        data._arrCascadeTile[0]._cell = layout.getCascadeCell( 0 );
        data._bValid                  = SW_TRUE;
        return data;
    }

    void ShadowSetupUtil::populateFrame( const DirectionalLightComponent* pShadowLight, const float4x4* pMainViewProj, uint32 atlasResolution,
                                         const vector<uint32>& listShadowLightIndex, vector<GPULight>& inoutListLight, FrameShadowData& outFrame )
    {
        outFrame.clear();
        const uint32 cascadeCount = pShadowLight != nullptr ? pShadowLight->getShadowCascadeCount() : 1u;
        outFrame._layout          = ShadowAtlasLayout::make( atlasResolution, cascadeCount, countLocalTiles( inoutListLight, listShadowLightIndex ) );
        populateLocalShadows( outFrame._layout, pMainViewProj, listShadowLightIndex, inoutListLight, outFrame );

        if ( pShadowLight != nullptr )
        {
            // 볼륨 맞춤 · 텍셀 스냅 · 바이어스는 렌더 스레드가 그릴 캐스케이드 칸과 같은 해상도로 측정한다.
            const uint32                      tileResolution = outFrame._layout.getCascadeTileResolution();
            const DirectionalShadowProjection projection =
                pMainViewProj != nullptr ? pShadowLight->computeShadowProjectionForView( *pMainViewProj, tileResolution, outFrame._layout.getCascadeCount() )
                                         : pShadowLight->computeShadowProjection( tileResolution );
            outFrame._mainView = makeViewShadow( projection, outFrame._layout, outFrame._listLocalBound );
        }
        else
        {
            // 그림자를 드리우는 방향광이 없다(주광이 그림자를 끈 경우와 주광이 아예 없는 경우가 같다) — 그림자 없는 뷰다. 키 라이트로 볼륨을 대신 만들지 않는다.
            outFrame._mainView = makeUnshadowedView( outFrame._layout );
        }
    }

    ViewShadowData ShadowSetupUtil::makeViewShadowFor( const DirectionalLightComponent* pShadowLight, const float4x4& viewProj, const FrameShadowData& frame )
    {
        if ( pShadowLight == nullptr )
            return frame._mainView;
        const uint32                      tileResolution = frame._layout.getCascadeTileResolution();
        const DirectionalShadowProjection projection     = pShadowLight->computeShadowProjectionForView( viewProj, tileResolution, frame._layout.getCascadeCount() );
        return makeViewShadow( projection, frame._layout, frame._listLocalBound );
    }
} // namespace sw
