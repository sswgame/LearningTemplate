#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Object/Component/3D/DirectionalLightComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Renderer/Light/ShadowSetup.h"
#include "Engine/Scene/Scene.h"

#include "TestFramework/TestFramework.h"

// 그림자 배치(아틀라스 칸 · 캐스케이드 · 점광 · 스폿 타일 · 뷰별 볼륨)를 GPU 없이 행렬과 칸으로 본다. 픽셀은 RenderPassGPUTest 의 그림자 케이스가 본다.

namespace sw
{
    namespace
    {
        struct ShadowSetupTestInternal
        {
            static constexpr uint32 kAtlasResolution = 2048;

            /** @brief 행벡터 규약으로 점을 행렬에 곱하고 w 로 나눕니다(셰이더의 `mul( float4( p, 1 ), m )`). */
            static float3 projectToNdc( const float3& worldPos, const float4x4& matrix )
            {
                const float4 clip = float4::transform( float4{ worldPos._x, worldPos._y, worldPos._z, 1.0f }, matrix );
                return float3{ clip._x / clip._w, clip._y / clip._w, clip._z / clip._w };
            }

            /** @brief NDC xy 를 텍스처 uv 로(셰이더의 `ndc.xy * ( 0.5, -0.5 ) + 0.5`). */
            static float2 computeUv( const float3& ndc ) { return float2{ ndc._x * 0.5f + 0.5f, ndc._y * -0.5f + 0.5f }; }

            /** @brief 라이트 버퍼 원소 하나(행 넷)를 행렬로 되돌립니다(셰이더의 `float4x4( 행 넷 )`). */
            static float4x4 makeMatrixFromElement( const GPULight& element )
            {
                return float4x4{ element._positionRadius, element._colorIntensity, element._directionType, element._params };
            }

            /** @brief 점광 원소입니다(위치 · 반경 · 종류). */
            static GPULight makePointLight( const float3& position, float32 radius )
            {
                GPULight light{};
                light._positionRadius = float4{ position._x, position._y, position._z, radius };
                light._colorIntensity = float4{ 1.0f, 1.0f, 1.0f, 1.0f };
                light._directionType  = float4{ 0.0f, 0.0f, 0.0f, static_cast<float32>( shaderslot::kLightTypePoint ) };
                return light;
            }

            /** @brief 스폿 원소입니다(위치 · 반경 · 방향 · 바깥 원뿔 cos). */
            static GPULight makeSpotLight( const float3& position, float32 radius, const float3& direction, float32 outerHalfAngle )
            {
                GPULight light{};
                light._positionRadius = float4{ position._x, position._y, position._z, radius };
                light._colorIntensity = float4{ 1.0f, 1.0f, 1.0f, 1.0f };
                light._directionType  = float4{ direction._x, direction._y, direction._z, static_cast<float32>( shaderslot::kLightTypeSpot ) };
                light._params         = float4{ 0.0f, MathUtil::cos( outerHalfAngle ), MathUtil::cos( outerHalfAngle * 0.5f ), 0.0f };
                return light;
            }

            /** @brief 셰이더와 같은 규칙으로 점광 면을 고릅니다(+X −X +Y −Y +Z −Z). */
            static uint32 selectPointFace( const float3& fromLight )
            {
                const float32 ax = MathUtil::abs( fromLight._x );
                const float32 ay = MathUtil::abs( fromLight._y );
                const float32 az = MathUtil::abs( fromLight._z );
                if ( ax >= ay && ax >= az )
                    return fromLight._x >= 0.0f ? 0u : 1u;
                if ( ay >= az )
                    return fromLight._y >= 0.0f ? 2u : 3u;
                return fromLight._z >= 0.0f ? 4u : 5u;
            }

            /** @brief 원근 카메라의 뷰-투영입니다(눈 · 바라보는 점 · 16:9 · 세로 60°, 근 0.1 · 원 500 m). */
            static float4x4 makePerspectiveViewProj( const float3& eye, const float3& target )
            {
                return float4x4::makeLookAt( eye, target, float3::Up ) * float4x4::makePerspectiveFieldOfView( 1.0471976f, 16.0f / 9.0f, 0.1f, 500.0f );
            }

            /** @brief 탑다운 직교 카메라(피치 30° · 요 @p yaw)의 뷰-투영입니다 — ThemePark 리그와 같은 모양. */
            static float4x4 makeOrthoRigViewProj( const float3& focus, float32 orthoHeight, float32 yaw )
            {
                constexpr float32 kPitch = 0.5235988f;
                const float3      forward{ MathUtil::cos( kPitch ) * MathUtil::sin( yaw ), -MathUtil::sin( kPitch ), MathUtil::cos( kPitch ) * MathUtil::cos( yaw ) };
                const float3      eye = focus - forward * 250.0f;
                return float4x4::makeLookAt( eye, focus, float3::Up ) * float4x4::makeOrthographic( orthoHeight * 16.0f / 9.0f, orthoHeight, 0.1f, 625.0f );
            }

            /** @brief 해(고도 약 20°) 하나를 씬에 둡니다. 볼륨은 카메라에 맞춘다(띠 -2 ~ 40 m, 빛 쪽 120 m). */
            static DirectionalLightComponent* addSun( Scene& scene, float32 viewDistance, uint32 cascadeCount )
            {
                GameObject*                pSun   = scene.getObjectManager()->createGameObject( hashed_string( "Sun" ) );
                DirectionalLightComponent* pLight = pSun != nullptr ? pSun->addComponent<DirectionalLightComponent>() : nullptr;
                if ( pLight == nullptr )
                    return nullptr;
                pLight->setLocalRotation( float3{ 0.9f, 0.8f, 0.0f } );
                pLight->setShadowExtent( 180.0f );
                pLight->setShadowDistance( 120.0f );
                pLight->setShadowViewDistance( viewDistance );
                pLight->setShadowReceiverHeightRange( -2.0f, 40.0f );
                pLight->setShadowCascadeCount( cascadeCount );
                return pLight;
            }

            /** @brief 칸 두 개가 겹치는지 반환합니다. */
            static bool overlaps( const ShadowAtlasCell& first, const ShadowAtlasCell& second )
            {
                const bool bApartX = first._x + first._size <= second._x || second._x + second._size <= first._x;
                const bool bApartY = first._y + first._size <= second._y || second._y + second._size <= first._y;
                return ( bApartX || bApartY ) == false;
            }
        };
    } // namespace
} // namespace sw

/**
 * @brief [ShadowSetupTest] 캐스케이드 하나 · 점광 · 스폿 없음이면 아틀라스 전체가 그 캐스케이드다 — 아틀라스가 생기기 전과 같은 그림
 */
SW_TEST_CASE( ShadowSetupTest, SingleCascadeUsesTheWholeAtlas )
{
    using Internal                     = sw::ShadowSetupTestInternal;
    const sw::ShadowAtlasLayout layout = sw::ShadowAtlasLayout::make( Internal::kAtlasResolution, 1, 0 );
    SW_EXPECT_EQUAL( layout.getCascadeCount(), 1u );
    SW_EXPECT_EQUAL( layout.getLocalCapacity(), 0u );
    SW_EXPECT_NEAR_EQUAL( layout.getCascadeTileScale(), 1.0f, 1e-6f );
    SW_EXPECT_EQUAL( layout.getCascadeTileResolution(), Internal::kAtlasResolution );
    const sw::ShadowAtlasCell cell = layout.getCascadeCell( 0 );
    SW_EXPECT_EQUAL( cell._x, 0u );
    SW_EXPECT_EQUAL( cell._y, 0u );
    SW_EXPECT_EQUAL( cell._size, Internal::kAtlasResolution );
}

/**
 * @brief [ShadowSetupTest] 캐스케이드는 사분면을 하나씩, 점광 · 스폿은 남은 사분면의 2x2 칸을 쓰고 칸끼리 겹치지 않는다
 * @details 점광 · 스폿 그림자가 있으면 캐스케이드는 둘까지다(칸 여덟을 남긴다). 셰이더는 캐스케이드 c 의 원점을 사분면 c 로 구한다.
 */
SW_TEST_CASE( ShadowSetupTest, CascadesTakeQuadrantsAndLocalCellsTakeTheRest )
{
    using Internal                       = sw::ShadowSetupTestInternal;
    const sw::ShadowAtlasLayout cascades = sw::ShadowAtlasLayout::make( Internal::kAtlasResolution, 4, 0 );
    SW_EXPECT_EQUAL( cascades.getCascadeCount(), 4u );
    SW_EXPECT_NEAR_EQUAL( cascades.getCascadeTileScale(), 0.5f, 1e-6f );
    for ( uint32 cascadeIndex = 0; cascadeIndex < 4; ++cascadeIndex )
    {
        const sw::ShadowAtlasCell cell = cascades.getCascadeCell( cascadeIndex );
        SW_EXPECT_EQUAL( cell._size, Internal::kAtlasResolution / 2u );
        SW_EXPECT_EQUAL( cell._x, ( cascadeIndex & 1u ) * Internal::kAtlasResolution / 2u );
        SW_EXPECT_EQUAL( cell._y, ( cascadeIndex >> 1u ) * Internal::kAtlasResolution / 2u );
    }

    const sw::ShadowAtlasLayout mixed = sw::ShadowAtlasLayout::make( Internal::kAtlasResolution, 4, 7 );
    SW_EXPECT_EQUAL( mixed.getCascadeCount(), sw::ShadowAtlasLayout::kMaxCascadeWithLocalShadow );
    SW_EXPECT_EQUAL( mixed.getLocalCapacity(), 8u );
    sw::vector<sw::ShadowAtlasCell> listCell;
    for ( uint32 cascadeIndex = 0; cascadeIndex < mixed.getCascadeCount(); ++cascadeIndex )
    {
        listCell.push_back( mixed.getCascadeCell( cascadeIndex ) );
    }
    for ( uint32 tileIndex = 0; tileIndex < mixed.getLocalCapacity(); ++tileIndex )
    {
        const sw::ShadowAtlasCell cell = mixed.getLocalCell( tileIndex );
        SW_EXPECT_EQUAL( cell._size, Internal::kAtlasResolution / 4u );
        SW_EXPECT_TRUE( cell._x + cell._size <= Internal::kAtlasResolution && cell._y + cell._size <= Internal::kAtlasResolution );
        listCell.push_back( cell );
    }
    for ( size_t first = 0; first < listCell.size(); ++first )
    {
        for ( size_t second = first + 1; second < listCell.size(); ++second )
        {
            SW_EXPECT_FALSE_MSG( Internal::overlaps( listCell[first], listCell[second] ),
                                 ( "칸 " + sw::to_string( static_cast<uint64>( first ) ) + " 과 " + sw::to_string( static_cast<uint64>( second ) ) + " 이 겹친다" ).c_str() );
        }
    }
}

/**
 * @brief [ShadowSetupTest] 카메라가 없으면 점광 · 스폿은 앞에서부터 칸을 받고, 칸이 모자란 빛은 그림자 없이 남는다 — 빛 원소의 params.x · w 와 타일 원소 수
 * @details 캐스케이드 하나 + 점광 둘 + 스폿 하나면 칸 열둘 중 점광(6) · 스폿(1)이 먼저 받고 뒤의 점광(6)은 못 받는다. 스폿의 첫 타일 원소는 점광 여섯 면 뒤(12)다.
 */
SW_TEST_CASE( ShadowSetupTest, LocalLightsTakeCellsInOrderUntilTheAtlasIsFull )
{
    using Internal = sw::ShadowSetupTestInternal;
    sw::vector<sw::GPULight> listLight;
    listLight.push_back( sw::GPULight{} ); // 방향광 자리 — 번호 목록에 없으므로 그대로다
    listLight.push_back( Internal::makePointLight( sw::float3{ 0.0f, 3.0f, 0.0f }, 8.0f ) );
    listLight.push_back( Internal::makeSpotLight( sw::float3{ 5.0f, 4.0f, 0.0f }, 10.0f, sw::float3{ 0.0f, -1.0f, 0.0f }, 0.6f ) );
    listLight.push_back( Internal::makePointLight( sw::float3{ -6.0f, 2.0f, 0.0f }, 5.0f ) );
    const sw::vector<uint32> listShadowIndex{ 1u, 2u, 3u };

    SW_EXPECT_EQUAL( sw::ShadowSetupUtil::countLocalTiles( listLight, listShadowIndex ), 13u );
    sw::FrameShadowData frame;
    sw::ShadowSetupUtil::populateFrame( nullptr, nullptr, false, Internal::kAtlasResolution, listShadowIndex, listLight, frame );
    SW_EXPECT_EQUAL( frame._layout.getCascadeCount(), 1u );
    SW_EXPECT_EQUAL( frame._layout.getLocalCapacity(), 12u );

    SW_EXPECT_NEAR_EQUAL( listLight[1]._params._x, 1.0f, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( listLight[1]._params._w, 0.0f, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( listLight[2]._params._x, 1.0f, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( listLight[2]._params._w, static_cast<float32>( sw::shaderslot::kPointShadowFaceCount * sw::shaderslot::kShadowTileElementCount ), 1e-6f );
    SW_EXPECT_NEAR_EQUAL( listLight[3]._params._x, 0.0f, 1e-6f ); // 칸이 모자라 그림자 없음
    SW_EXPECT_NEAR_EQUAL( listLight[0]._params._x, 0.0f, 1e-6f );

    SW_EXPECT_EQUAL( frame._listLocalTile.size(), static_cast<size_t>( 7 ) );
    SW_EXPECT_EQUAL( frame._listTileElement.size(), static_cast<size_t>( 7 * sw::shaderslot::kShadowTileElementCount ) );
    SW_EXPECT_EQUAL( frame._listLocalBound.size(), static_cast<size_t>( 2 ) );
    // 주광이 없으면 주 시점 그림자는 비고 렌더러가 키 라이트로 폴백한다.
    SW_EXPECT_TRUE( frame._mainView._bValid == SW_FALSE );
}

/**
 * @brief [ShadowSetupTest] 카메라가 있으면 칸은 화면에 크게 보이는 빛부터 받는다 — 목록 앞의 먼 점광보다 뒤의 가까운 점광이 먼저다
 * @details 칸 열둘에 먼 점광(6) · 스폿(1) · 가까운 점광(6). 목록 순서면 가까운 점광이 빠지지만 우선순위(반경 / 카메라 깊이)로는 먼 점광이 빠진다.
 *          카메라 뒤의 빛은 0 이고, 카메라가 빛 구 안이면 값이 깊이 하한으로 막혀 유한하다.
 */
SW_TEST_CASE( ShadowSetupTest, LocalLightsTakeCellsByScreenPriority )
{
    using Internal                    = sw::ShadowSetupTestInternal;
    const sw::float4x4       viewProj = Internal::makePerspectiveViewProj( sw::float3{ 0.0f, 2.0f, 0.0f }, sw::float3{ 0.0f, 2.0f, 10.0f } );
    sw::vector<sw::GPULight> listLight;
    listLight.push_back( Internal::makePointLight( sw::float3{ 0.0f, 3.0f, 80.0f }, 8.0f ) );
    listLight.push_back( Internal::makeSpotLight( sw::float3{ 2.0f, 4.0f, 12.0f }, 10.0f, sw::float3{ 0.0f, -1.0f, 0.0f }, 0.6f ) );
    listLight.push_back( Internal::makePointLight( sw::float3{ -1.0f, 2.0f, 6.0f }, 6.0f ) );
    const sw::vector<uint32> listShadowIndex{ 0u, 1u, 2u };

    sw::FrameShadowData frame;
    sw::ShadowSetupUtil::populateFrame( nullptr, &viewProj, false, Internal::kAtlasResolution, listShadowIndex, listLight, frame );
    SW_ASSERT_EQUAL( frame._layout.getLocalCapacity(), 12u );
    SW_EXPECT_NEAR_EQUAL( listLight[0]._params._x, 0.0f, 1e-6f ); // 먼 점광은 칸이 모자라 그림자 없음
    SW_EXPECT_NEAR_EQUAL( listLight[1]._params._x, 1.0f, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( listLight[2]._params._x, 1.0f, 1e-6f );
    // 가까운 점광이 가장 먼저 받아 첫 타일 원소(0)를 쓰고, 스폿은 그 여섯 면 뒤다.
    SW_EXPECT_NEAR_EQUAL( listLight[2]._params._w, 0.0f, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( listLight[1]._params._w, static_cast<float32>( sw::shaderslot::kPointShadowFaceCount * sw::shaderslot::kShadowTileElementCount ), 1e-6f );
    SW_EXPECT_EQUAL( frame._listLocalTile.size(), static_cast<size_t>( 7 ) );

    const sw::GPULight behind = Internal::makePointLight( sw::float3{ 0.0f, 2.0f, -30.0f }, 5.0f );
    SW_EXPECT_NEAR_EQUAL( sw::ShadowSetupUtil::computeLocalShadowPriority( behind, viewProj ), 0.0f, 1e-6f );
    const sw::GPULight around   = Internal::makePointLight( sw::float3{ 0.0f, 2.0f, 0.0f }, 5.0f );
    const float32      priority = sw::ShadowSetupUtil::computeLocalShadowPriority( around, viewProj );
    SW_EXPECT_NEAR_EQUAL( priority, 1.0f / sw::ShadowSetupUtil::kLocalPriorityMinDepthFraction, 1e-3f );
}

/**
 * @brief [ShadowSetupTest] 점광의 면마다, 빛에서 그 축으로 나간 점은 셰이더의 면 고르기로 고른 면의 칸 안에 떨어지고 그 칸의 그릴 행렬로는 칸 NDC 안에 떨어진다
 * @details 셰이딩 행렬(아틀라스 자리를 접은 것)과 그리는 행렬(칸 NDC)이 같은 점을 같은 아틀라스 텍셀로 보내야 그림자가 맞는다.
 */
SW_TEST_CASE( ShadowSetupTest, PointFacesProjectIntoTheirOwnCells )
{
    using Internal = sw::ShadowSetupTestInternal;
    const sw::float3         lightPosition{ 1.0f, 3.0f, -2.0f };
    sw::vector<sw::GPULight> listLight{ Internal::makePointLight( lightPosition, 10.0f ) };
    const sw::vector<uint32> listShadowIndex{ 0u };
    sw::FrameShadowData      frame;
    sw::ShadowSetupUtil::populateFrame( nullptr, nullptr, false, Internal::kAtlasResolution, listShadowIndex, listLight, frame );
    SW_ASSERT_EQUAL( frame._listLocalTile.size(), static_cast<size_t>( sw::shaderslot::kPointShadowFaceCount ) );

    const sw::float3 arrOffset[] = {
        { 4.0f,  0.7f, -1.1f},
        {-4.0f,  1.2f,  0.4f},
        { 0.9f,  4.0f, -0.6f},
        {-1.3f, -4.0f,  0.2f},
        { 0.3f, -0.8f,  4.0f},
        { 1.1f,  0.5f, -4.0f},
    };
    for ( uint32 faceIndex = 0; faceIndex < sw::shaderslot::kPointShadowFaceCount; ++faceIndex )
    {
        const sw::float3 worldPoint = lightPosition + arrOffset[faceIndex];
        SW_ASSERT_EQUAL( Internal::selectPointFace( arrOffset[faceIndex] ), faceIndex );
        const sw::GPULight& matrixElement = frame._listTileElement[faceIndex * sw::shaderslot::kShadowTileElementCount];
        const sw::GPULight& info          = frame._listTileElement[faceIndex * sw::shaderslot::kShadowTileElementCount + 1];
        const sw::float3    atlasNdc      = Internal::projectToNdc( worldPoint, Internal::makeMatrixFromElement( matrixElement ) );
        const sw::float2    atlasUv       = Internal::computeUv( atlasNdc );
        const sw::string    label         = "face " + sw::to_string( faceIndex );
        SW_EXPECT_TRUE_MSG( info._positionRadius._x <= atlasUv._x && atlasUv._x <= info._positionRadius._z, ( label + " u " + sw::to_string( atlasUv._x ) ).c_str() );
        SW_EXPECT_TRUE_MSG( info._positionRadius._y <= atlasUv._y && atlasUv._y <= info._positionRadius._w, ( label + " v " + sw::to_string( atlasUv._y ) ).c_str() );
        SW_EXPECT_TRUE_MSG( 0.0f < atlasNdc._z && atlasNdc._z < 1.0f, ( label + " depth " + sw::to_string( atlasNdc._z ) ).c_str() );

        // 그리는 행렬로는 칸 NDC — 칸 원점 + 칸 uv · 칸 크기가 셰이딩 uv 와 같다.
        const sw::ShadowTileDraw& tile    = frame._listLocalTile[faceIndex];
        const sw::float3          tileNdc = Internal::projectToNdc( worldPoint, tile._viewProj );
        const sw::float2          tileUv  = Internal::computeUv( tileNdc );
        const float32             inverse = 1.0f / static_cast<float32>( Internal::kAtlasResolution );
        const float32             expectU = ( static_cast<float32>( tile._cell._x ) + tileUv._x * static_cast<float32>( tile._cell._size ) ) * inverse;
        const float32             expectV = ( static_cast<float32>( tile._cell._y ) + tileUv._y * static_cast<float32>( tile._cell._size ) ) * inverse;
        SW_EXPECT_NEAR_EQUAL( atlasUv._x, expectU, 1e-4f );
        SW_EXPECT_NEAR_EQUAL( atlasUv._y, expectV, 1e-4f );
        SW_EXPECT_NEAR_EQUAL( atlasNdc._z, tileNdc._z, 1e-5f );
    }
}

/**
 * @brief [ShadowSetupTest] 스폿의 칸은 바깥 원뿔 가장자리까지 덮는다 — 원뿔 안의 점은 칸 안에, 깊이 구간 안에 떨어진다
 */
SW_TEST_CASE( ShadowSetupTest, SpotCellCoversTheOuterCone )
{
    using Internal = sw::ShadowSetupTestInternal;
    const sw::float3         lightPosition{ 0.0f, 6.0f, 0.0f };
    constexpr float32        kOuterHalf = 0.7f;
    sw::vector<sw::GPULight> listLight{ Internal::makeSpotLight( lightPosition, 12.0f, sw::float3{ 0.0f, -1.0f, 0.0f }, kOuterHalf ) };
    const sw::vector<uint32> listShadowIndex{ 0u };
    sw::FrameShadowData      frame;
    sw::ShadowSetupUtil::populateFrame( nullptr, nullptr, false, Internal::kAtlasResolution, listShadowIndex, listLight, frame );
    SW_ASSERT_EQUAL( frame._listLocalTile.size(), static_cast<size_t>( 1 ) );
    const sw::float4x4  shading = Internal::makeMatrixFromElement( frame._listTileElement[0] );
    const sw::GPULight& info    = frame._listTileElement[1];
    // 바깥 원뿔 반각의 98% 로 기운 방향, 거리 8 m — 원뿔 가장자리 근처다.
    for ( const float32 azimuth : { 0.0f, 1.3f, 2.6f, 4.1f, 5.5f } )
    {
        const float32    tilt = kOuterHalf * 0.98f;
        const sw::float3 direction{ sw::MathUtil::sin( tilt ) * sw::MathUtil::cos( azimuth ), -sw::MathUtil::cos( tilt ), sw::MathUtil::sin( tilt ) * sw::MathUtil::sin( azimuth ) };
        const sw::float3 ndc = Internal::projectToNdc( lightPosition + direction * 8.0f, shading );
        const sw::float2 uv  = Internal::computeUv( ndc );
        SW_EXPECT_TRUE( info._positionRadius._x <= uv._x && uv._x <= info._positionRadius._z );
        SW_EXPECT_TRUE( info._positionRadius._y <= uv._y && uv._y <= info._positionRadius._w );
        SW_EXPECT_TRUE( 0.0f < ndc._z && ndc._z < 1.0f );
    }
}

/**
 * @brief [ShadowSetupTest] 원근 카메라는 깊이를 캐스케이드로 나눈다 — 끝 깊이가 늘고 텍셀이 커지며, 캐스케이드 c 의 행렬은 기준 NDC · 배율 + 이동과 같다
 * @details 셰이더는 기준 행렬 하나로 투영하고 캐스케이드마다 (배율, 이동)만 더 읽는다. 그리는 행렬(`computeCascadeViewProj`)이 같은 점을 같은 NDC 로 보내야 그림자가 맞는다.
 *          각 캐스케이드가 맡는 깊이의 바닥 점은 그 캐스케이드 볼륨 안이어야 한다.
 */
SW_TEST_CASE( ShadowSetupTest, PerspectiveCameraSplitsIntoNestedCascades )
{
    using Internal = sw::ShadowSetupTestInternal;
    sw::Scene scene{ "ShadowCascade" };
    SW_ASSERT_NOT_NULL( scene.getObjectManager() );
    sw::DirectionalLightComponent* pLight = Internal::addSun( scene, 120.0f, 4 );
    SW_ASSERT_NOT_NULL( pLight );

    // 거의 수평으로 본다 — 시선 위의 점이 120 m 까지 받는 높이 띠(-2 ~ 40 m) 안에 있다.
    const sw::float3                      eye{ 0.0f, 8.0f, 0.0f };
    const sw::float3                      target{ 0.0f, 6.0f, 40.0f };
    const sw::float4x4                    viewProj   = Internal::makePerspectiveViewProj( eye, target );
    const sw::DirectionalShadowProjection projection = pLight->computeShadowProjectionForView( viewProj, Internal::kAtlasResolution / 2u, 4 );
    SW_ASSERT_EQUAL( projection._cascadeCount, 4u );
    float32 previousSplit = 0.0f;
    float32 previousTexel = 0.0f;
    for ( uint32 cascadeIndex = 0; cascadeIndex < projection._cascadeCount; ++cascadeIndex )
    {
        const sw::DirectionalShadowCascade& cascade = projection._arrCascade[cascadeIndex];
        SW_EXPECT_TRUE( cascade._splitDepth > previousSplit );
        SW_EXPECT_TRUE( cascade._texelWorldSize > previousTexel );
        SW_EXPECT_TRUE( cascade._scale >= 1.0f - 1e-4f );
        previousSplit = cascade._splitDepth;
        previousTexel = cascade._texelWorldSize;
    }
    SW_EXPECT_NEAR_EQUAL( projection._arrCascade[3]._splitDepth, 120.0f, 0.5f );

    // 시선 한가운데의 점 — 클립 w(뷰 깊이)가 곧 거리다. 그 깊이를 맡는 캐스케이드 볼륨 안에 있고, 그리는 행렬과 (배율, 이동)이 같은 NDC 를 낸다.
    sw::float3 forward = target - eye;
    forward.normalize();
    for ( uint32 cascadeIndex = 0; cascadeIndex < projection._cascadeCount; ++cascadeIndex )
    {
        const float32    startDepth = cascadeIndex == 0 ? 0.1f : projection._arrCascade[cascadeIndex - 1]._splitDepth;
        const float32    midDepth   = ( startDepth + projection._arrCascade[cascadeIndex]._splitDepth ) * 0.5f;
        const sw::float3 ground     = eye + forward * midDepth;
        const sw::float4 value      = projection.computeCascadeShaderValue( cascadeIndex );
        const sw::float3 baseNdc    = Internal::projectToNdc( ground, projection._viewProj );
        const sw::float3 drawNdc    = Internal::projectToNdc( ground, projection.computeCascadeViewProj( cascadeIndex ) );
        SW_EXPECT_NEAR_EQUAL( drawNdc._x, baseNdc._x * value._x + value._y, 1e-3f );
        SW_EXPECT_NEAR_EQUAL( drawNdc._y, baseNdc._y * value._x + value._z, 1e-3f );
        SW_EXPECT_NEAR_EQUAL( drawNdc._z, baseNdc._z, 1e-5f );
        const sw::string label = "cascade " + sw::to_string( cascadeIndex ) + " ndc (" + sw::to_string( drawNdc._x ) + ", " + sw::to_string( drawNdc._y ) + ")";
        SW_EXPECT_TRUE_MSG( sw::MathUtil::abs( drawNdc._x ) <= 1.0f && sw::MathUtil::abs( drawNdc._y ) <= 1.0f && 0.0f <= drawNdc._z && drawNdc._z <= 1.0f, label.c_str() );
    }
}

/**
 * @brief [ShadowSetupTest] 캐스케이드가 하나면 그 캐스케이드가 기준 볼륨이다 — 배율 1 · 이동 0 이고 그리는 행렬이 기준 행렬과 같다
 * @details 직교 카메라는 캐스케이드를 요청해도 하나다(깊이가 텍셀 크기를 바꾸지 않는다).
 */
SW_TEST_CASE( ShadowSetupTest, SingleCascadeIsTheBaseVolume )
{
    using Internal = sw::ShadowSetupTestInternal;
    sw::Scene scene{ "ShadowSingle" };
    SW_ASSERT_NOT_NULL( scene.getObjectManager() );
    sw::DirectionalLightComponent* pLight = Internal::addSun( scene, 1000.0f, 4 );
    SW_ASSERT_NOT_NULL( pLight );

    const sw::DirectionalShadowProjection projection = pLight->computeShadowProjectionForView( Internal::makeOrthoRigViewProj( sw::float3{ 20.0f, 0.0f, 20.0f }, 110.0f, 0.785f ),
                                                                                               Internal::kAtlasResolution, 4 );
    SW_ASSERT_EQUAL( projection._cascadeCount, 1u );
    const sw::float4 value = projection.computeCascadeShaderValue( 0 );
    SW_EXPECT_EQUAL( value._x, 1.0f );
    SW_EXPECT_EQUAL( value._y, 0.0f );
    SW_EXPECT_EQUAL( value._z, 0.0f );
    SW_EXPECT_TRUE( value._w >= sw::DirectionalShadowCascade::kUnboundedDepth );
    const sw::float4x4 drawMatrix = projection.computeCascadeViewProj( 0 );
    const float32*     pDraw      = drawMatrix.data();
    const float32*     pBase      = projection._viewProj.data();
    for ( uint32 index = 0; index < 16; ++index )
    {
        SW_EXPECT_NEAR_EQUAL( pDraw[index], pBase[index], 1e-6f );
    }
}

/**
 * @brief [ShadowSetupTest] 볼륨 크기는 카메라를 돌려도 그대로다(경계 구) — 회전 중에 양자 경계에서 크기가 튀어 가장자리가 흔들리지 않는다
 * @details 직교 리그를 요 방향으로 돌리고(ThemePark 의 90° 회전 · 그 사이), 원근 카메라를 제자리에서 돌린다. 상자로 맞추면 빛 공간에서 본 폭이 회전마다 달라
 *          2 m 양자 경계를 넘나든다.
 */
SW_TEST_CASE( ShadowSetupTest, FittedVolumeSizeIgnoresCameraRotation )
{
    using Internal = sw::ShadowSetupTestInternal;
    sw::Scene scene{ "ShadowStable" };
    SW_ASSERT_NOT_NULL( scene.getObjectManager() );
    sw::DirectionalLightComponent* pLight = Internal::addSun( scene, 1000.0f, 1 );
    SW_ASSERT_NOT_NULL( pLight );

    const float32 referenceTexel =
        pLight->computeShadowProjectionForView( Internal::makeOrthoRigViewProj( sw::float3{ 20.0f, 0.0f, -35.0f }, 25.0f, 0.785f ), Internal::kAtlasResolution )._texelWorldSize;
    for ( const float32 yaw : { 1.1f, 2.356f, 3.0f, 3.927f, 5.498f } )
    {
        const float32 texel =
            pLight->computeShadowProjectionForView( Internal::makeOrthoRigViewProj( sw::float3{ 20.0f, 0.0f, -35.0f }, 25.0f, yaw ), Internal::kAtlasResolution )._texelWorldSize;
        SW_EXPECT_NEAR_EQUAL( texel, referenceTexel, 1e-5f );
    }

    pLight->setShadowViewDistance( 60.0f );
    pLight->setShadowCascadeCount( 3 );
    const sw::float3                      eye{ 3.0f, 6.0f, -4.0f };
    const sw::DirectionalShadowProjection reference = pLight->computeShadowProjectionForView( Internal::makePerspectiveViewProj( eye, eye + sw::float3{ 0.0f, -0.3f, 1.0f } ),
                                                                                              Internal::kAtlasResolution / 2u, 3 );
    for ( const float32 yaw : { 0.7f, 1.9f, 3.3f } )
    {
        const sw::float3                      forward{ sw::MathUtil::sin( yaw ), -0.3f, sw::MathUtil::cos( yaw ) };
        const sw::DirectionalShadowProjection turned = pLight->computeShadowProjectionForView( Internal::makePerspectiveViewProj( eye, eye + forward ), Internal::kAtlasResolution / 2u, 3 );
        SW_ASSERT_EQUAL( turned._cascadeCount, reference._cascadeCount );
        for ( uint32 cascadeIndex = 0; cascadeIndex < turned._cascadeCount; ++cascadeIndex )
        {
            SW_EXPECT_NEAR_EQUAL( turned._arrCascade[cascadeIndex]._texelWorldSize, reference._arrCascade[cascadeIndex]._texelWorldSize, 1e-4f );
        }
    }
}

/**
 * @brief [ShadowSetupTest] 추가 뷰의 그림자는 그 뷰의 카메라에 맞춘다 — 주 시점에서 먼 곳을 보는 뷰의 바닥이 그 뷰 볼륨 안이고 주 시점 볼륨 밖이다
 * @details 주 시점 볼륨을 쓰면 CCTV · 분할 화면이 다른 곳을 볼 때 그림자가 빠진다(백로그 1-3 방향광 (3)).
 */
SW_TEST_CASE( ShadowSetupTest, ExtraViewShadowFollowsItsOwnCamera )
{
    using Internal = sw::ShadowSetupTestInternal;
    sw::Scene scene{ "ShadowExtraView" };
    SW_ASSERT_NOT_NULL( scene.getObjectManager() );
    sw::DirectionalLightComponent* pLight = Internal::addSun( scene, 1000.0f, 1 );
    SW_ASSERT_NOT_NULL( pLight );

    const sw::float3   mainFocus{ 0.0f, 0.0f, 0.0f };
    const sw::float3   extraFocus{ 400.0f, 0.0f, 300.0f };
    const sw::float4x4 mainViewProj  = Internal::makeOrthoRigViewProj( mainFocus, 25.0f, 0.785f );
    const sw::float4x4 extraViewProj = Internal::makeOrthoRigViewProj( extraFocus, 25.0f, 0.785f );

    sw::vector<sw::GPULight> listLight;
    const sw::vector<uint32> listShadowIndex;
    sw::FrameShadowData      frame;
    sw::ShadowSetupUtil::populateFrame( pLight, &mainViewProj, true, Internal::kAtlasResolution, listShadowIndex, listLight, frame );
    SW_ASSERT_TRUE( frame._mainView._bValid == SW_TRUE );
    const sw::ViewShadowData extra = sw::ShadowSetupUtil::makeViewShadowFor( pLight, extraViewProj, frame );
    SW_ASSERT_TRUE( extra._bValid == SW_TRUE );

    const sw::float3 extraNdc = Internal::projectToNdc( extraFocus, extra._lightViewProj );
    SW_EXPECT_TRUE_MSG( sw::MathUtil::abs( extraNdc._x ) <= 1.0f && sw::MathUtil::abs( extraNdc._y ) <= 1.0f && 0.0f <= extraNdc._z && extraNdc._z <= 1.0f,
                        "추가 뷰의 초점이 그 뷰의 그림자 볼륨 밖이다" );
    const sw::float3 mainNdc = Internal::projectToNdc( extraFocus, frame._mainView._lightViewProj );
    SW_EXPECT_FALSE_MSG( sw::MathUtil::abs( mainNdc._x ) <= 1.0f && sw::MathUtil::abs( mainNdc._y ) <= 1.0f, "주 시점 볼륨이 이미 추가 뷰의 초점을 덮는다 — 테스트 배치가 무의미하다" );

    // 그림자 빛이 없으면 추가 뷰는 주 시점의 것을 그대로 쓴다.
    const sw::ViewShadowData fallback = sw::ShadowSetupUtil::makeViewShadowFor( nullptr, extraViewProj, frame );
    SW_EXPECT_NEAR_EQUAL( fallback._lightViewProj._11, frame._mainView._lightViewProj._11, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( fallback._lightViewProj._42, frame._mainView._lightViewProj._42, 1e-6f );
}

/**
 * @brief [ShadowSetupTest] 그림자 컬링 상자는 방향광 볼륨에 점광 · 스폿의 경계 구를 더한 것이다 — 방향광 볼륨 밖의 점광 둘레 물체가 그림자 패스에서 걸러지지 않는다
 */
SW_TEST_CASE( ShadowSetupTest, ShadowCullBoxCoversLocalLights )
{
    using Internal = sw::ShadowSetupTestInternal;
    sw::Scene scene{ "ShadowCull" };
    SW_ASSERT_NOT_NULL( scene.getObjectManager() );
    sw::DirectionalLightComponent* pLight = Internal::addSun( scene, 1000.0f, 1 );
    SW_ASSERT_NOT_NULL( pLight );

    const sw::float4x4       mainViewProj = Internal::makeOrthoRigViewProj( sw::float3{ 0.0f, 0.0f, 0.0f }, 25.0f, 0.785f );
    const sw::float3         farPoint{ 300.0f, 2.0f, -250.0f };
    sw::vector<sw::GPULight> listLight{ Internal::makePointLight( farPoint, 6.0f ) };
    const sw::vector<uint32> listShadowIndex{ 0u };
    sw::FrameShadowData      frame;
    sw::ShadowSetupUtil::populateFrame( pLight, &mainViewProj, true, Internal::kAtlasResolution, listShadowIndex, listLight, frame );
    SW_ASSERT_TRUE( frame._mainView._bValid == SW_TRUE );

    const sw::float3 shadingNdc = Internal::projectToNdc( farPoint, frame._mainView._lightViewProj );
    SW_EXPECT_FALSE( sw::MathUtil::abs( shadingNdc._x ) <= 1.0f && sw::MathUtil::abs( shadingNdc._y ) <= 1.0f );
    for ( const sw::float3& offset : {
              sw::float3{5.5f,  0.0f, 0.0f},
              sw::float3{0.0f, -5.5f, 0.0f},
              sw::float3{0.0f,  0.0f, 5.5f}
    } )
    {
        const sw::float3 cullNdc = Internal::projectToNdc( farPoint + offset, frame._mainView._cullViewProj );
        SW_EXPECT_TRUE( sw::MathUtil::abs( cullNdc._x ) <= 1.0f && sw::MathUtil::abs( cullNdc._y ) <= 1.0f && 0.0f <= cullNdc._z && cullNdc._z <= 1.0f );
    }
}
