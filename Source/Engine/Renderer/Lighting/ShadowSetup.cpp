#include "pch.h"

#include "Engine/Renderer/Lighting/ShadowSetup.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Config/EngineConfig.h"
#include "Engine/Renderer/Lighting/ShadowFilter.h"
#include "Engine/Visual/Component/DirectionalLightComponent.h"

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
            /// @brief 점광 · 스폿의 노멀 오프셋(텍셀 수) — 원근 칸은 텍셀 크기가 거리에 비례하므로 셰이더가 거리를 곱한다. 깊이 바이어스는 `RenderConfig::_shadowLocalDepthBiasTexels`.
            static constexpr float32 kLocalNormalOffsetTexels = 2.0f;
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
                uint64  _lightID{ 0 };
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
    ShadowAtlasLayout ShadowAtlasLayout::make( uint32 atlasResolution, uint32 cascadeCount )
    {
        ShadowAtlasLayout layout{};
        layout._atlasResolution = MathUtil::max( atlasResolution, 4u );
        layout._cascadeCount    = MathUtil::clamp( cascadeCount, 1u, shaderslot::kShadowCascadeMax );
        return layout;
    }

    ShadowAtlasCell ShadowAtlasLayout::getCascadeCell( uint32 cascadeIndex ) const
    {
        if ( _cascadeCount <= 1 )
            return ShadowAtlasCell{ 0u, 0u, _atlasResolution };
        const uint32 half = _atlasResolution / 2u;
        return ShadowAtlasCell{ ( cascadeIndex & 1u ) * half, ( cascadeIndex >> 1u ) * half, half };
    }
} // namespace sw

namespace sw
{
    void LocalShadowAtlas::build( uint32 atlasResolution, uint32 minTileResolution, const vector<LocalShadowRequest>& listRequest )
    {
        const uint32 resolution = MathUtil::max( atlasResolution, 4u );
        const uint32 maxTile    = resolution / 2u;
        // 칸은 아틀라스 반 ÷ 2ⁿ 이다 — 하한은 그 꼴 가운데 하한 값 이상인 가장 작은 것.
        uint32 minTile = maxTile;
        while ( minTile / 2u >= MathUtil::max( minTileResolution, 1u ) )
            minTile /= 2u;
        _minTileResolution = minTile;

        const size_t requestCount = listRequest.size();
        _listSize.resize( requestCount );
        for ( size_t requestIndex = 0; requestIndex < requestCount; ++requestIndex )
        {
            // 원하는 크기 이상인 가장 작은 칸(올림) — 화면에서 작아지면 칸도 반씩 준다.
            const uint32 desired = listRequest[requestIndex]._desiredSize;
            uint32       size    = maxTile;
            while ( size / 2u >= minTile && size / 2u >= desired )
                size /= 2u;
            _listSize[requestIndex] = size;
        }
        // 넓이가 넘치면 하한보다 큰 칸을 모두 반으로 — 크기 차례(우선순위 차례)가 그대로 남는다.
        const uint64 atlasArea = static_cast<uint64>( resolution ) * resolution;
        for ( ;; )
        {
            uint64 area = 0;
            for ( size_t requestIndex = 0; requestIndex < requestCount; ++requestIndex )
            {
                area += static_cast<uint64>( listRequest[requestIndex]._faceCount ) * _listSize[requestIndex] * _listSize[requestIndex];
            }
            if ( area <= atlasArea )
                break;
            bool bShrunk = false;
            for ( uint32& size : _listSize )
            {
                if ( size > minTile )
                {
                    size /= 2u;
                    bShrunk = true;
                }
            }
            if ( bShrunk == false )
                break; // 모두 하한이다 — 뒤의 빛은 칸을 못 받는다
        }

        const bool bKeep = resolution == _atlasResolution;
        _atlasResolution = resolution;
        if ( place( listRequest, bKeep ) == false && bKeep )
            (void)place( listRequest, false ); // 지킨 자리가 빈 곳을 조각냈다 — 처음부터 쌓는다

        // 다음 프레임이 지킬 자리 — 이번에 칸을 받은 빛만 남는다(사라진 빛의 칸은 풀린다).
        _mapLastCell.clear();
        for ( size_t requestIndex = 0; requestIndex < requestCount; ++requestIndex )
        {
            const uint32 firstCell = _listFirstCell[requestIndex];
            if ( firstCell == kNotPlaced )
                continue;
            for ( uint32 faceIndex = 0; faceIndex < listRequest[requestIndex]._faceCount; ++faceIndex )
            {
                _mapLastCell.insert_or_assign( makeTileKey( listRequest[requestIndex]._lightID, faceIndex ), _listCell[firstCell + faceIndex] );
            }
        }
    }

    bool LocalShadowAtlas::place( const vector<LocalShadowRequest>& listRequest, bool bKeep )
    {
        const size_t requestCount = listRequest.size();
        _listFirstCell.assign( requestCount, kNotPlaced );
        _listOffset.resize( requestCount );
        uint32 cellCount = 0;
        _unit            = _atlasResolution / 2u;
        for ( size_t requestIndex = 0; requestIndex < requestCount; ++requestIndex )
        {
            _listOffset[requestIndex] = cellCount;
            cellCount += listRequest[requestIndex]._faceCount;
            _unit = MathUtil::min( _unit, _listSize[requestIndex] );
        }
        _listCell.assign( cellCount, ShadowAtlasCell{} );
        _unit     = MathUtil::max( _unit, 1u );
        _gridSize = _atlasResolution / _unit;
        _listOccupied.assign( static_cast<size_t>( _gridSize ) * _gridSize, 0 );

        // 1) 지난 프레임에 같은 크기로 서 있던 빛은 그 자리 그대로 — 칸은 자기 크기의 배수 자리라 이번 격자(가장 작은 칸)에도 맞는다.
        if ( bKeep )
        {
            for ( size_t requestIndex = 0; requestIndex < requestCount; ++requestIndex )
            {
                const LocalShadowRequest& request = listRequest[requestIndex];
                const uint32              units   = _listSize[requestIndex] / _unit;
                bool                      bKept   = true;
                for ( uint32 faceIndex = 0; faceIndex < request._faceCount && bKept; ++faceIndex )
                {
                    const auto it = _mapLastCell.find( makeTileKey( request._lightID, faceIndex ) );
                    bKept         = it != _mapLastCell.end() && it->second._size == _listSize[requestIndex] && isFree( it->second._x / _unit, it->second._y / _unit, units );
                    if ( bKept )
                        _listCell[_listOffset[requestIndex] + faceIndex] = it->second;
                    // 면끼리 겹치지 않게 지금 채운다(못 지키면 아래에서 비운다).
                    if ( bKept )
                        mark( it->second._x / _unit, it->second._y / _unit, units, true );
                }
                if ( bKept )
                {
                    _listFirstCell[requestIndex] = _listOffset[requestIndex];
                    continue;
                }
                for ( uint32 faceIndex = 0; faceIndex < request._faceCount; ++faceIndex )
                {
                    const ShadowAtlasCell& cell = _listCell[_listOffset[requestIndex] + faceIndex];
                    if ( cell._size != 0 )
                        mark( cell._x / _unit, cell._y / _unit, units, false );
                    _listCell[_listOffset[requestIndex] + faceIndex] = ShadowAtlasCell{};
                }
            }
        }

        // 2) 나머지는 우선순위 차례로 첫 빈 자리 — 크기가 내림차순이라 넓이가 남으면 반드시 든다(빈 곳이 모두 지금 크기의 배수 덩어리다).
        bool bAllPlaced = true;
        for ( size_t requestIndex = 0; requestIndex < requestCount; ++requestIndex )
        {
            if ( _listFirstCell[requestIndex] != kNotPlaced )
                continue;
            const LocalShadowRequest& request     = listRequest[requestIndex];
            const uint32              units       = _listSize[requestIndex] / _unit;
            uint32                    placedFaces = 0;
            for ( ; placedFaces < request._faceCount; ++placedFaces )
            {
                bool bFound = false;
                for ( uint32 unitY = 0; unitY + units <= _gridSize && bFound == false; unitY += units )
                {
                    for ( uint32 unitX = 0; unitX + units <= _gridSize && bFound == false; unitX += units )
                    {
                        if ( isFree( unitX, unitY, units ) == false )
                            continue;
                        mark( unitX, unitY, units, true );
                        _listCell[_listOffset[requestIndex] + placedFaces] = ShadowAtlasCell{ unitX * _unit, unitY * _unit, _listSize[requestIndex] };
                        bFound                                             = true;
                    }
                }
                if ( bFound == false )
                    break;
            }
            if ( placedFaces == request._faceCount )
            {
                _listFirstCell[requestIndex] = _listOffset[requestIndex];
                continue;
            }
            // 면 일부만 들어갔다 — 빛 하나는 면을 다 받거나 하나도 받지 않는다.
            for ( uint32 faceIndex = 0; faceIndex < placedFaces; ++faceIndex )
            {
                const ShadowAtlasCell& cell = _listCell[_listOffset[requestIndex] + faceIndex];
                mark( cell._x / _unit, cell._y / _unit, units, false );
            }
            bAllPlaced = false;
        }
        return bAllPlaced;
    }

    bool LocalShadowAtlas::isFree( uint32 unitX, uint32 unitY, uint32 units ) const
    {
        if ( unitX + units > _gridSize || unitY + units > _gridSize )
            return false;
        for ( uint32 y = unitY; y < unitY + units; ++y )
        {
            for ( uint32 x = unitX; x < unitX + units; ++x )
            {
                if ( _listOccupied[static_cast<size_t>( y ) * _gridSize + x] != 0 )
                    return false;
            }
        }
        return true;
    }

    void LocalShadowAtlas::mark( uint32 unitX, uint32 unitY, uint32 units, bool bOccupied )
    {
        for ( uint32 y = unitY; y < unitY + units; ++y )
        {
            for ( uint32 x = unitX; x < unitX + units; ++x )
            {
                _listOccupied[static_cast<size_t>( y ) * _gridSize + x] = static_cast<uint8>( bOccupied ? 1 : 0 );
            }
        }
    }
} // namespace sw

namespace sw
{
    float32 FrameShadowData::computeLocalShadowPriority( const GPULight& light, const float4x4& viewProj )
    {
        const float4  clip   = float4::transform( float4{ light._positionRadius._x, light._positionRadius._y, light._positionRadius._z, 1.0f }, viewProj );
        const float32 radius = MathUtil::max( light._positionRadius._w, 0.0f );
        // 구 전체가 카메라 뒤 — 그 빛이 닿는 곳은 화면에 없다.
        if ( clip._w + radius <= 0.0f )
            return 0.0f;
        return radius / MathUtil::max( clip._w, MathUtil::max( radius * kLocalPriorityMinDepthFraction, MathUtil::kEpsilon ) );
    }

    uint32 FrameShadowData::computeLocalShadowDesiredSize( float32 priority, const float4x4& viewProj, uint32 atlasResolution )
    {
        // 행벡터 규약 — 클립 x 는 첫 열과의 내적이고 그 열의 xyz 길이가 투영 배율이다(뷰 회전은 길이를 바꾸지 않는다).
        const float32 scaleX     = float3{ viewProj._11, viewProj._21, viewProj._31 }.getLength();
        const float32 scaleY     = float3{ viewProj._12, viewProj._22, viewProj._32 }.getLength();
        const float32 ndcRadius  = priority * MathUtil::max( scaleX, scaleY );
        const float32 resolution = static_cast<float32>( atlasResolution );
        return static_cast<uint32>( MathUtil::clamp( ndcRadius * resolution, 0.0f, resolution ) );
    }

    void FrameShadowData::populateLocalShadows( uint32 atlasResolution, const float4x4* pPriorityViewProj, const RenderConfig& renderConfig, const vector<LocalShadowLight>& listShadowLight,
                                                LocalShadowAtlas& inoutLocalAtlas, vector<GPULight>& inoutListLight, FrameShadowData& outFrame )
    {
        using Internal       = ShadowSetupInternal;
        uint32 usedTileCount = 0;

        // 칸이 모자랄 때 화면에 크게 보이는 빛이 먼저 받는다. 같은 값은 목록 순서를 지킨다(stable). 점광 · 스폿이 아닌 원소는 칸을 원하지 않는다.
        vector<Internal::PriorityEntry> listOrder;
        listOrder.reserve( listShadowLight.size() );
        for ( const LocalShadowLight& shadowLight : listShadowLight )
        {
            if ( shadowLight._lightIndex >= inoutListLight.size() )
                continue;
            const uint32 lightType = Internal::getLightType( inoutListLight[shadowLight._lightIndex] );
            if ( lightType != shaderslot::kLightTypePoint && lightType != shaderslot::kLightTypeSpot )
                continue;
            const float32 priority = pPriorityViewProj != nullptr ? computeLocalShadowPriority( inoutListLight[shadowLight._lightIndex], *pPriorityViewProj ) : 0.0f;
            listOrder.push_back( Internal::PriorityEntry{ priority, shadowLight._lightIndex, shadowLight._lightID } );
        }
        VectorUtil::stableSort( listOrder, Internal::isHigherPriority );

        // 칸 크기는 화면 크기로 — 카메라가 없으면 가장 큰 칸을 원한다(넘치면 할당기가 고루 줄인다).
        vector<LocalShadowRequest> listRequest;
        listRequest.reserve( listOrder.size() );
        for ( const Internal::PriorityEntry& entry : listOrder )
        {
            const bool   bPoint  = Internal::getLightType( inoutListLight[entry._lightIndex] ) == shaderslot::kLightTypePoint;
            const uint32 desired = pPriorityViewProj != nullptr ? computeLocalShadowDesiredSize( entry._priority, *pPriorityViewProj, atlasResolution ) : atlasResolution;
            listRequest.push_back( LocalShadowRequest{ entry._lightID, bPoint ? shaderslot::kPointShadowFaceCount : 1u, desired } );
        }
        inoutLocalAtlas.build( atlasResolution, renderConfig._shadowLocalMinTileResolution, listRequest );

        for ( uint32 requestIndex = 0; requestIndex < static_cast<uint32>( listOrder.size() ); ++requestIndex )
        {
            const Internal::PriorityEntry& entry      = listOrder[requestIndex];
            const uint32                   lightIndex = entry._lightIndex;
            GPULight&                      light      = inoutListLight[lightIndex];
            const bool                     bSpot      = Internal::getLightType( light ) == shaderslot::kLightTypeSpot;
            const uint32                   tileCount  = listRequest[requestIndex]._faceCount;
            const uint32                   firstCell  = inoutLocalAtlas.getFirstCell( requestIndex );
            // 칸이 모자라면 이 빛은 그림자 없이 비춘다. 뒤의 작은 빛은 아직 들어갈 수 있다(할당기가 계속 본다).
            if ( firstCell == LocalShadowAtlas::kNotPlaced )
            {
                SW_LOG_WARNING_ONCE_ID( "ShadowSetup.LocalShadowAtlasFull", "Local shadow atlas has no room for a point/spot shadow (smallest cell %# texels) - the light is drawn without shadows. Lower _shadowLocalMinTileResolution or the shadow light count",
                                        inoutLocalAtlas.getMinTileResolution() );
                continue;
            }

            const float3  position{ light._positionRadius._x, light._positionRadius._y, light._positionRadius._z };
            const float32 radius    = MathUtil::max( light._positionRadius._w, Internal::kLocalMinNearPlane * 2.0f );
            const float32 nearPlane = MathUtil::max( radius * Internal::kLocalNearFraction, Internal::kLocalMinNearPlane );
            light._params._x        = 1.0f;
            light._params._w        = static_cast<float32>( usedTileCount * shaderslot::kShadowTileElementCount );
            const uint32 lightSlot  = static_cast<uint32>( outFrame._listLocalBound.size() );

            for ( uint32 faceIndex = 0; faceIndex < tileCount; ++faceIndex )
            {
                const ShadowAtlasCell cell     = inoutLocalAtlas.getCell( firstCell + faceIndex );
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
                outFrame._listLocalTile.push_back( ShadowTileDraw{ viewProj, cell, LocalShadowAtlas::makeTileKey( entry._lightID, faceIndex ) } );
                outFrame._listLocalTileLight.push_back( lightSlot );

                // 셰이딩은 아틀라스 자리를 접은 행렬로 투영한다 — 클립 공간 변환이라 원근 나눗셈 뒤에도 칸 안에 떨어진다.
                outFrame._listTileElement.push_back( Internal::makeMatrixElement( viewProj * Internal::makeAtlasPlacement( cell, atlasResolution ) ) );
                const float32 inverseResolution = 1.0f / static_cast<float32>( atlasResolution );
                const float32 texelAngle        = 2.0f * tanHalf / cellSize; // 거리 1 m 에서 텍셀 하나의 월드 길이
                GPULight      info{};
                // 쌍선형 2x2 탭이 칸 밖을 읽지 않는 uv 범위 — 칸 가장자리에서 반 텍셀 안쪽.
                info._positionRadius = float4{ ( static_cast<float32>( cell._x ) + 0.5f ) * inverseResolution, ( static_cast<float32>( cell._y ) + 0.5f ) * inverseResolution,
                                               ( static_cast<float32>( cell._x + cell._size ) - 0.5f ) * inverseResolution,
                                               ( static_cast<float32>( cell._y + cell._size ) - 0.5f ) * inverseResolution };
                info._colorIntensity = float4{ texelAngle * Internal::kLocalNormalOffsetTexels, texelAngle * renderConfig._shadowLocalDepthBiasTexels, inverseResolution,
                                               Internal::kLocalShadowStrength };
                // PCSS 가 가리개 깊이를 시선 깊이로 되돌리고(가까운 면 · 먼 면) 반그림자를 텍셀로 잰다(원천 반지름 = 빛 원소의 `_extra.x`, `LightShadowSettings`).
                info._directionType = float4{ nearPlane, radius, texelAngle, MathUtil::max( light._extra._x, 0.0f ) };
                outFrame._listTileElement.push_back( info );
            }
            outFrame._listLocalBound.push_back( float4{ position._x, position._y, position._z, radius } );
            // 그림자 컬링 절두체 — 스폿은 그 타일(원뿔을 덮는 원근)이고, 점광은 면 여섯이 덮는 경계 구의 상자다(면마다 나누면 슬롯이 여섯 배다).
            outFrame._listLocalCullViewProj.push_back( bSpot ? outFrame._listLocalTile.back()._viewProj
                                                             : float4x4::makeLookAt( position, position + float3::Forward, float3::Up ) *
                                                                   float4x4::makeOrthographicOffCenter( -radius, radius, -radius, radius, -radius, radius ) );
            usedTileCount += tileCount;
        }
    }

    ViewShadowData FrameShadowData::makeViewShadow( const DirectionalShadowProjection& projection, const ShadowAtlasLayout& layout, const vector<float4>& listLocalBound )
    {
        ViewShadowData data{};
        data._lightViewProj            = projection._viewProj;
        data._cullViewProj             = listLocalBound.empty()
                                           ? projection._viewProj
                                           : ShadowSetupInternal::makeUnionCullViewProj( projection._lightView, projection._lightSpaceMin, projection._lightSpaceMax, listLocalBound );
        data._shadowParams             = projection.computeShaderParams();
        data._directionalPenumbraScale = ShadowFilter::computeDirectionalPenumbraScale( projection._depthRange, projection._sourceAngle, projection._texelWorldSize );
        data._cascadeCount             = MathUtil::clamp( MathUtil::min( projection._cascadeCount, layout.getCascadeCount() ), 1u, shaderslot::kShadowCascadeMax );
        data._cascadeLayout            = float4{ static_cast<float32>( data._cascadeCount ), layout.getCascadeTileScale(), kCascadeBlendFraction, projection._fadeStartDepth };
        for ( uint32 cascadeIndex = 0; cascadeIndex < shaderslot::kShadowCascadeMax; ++cascadeIndex )
        {
            const bool bUsed                   = cascadeIndex < data._cascadeCount;
            data._arrCascade[cascadeIndex]     = projection.computeCascadeShaderValue( bUsed ? cascadeIndex : shaderslot::kShadowCascadeMax );
            data._arrCascadeTile[cascadeIndex] = ShadowTileDraw{ bUsed ? projection.computeCascadeViewProj( cascadeIndex ) : float4x4{}, layout.getCascadeCell( cascadeIndex ) };
        }
        data._bValid = SW_TRUE;
        return data;
    }

    ViewShadowData FrameShadowData::makeUnshadowedView( const ShadowAtlasLayout& layout )
    {
        // 행렬은 기본값으로 둔다 — 그림자를 받는 방향광(`params.x`)이 없으므로 셰이더는 이 볼륨을 읽지 않는다. 캐스케이드 하나 · 깊이 끝 무한이라 고르기만 성립한다.
        ViewShadowData data           = ViewShadowData{};
        data._cascadeCount            = 1;
        data._cascadeLayout           = float4{ 1.0f, layout.getCascadeTileScale(), kCascadeBlendFraction, DirectionalShadowCascade::kUnboundedDepth };
        data._arrCascade[0]           = float4{ 1.0f, 0.0f, 0.0f, DirectionalShadowCascade::kUnboundedDepth };
        data._arrCascadeTile[0]._cell = layout.getCascadeCell( 0 );
        data._bValid                  = SW_TRUE;
        return data;
    }

    void FrameShadowData::populateContactShadows( vector<GPULight>& inoutListLight, FrameShadowData& outFrame )
    {
        using Internal                               = ShadowSetupInternal;
        outFrame._contactShadowLights                = kNoContactShadowLights;
        float32 arrLight[kContactShadowChannelCount] = { -1.0f, -1.0f, -1.0f, -1.0f };
        uint32  channelCount                         = 0;
        // 두 바퀴 — 방향광(대개 해, 화면 전체에 닿는다)이 먼저 채널을 받는다.
        for ( uint32 pass = 0; pass < 2; ++pass )
        {
            for ( uint32 lightIndex = 0; lightIndex < static_cast<uint32>( inoutListLight.size() ); ++lightIndex )
            {
                GPULight&    light        = inoutListLight[lightIndex];
                const uint32 lightType    = Internal::getLightType( light );
                const bool   bDirectional = lightType == shaderslot::kLightTypeDirectional;
                const bool   b3DLight     = bDirectional || lightType == shaderslot::kLightTypePoint || lightType == shaderslot::kLightTypeSpot;
                if ( b3DLight == false || bDirectional != ( pass == 0 ) )
                    continue;
                light._extra._z = 0.0f;
                if ( light._extra._y <= 0.0f || channelCount >= kContactShadowChannelCount )
                    continue;
                light._extra._z          = static_cast<float32>( channelCount + 1 );
                arrLight[channelCount++] = static_cast<float32>( lightIndex );
            }
        }
        outFrame._contactShadowLights = float4{ arrLight[0], arrLight[1], arrLight[2], arrLight[3] };
    }

    void FrameShadowData::populateFrame( const DirectionalLightComponent* pShadowLight, const float4x4* pMainViewProj, uint32 atlasResolution, const RenderConfig& renderConfig,
                                         const vector<LocalShadowLight>& listShadowLight, LocalShadowAtlas& inoutLocalAtlas, vector<GPULight>& inoutListLight, FrameShadowData& outFrame )
    {
        outFrame.clear();
        populateContactShadows( inoutListLight, outFrame );
        const uint32 cascadeCount = pShadowLight != nullptr ? pShadowLight->getShadowCascadeCount() : 1u;
        outFrame._layout          = ShadowAtlasLayout::make( atlasResolution, cascadeCount );
        populateLocalShadows( outFrame._layout.getAtlasResolution(), pMainViewProj, renderConfig, listShadowLight, inoutLocalAtlas, inoutListLight, outFrame );

        if ( pShadowLight != nullptr )
        {
            // 볼륨 맞춤 · 텍셀 스냅 · 바이어스는 렌더 스레드가 그릴 캐스케이드 칸과 같은 해상도로 측정한다.
            const uint32                      tileResolution = outFrame._layout.getCascadeTileResolution();
            const DirectionalShadowProjection projection =
                pMainViewProj != nullptr ? pShadowLight->computeShadowProjectionForView( *pMainViewProj, tileResolution, renderConfig, outFrame._layout.getCascadeCount() )
                                         : pShadowLight->computeShadowProjection( tileResolution );
            outFrame._mainView = makeViewShadow( projection, outFrame._layout, outFrame._listLocalBound );
        }
        else
        {
            // 그림자를 드리우는 방향광이 없다(주광이 그림자를 끈 경우와 주광이 아예 없는 경우가 같다) — 그림자 없는 뷰다. 키 라이트로 볼륨을 대신 만들지 않는다.
            outFrame._mainView = makeUnshadowedView( outFrame._layout );
        }
    }

    ViewShadowData FrameShadowData::makeViewShadowFor( const DirectionalLightComponent* pShadowLight, const float4x4& viewProj, const RenderConfig& renderConfig, const FrameShadowData& frame )
    {
        if ( pShadowLight == nullptr )
            return frame._mainView;
        const uint32                      tileResolution = frame._layout.getCascadeTileResolution();
        const DirectionalShadowProjection projection     = pShadowLight->computeShadowProjectionForView( viewProj, tileResolution, renderConfig, frame._layout.getCascadeCount() );
        return makeViewShadow( projection, frame._layout, frame._listLocalBound );
    }
} // namespace sw
