#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Config/EngineConfig.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Renderer/Lighting/ShadowCache.h"
#include "Engine/Renderer/Lighting/ShadowSetup.h"
#include "Engine/Renderer/Pipeline/RenderPassInputSignature.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Visual/Component/DirectionalLightComponent.h"

// 그림자 배치(아틀라스 칸 · 캐스케이드 · 점광 · 스폿 타일 · 뷰별 볼륨)를 GPU 없이 행렬과 칸으로 본다. 픽셀은 RenderPassGPUTest 의 그림자 케이스가 본다.

namespace sw
{
    namespace
    {
        struct ShadowSetupTestInternal
        {
            static constexpr uint32 kAtlasResolution = 2048;

            /** @brief 렌더 정책(Config/Engine/EngineConfig.json `_render` 와 같은 값) — 그림자 함수는 서비스를 찾지 않고 이 값을 인자로 받는다. */
            static RenderConfig makeRenderConfig()
            {
                RenderConfig config;
                config._psoBuildBudgetMicroseconds = 2000;
                config._shadowCascadeSplitLambda   = 0.75f;
                config._shadowLocalDepthBiasTexels = 1.5f;
                // 칸 하한을 아틀라스 4분의 1(칸 16)로 두어 칸이 모자라는 경우를 본다. 크기가 줄어드는 경우는 따로 본다.
                config._shadowLocalMinTileResolution = kAtlasResolution / 4u;
                config._shadowLocalCullSlotLimit     = 16;
                config._taaHistoryWeight             = 0.9f;
                return config;
            }

            /** @brief 행벡터 규약으로 점을 행렬에 곱하고 w 로 나눕니다(셰이더의 `mul( float4( p, 1 ), m )`). */
            static float3 projectToNdc( const float3& worldPos, const float4x4& matrix )
            {
                const float4 clip = float4::transform( float4{ worldPos._x, worldPos._y, worldPos._z, 1.0f }, matrix );
                return float3{ clip._x / clip._w, clip._y / clip._w, clip._z / clip._w };
            }

            /** @brief NDC xy 를 텍스처 uv 로(셰이더의 `ndc.xy * ( 0.5, -0.5 ) + 0.5`). */
            static float2 computeUV( const float3& ndc ) { return float2{ ndc._x * 0.5f + 0.5f, ndc._y * -0.5f + 0.5f }; }

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
                pLight->setLocalRotation( float3{ 0.356582f, -1.938715f, 0.0f } );
                pLight->setShadowExtent( 180.0f );
                pLight->setShadowDistance( 120.0f );
                pLight->setShadowViewDistance( viewDistance );
                pLight->setShadowReceiverHeightRange( -2.0f, 40.0f );
                pLight->setShadowCascadeCount( cascadeCount );
                return pLight;
            }

            /** @brief 원소 번호마다 그림자 빛(정체 = 1000 + 번호)을 만듭니다 — `collectSceneLights` 가 컴포넌트 id 를 싣는 자리. */
            static vector<LocalShadowLight> makeShadowLights( std::initializer_list<uint32> listIndex )
            {
                vector<LocalShadowLight> listShadowLight;
                for ( const uint32 lightIndex : listIndex )
                {
                    listShadowLight.push_back( LocalShadowLight{ lightIndex, 1000u + lightIndex } );
                }
                return listShadowLight;
            }

            /** @brief 정체 @p key 의 타일을 찾습니다. 없으면 nullptr 입니다. */
            static const ShadowTileDraw* findTile( const FrameShadowData& frame, uint64 key )
            {
                for ( const ShadowTileDraw& tile : frame._listLocalTile )
                {
                    if ( tile._key == key )
                        return &tile;
                }
                return nullptr;
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
 * @brief [ShadowSetupTest] 캐스케이드 하나면 방향광 아틀라스 전체가 그 캐스케이드다 — 아틀라스가 생기기 전과 같은 그림(점광 · 스폿은 다른 텍스처라 상관없다)
 */
SW_TEST_CASE( ShadowSetupTest, SingleCascadeUsesTheWholeAtlas )
{
    using Internal                     = sw::ShadowSetupTestInternal;
    const sw::ShadowAtlasLayout layout = sw::ShadowAtlasLayout::make( Internal::kAtlasResolution, 1 );
    SW_EXPECT_EQUAL( layout.getCascadeCount(), 1u );
    SW_EXPECT_NEAR( layout.getCascadeTileScale(), 1.0f, 1e-6f );
    SW_EXPECT_EQUAL( layout.getCascadeTileResolution(), Internal::kAtlasResolution );
    const sw::ShadowAtlasCell cell = layout.getCascadeCell( 0 );
    SW_EXPECT_EQUAL( cell._x, 0u );
    SW_EXPECT_EQUAL( cell._y, 0u );
    SW_EXPECT_EQUAL( cell._size, Internal::kAtlasResolution );
}

/**
 * @brief [ShadowSetupTest] 캐스케이드는 방향광 아틀라스의 사분면을 하나씩 쓰고, 점광 · 스폿 칸은 점광 · 스폿 아틀라스 안에서 서로 겹치지 않는다
 * @details 셰이더는 캐스케이드 c 의 원점을 사분면 c 로 구한다. 점광 둘 · 스폿 하나(칸 13)는 하한(아틀라스 4분의 1)에서 칸 16 안에 든다.
 */
SW_TEST_CASE( ShadowSetupTest, CascadesTakeQuadrantsAndLocalCellsDoNotOverlap )
{
    using Internal                       = sw::ShadowSetupTestInternal;
    const sw::ShadowAtlasLayout cascades = sw::ShadowAtlasLayout::make( Internal::kAtlasResolution, 4 );
    SW_EXPECT_EQUAL( cascades.getCascadeCount(), 4u );
    SW_EXPECT_NEAR( cascades.getCascadeTileScale(), 0.5f, 1e-6f );
    for ( uint32 cascadeIndex = 0; cascadeIndex < 4; ++cascadeIndex )
    {
        const sw::ShadowAtlasCell cell = cascades.getCascadeCell( cascadeIndex );
        SW_EXPECT_EQUAL( cell._size, Internal::kAtlasResolution / 2u );
        SW_EXPECT_EQUAL( cell._x, ( cascadeIndex & 1u ) * Internal::kAtlasResolution / 2u );
        SW_EXPECT_EQUAL( cell._y, ( cascadeIndex >> 1u ) * Internal::kAtlasResolution / 2u );
    }

    sw::vector<sw::GPULight> listLight;
    listLight.push_back( Internal::makePointLight( sw::float3{ 0.0f, 3.0f, 0.0f }, 8.0f ) );
    listLight.push_back( Internal::makeSpotLight( sw::float3{ 5.0f, 4.0f, 0.0f }, 10.0f, sw::float3{ 0.0f, -1.0f, 0.0f }, 0.6f ) );
    listLight.push_back( Internal::makePointLight( sw::float3{ -6.0f, 2.0f, 0.0f }, 5.0f ) );
    sw::FrameShadowData  frame;
    sw::LocalShadowAtlas atlas;
    sw::FrameShadowData::populateFrame( nullptr, nullptr, Internal::kAtlasResolution, Internal::makeRenderConfig(), Internal::makeShadowLights( { 0u, 1u, 2u } ), atlas, listLight, frame );
    SW_REQUIRE_EQUAL( frame._listLocalTile.size(), static_cast<size_t>( 13 ) );
    for ( size_t first = 0; first < frame._listLocalTile.size(); ++first )
    {
        const sw::ShadowAtlasCell& cell = frame._listLocalTile[first]._cell;
        SW_EXPECT_EQUAL( cell._size, Internal::kAtlasResolution / 4u );
        SW_EXPECT_TRUE( cell._x + cell._size <= Internal::kAtlasResolution && cell._y + cell._size <= Internal::kAtlasResolution );
        for ( size_t second = first + 1; second < frame._listLocalTile.size(); ++second )
        {
            sw::fixed_string<sw::constant::buffer::kMax128> message;
            sw::formatstring( message.data(), message.capacity(), "칸 %# 과 %# 이 겹친다", static_cast<uint64>( first ), static_cast<uint64>( second ) );
            SW_EXPECT_FALSE_MSG( Internal::overlaps( cell, frame._listLocalTile[second]._cell ), message );
        }
    }
}

/**
 * @brief [ShadowSetupTest] 카메라가 없으면 점광 · 스폿은 앞에서부터 칸을 받고, 칸이 모자란 빛은 그림자 없이 남는다 — 빛 원소의 params.x · w 와 타일 원소 수
 * @details 점광 셋 + 스폿 하나(칸 19)는 하한(아틀라스 4분의 1, 칸 16)까지 줄어도 다 들지 못한다. 앞의 점광(6) · 스폿(1) · 점광(6)이 받고 마지막 점광(6)은 못 받는다.
 *          스폿의 첫 타일 원소는 점광 여섯 면 뒤(12)다.
 */
SW_TEST_CASE( ShadowSetupTest, LocalLightsTakeCellsInOrderUntilTheAtlasIsFull )
{
    using Internal = sw::ShadowSetupTestInternal;
    sw::vector<sw::GPULight> listLight;
    listLight.push_back( sw::GPULight{} ); // 방향광 자리 — 번호 목록에 없으므로 그대로다
    listLight.push_back( Internal::makePointLight( sw::float3{ 0.0f, 3.0f, 0.0f }, 8.0f ) );
    listLight.push_back( Internal::makeSpotLight( sw::float3{ 5.0f, 4.0f, 0.0f }, 10.0f, sw::float3{ 0.0f, -1.0f, 0.0f }, 0.6f ) );
    listLight.push_back( Internal::makePointLight( sw::float3{ -6.0f, 2.0f, 0.0f }, 5.0f ) );
    listLight.push_back( Internal::makePointLight( sw::float3{ 9.0f, 2.0f, 4.0f }, 5.0f ) );
    SW_EXPECT_LOG( "ShadowSetup.LocalShadowAtlasFull" ); // 마지막 점광이 칸을 못 받는다

    sw::FrameShadowData  frame;
    sw::LocalShadowAtlas atlas;
    sw::FrameShadowData::populateFrame( nullptr, nullptr, Internal::kAtlasResolution, Internal::makeRenderConfig(), Internal::makeShadowLights( { 1u, 2u, 3u, 4u } ), atlas, listLight, frame );
    SW_EXPECT_EQUAL( frame._layout.getCascadeCount(), 1u );

    const float32 tileElements = static_cast<float32>( sw::shaderslot::kShadowTileElementCount );
    SW_EXPECT_NEAR( listLight[1]._params._x, 1.0f, 1e-6f );
    SW_EXPECT_NEAR( listLight[1]._params._w, 0.0f, 1e-6f );
    SW_EXPECT_NEAR( listLight[2]._params._x, 1.0f, 1e-6f );
    SW_EXPECT_NEAR( listLight[2]._params._w, static_cast<float32>( sw::shaderslot::kPointShadowFaceCount ) * tileElements, 1e-6f );
    SW_EXPECT_NEAR( listLight[3]._params._x, 1.0f, 1e-6f );
    SW_EXPECT_NEAR( listLight[3]._params._w, static_cast<float32>( sw::shaderslot::kPointShadowFaceCount + 1 ) * tileElements, 1e-6f );
    SW_EXPECT_NEAR( listLight[4]._params._x, 0.0f, 1e-6f ); // 칸이 모자라 그림자 없음
    SW_EXPECT_NEAR( listLight[0]._params._x, 0.0f, 1e-6f );

    SW_EXPECT_EQUAL( frame._listLocalTile.size(), static_cast<size_t>( 13 ) );
    SW_EXPECT_EQUAL( frame._listTileElement.size(), static_cast<size_t>( 13 * sw::shaderslot::kShadowTileElementCount ) );
    SW_EXPECT_EQUAL( frame._listLocalBound.size(), static_cast<size_t>( 3 ) );
    // 그림자 빛이 없으면 주 시점은 그림자 없는 뷰(makeUnshadowedView)다 — 키 라이트로 볼륨을 지어내지 않는다.
    const sw::ViewShadowData unshadowed = sw::FrameShadowData::makeUnshadowedView( frame._layout );
    SW_EXPECT_TRUE( frame._mainView._bValid == SW_TRUE );
    SW_EXPECT_EQUAL( frame._mainView._cascadeCount, unshadowed._cascadeCount );
    SW_EXPECT_NEAR( frame._mainView._arrCascade[0]._w, unshadowed._arrCascade[0]._w, 1e-6f );
    SW_EXPECT_NEAR( frame._mainView._shadowParams._x, 0.0f, 1e-6f );
}

/**
 * @brief [ShadowSetupTest] 카메라가 있으면 칸은 화면에 크게 보이는 빛부터 받는다 — 목록 앞의 먼 점광보다 뒤의 가까운 빛들이 먼저다
 * @details 먼 점광(6) · 스폿(1) · 가까운 점광 둘(6 + 6)이면 칸 19 가 하한의 칸 16 에 다 들지 못한다. 목록 순서면 마지막 점광이 빠지지만 우선순위(반경 / 카메라 깊이)로는
 *          먼 점광이 빠진다. 카메라 뒤의 빛은 0 이고, 카메라가 빛 구 안이면 값이 깊이 하한으로 막혀 유한하다.
 */
SW_TEST_CASE( ShadowSetupTest, LocalLightsTakeCellsByScreenPriority )
{
    using Internal                    = sw::ShadowSetupTestInternal;
    const sw::float4x4       viewProj = Internal::makePerspectiveViewProj( sw::float3{ 0.0f, 2.0f, 0.0f }, sw::float3{ 0.0f, 2.0f, 10.0f } );
    sw::vector<sw::GPULight> listLight;
    listLight.push_back( Internal::makePointLight( sw::float3{ 0.0f, 3.0f, 80.0f }, 8.0f ) );
    listLight.push_back( Internal::makeSpotLight( sw::float3{ 2.0f, 4.0f, 12.0f }, 10.0f, sw::float3{ 0.0f, -1.0f, 0.0f }, 0.6f ) );
    listLight.push_back( Internal::makePointLight( sw::float3{ -1.0f, 2.0f, 6.0f }, 6.0f ) );
    listLight.push_back( Internal::makePointLight( sw::float3{ 1.0f, 2.0f, 8.0f }, 6.0f ) );
    SW_EXPECT_LOG( "ShadowSetup.LocalShadowAtlasFull" ); // 먼 점광이 칸을 못 받는다

    sw::FrameShadowData  frame;
    sw::LocalShadowAtlas atlas;
    sw::FrameShadowData::populateFrame( nullptr, &viewProj, Internal::kAtlasResolution, Internal::makeRenderConfig(), Internal::makeShadowLights( { 0u, 1u, 2u, 3u } ), atlas, listLight, frame );
    SW_EXPECT_NEAR( listLight[0]._params._x, 0.0f, 1e-6f ); // 먼 점광은 칸이 모자라 그림자 없음
    SW_EXPECT_NEAR( listLight[1]._params._x, 1.0f, 1e-6f );
    SW_EXPECT_NEAR( listLight[2]._params._x, 1.0f, 1e-6f );
    SW_EXPECT_NEAR( listLight[3]._params._x, 1.0f, 1e-6f );
    // 가장 가까운 점광(6 m)이 먼저 받아 첫 타일 원소(0)를 쓰고, 스폿(12 m, 반경 10)이 그 여섯 면 뒤, 8 m 점광이 그 뒤다.
    const float32 tileElements = static_cast<float32>( sw::shaderslot::kShadowTileElementCount );
    SW_EXPECT_NEAR( listLight[2]._params._w, 0.0f, 1e-6f );
    SW_EXPECT_NEAR( listLight[1]._params._w, static_cast<float32>( sw::shaderslot::kPointShadowFaceCount ) * tileElements, 1e-6f );
    SW_EXPECT_NEAR( listLight[3]._params._w, static_cast<float32>( sw::shaderslot::kPointShadowFaceCount + 1 ) * tileElements, 1e-6f );
    SW_EXPECT_EQUAL( frame._listLocalTile.size(), static_cast<size_t>( 13 ) );

    const sw::GPULight behind = Internal::makePointLight( sw::float3{ 0.0f, 2.0f, -30.0f }, 5.0f );
    SW_EXPECT_NEAR( sw::FrameShadowData::computeLocalShadowPriority( behind, viewProj ), 0.0f, 1e-6f );
    const sw::GPULight around   = Internal::makePointLight( sw::float3{ 0.0f, 2.0f, 0.0f }, 5.0f );
    const float32      priority = sw::FrameShadowData::computeLocalShadowPriority( around, viewProj );
    SW_EXPECT_NEAR( priority, 1.0f / sw::FrameShadowData::kLocalPriorityMinDepthFraction, 1e-3f );
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
    sw::FrameShadowData      frame;
    sw::LocalShadowAtlas     atlas;
    sw::FrameShadowData::populateFrame( nullptr, nullptr, Internal::kAtlasResolution, Internal::makeRenderConfig(), Internal::makeShadowLights( { 0u } ), atlas, listLight, frame );
    SW_REQUIRE_EQUAL( frame._listLocalTile.size(), static_cast<size_t>( sw::shaderslot::kPointShadowFaceCount ) );

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
        SW_REQUIRE_EQUAL( Internal::selectPointFace( arrOffset[faceIndex] ), faceIndex );
        const sw::GPULight&                             matrixElement = frame._listTileElement[faceIndex * sw::shaderslot::kShadowTileElementCount];
        const sw::GPULight&                             info          = frame._listTileElement[faceIndex * sw::shaderslot::kShadowTileElementCount + 1];
        const sw::float3                                atlasNdc      = Internal::projectToNdc( worldPoint, Internal::makeMatrixFromElement( matrixElement ) );
        const sw::float2                                atlasUV       = Internal::computeUV( atlasNdc );
        sw::fixed_string<sw::constant::buffer::kMax128> message;
        sw::formatstring( message.data(), message.capacity(), "face %# u %#", faceIndex, atlasUV._x );
        SW_EXPECT_TRUE_MSG( info._positionRadius._x <= atlasUV._x && atlasUV._x <= info._positionRadius._z, message );
        sw::formatstring( message.data(), message.capacity(), "face %# v %#", faceIndex, atlasUV._y );
        SW_EXPECT_TRUE_MSG( info._positionRadius._y <= atlasUV._y && atlasUV._y <= info._positionRadius._w, message );
        sw::formatstring( message.data(), message.capacity(), "face %# depth %#", faceIndex, atlasNdc._z );
        SW_EXPECT_TRUE_MSG( 0.0f < atlasNdc._z && atlasNdc._z < 1.0f, message );

        // 그리는 행렬로는 칸 NDC — 칸 원점 + 칸 uv · 칸 크기가 셰이딩 uv 와 같다.
        const sw::ShadowTileDraw& tile    = frame._listLocalTile[faceIndex];
        const sw::float3          tileNdc = Internal::projectToNdc( worldPoint, tile._viewProj );
        const sw::float2          tileUV  = Internal::computeUV( tileNdc );
        const float32             inverse = 1.0f / static_cast<float32>( Internal::kAtlasResolution );
        const float32             expectU = ( static_cast<float32>( tile._cell._x ) + tileUV._x * static_cast<float32>( tile._cell._size ) ) * inverse;
        const float32             expectV = ( static_cast<float32>( tile._cell._y ) + tileUV._y * static_cast<float32>( tile._cell._size ) ) * inverse;
        SW_EXPECT_NEAR( atlasUV._x, expectU, 1e-4f );
        SW_EXPECT_NEAR( atlasUV._y, expectV, 1e-4f );
        SW_EXPECT_NEAR( atlasNdc._z, tileNdc._z, 1e-5f );
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
    sw::FrameShadowData      frame;
    sw::LocalShadowAtlas     atlas;
    sw::FrameShadowData::populateFrame( nullptr, nullptr, Internal::kAtlasResolution, Internal::makeRenderConfig(), Internal::makeShadowLights( { 0u } ), atlas, listLight, frame );
    SW_REQUIRE_EQUAL( frame._listLocalTile.size(), static_cast<size_t>( 1 ) );
    const sw::float4x4  shading = Internal::makeMatrixFromElement( frame._listTileElement[0] );
    const sw::GPULight& info    = frame._listTileElement[1];
    // 바깥 원뿔 반각의 98% 로 기운 방향, 거리 8 m — 원뿔 가장자리 근처다.
    for ( const float32 azimuth : { 0.0f, 1.3f, 2.6f, 4.1f, 5.5f } )
    {
        const float32    tilt = kOuterHalf * 0.98f;
        const sw::float3 direction{ sw::MathUtil::sin( tilt ) * sw::MathUtil::cos( azimuth ), -sw::MathUtil::cos( tilt ), sw::MathUtil::sin( tilt ) * sw::MathUtil::sin( azimuth ) };
        const sw::float3 ndc = Internal::projectToNdc( lightPosition + direction * 8.0f, shading );
        const sw::float2 uv  = Internal::computeUV( ndc );
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
    SW_REQUIRE_NOT_NULL( scene.getObjectManager() );
    sw::DirectionalLightComponent* pLight = Internal::addSun( scene, 120.0f, 4 );
    SW_REQUIRE_NOT_NULL( pLight );

    // 거의 수평으로 본다 — 시선 위의 점이 120 m 까지 받는 높이 띠(-2 ~ 40 m) 안에 있다.
    const sw::float3                      eye{ 0.0f, 8.0f, 0.0f };
    const sw::float3                      target{ 0.0f, 6.0f, 40.0f };
    const sw::float4x4                    viewProj   = Internal::makePerspectiveViewProj( eye, target );
    const sw::DirectionalShadowProjection projection = pLight->computeShadowProjectionForView( viewProj, Internal::kAtlasResolution / 2u, Internal::makeRenderConfig(), 4 );
    SW_REQUIRE_EQUAL( projection._cascadeCount, 4u );
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
    SW_EXPECT_NEAR( projection._arrCascade[3]._splitDepth, 120.0f, 0.5f );

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
        SW_EXPECT_NEAR( drawNdc._x, baseNdc._x * value._x + value._y, 1e-3f );
        SW_EXPECT_NEAR( drawNdc._y, baseNdc._y * value._x + value._z, 1e-3f );
        SW_EXPECT_NEAR( drawNdc._z, baseNdc._z, 1e-5f );
        sw::fixed_string<sw::constant::buffer::kMax128> label;
        sw::formatstring( label.data(), label.capacity(), "cascade %# ndc (%#, %#)", cascadeIndex, drawNdc._x, drawNdc._y );
        SW_EXPECT_TRUE_MSG( sw::MathUtil::abs( drawNdc._x ) <= 1.0f && sw::MathUtil::abs( drawNdc._y ) <= 1.0f && 0.0f <= drawNdc._z && drawNdc._z <= 1.0f, label );
    }
}

/**
 * @brief [ShadowSetupTest] 거리 끝 옅어짐 — 시작 깊이는 그림자 구간 끝에서 몫만큼 앞이고, 몫 0 · 끝없는 구간은 옅어지지 않는다(끝없음)
 */
SW_TEST_CASE( ShadowSetupTest, DistanceFadeStartsAtFractionOfShadowRange )
{
    using Projection = sw::DirectionalShadowProjection;
    using Cascade    = sw::DirectionalShadowCascade;
    SW_EXPECT_NEAR( Projection::computeFadeStartDepth( 0.1f, 100.1f, 0.1f ), 90.1f, 1e-3f );
    SW_EXPECT_NEAR( Projection::computeFadeStartDepth( 0.1f, 100.1f, 1.0f ), 0.1f, 1e-3f );
    SW_EXPECT_NEAR( Projection::computeFadeStartDepth( 0.1f, 100.1f, 2.0f ), 0.1f, 1e-3f ); // 몫은 1 에서 자른다
    SW_EXPECT_TRUE( Projection::computeFadeStartDepth( 0.1f, 100.1f, 0.0f ) >= Cascade::kUnboundedDepth );
    SW_EXPECT_TRUE( Projection::computeFadeStartDepth( 0.1f, Cascade::kUnboundedDepth, 0.1f ) >= Cascade::kUnboundedDepth );
    SW_EXPECT_TRUE( Projection::computeFadeStartDepth( 5.0f, 5.0f, 0.1f ) >= Cascade::kUnboundedDepth ); // 빈 구간
}

/**
 * @brief [ShadowSetupTest] 원근 카메라의 거리 끝 옅어짐은 마지막 캐스케이드 끝(그림자 거리)에서 재고 셰이더 상수 w 로 간다 — 몫 0(기본)이면 끝없음이라 그림이 그대로다
 */
SW_TEST_CASE( ShadowSetupTest, PerspectiveFadeEndsAtLastCascadeAndReachesShader )
{
    using Internal = sw::ShadowSetupTestInternal;
    sw::Scene scene{ "ShadowFade" };
    SW_REQUIRE_NOT_NULL( scene.getObjectManager() );
    sw::DirectionalLightComponent* pLight = Internal::addSun( scene, 120.0f, 2 );
    SW_REQUIRE_NOT_NULL( pLight );
    const sw::float4x4                     viewProj = Internal::makePerspectiveViewProj( sw::float3{ 0.0f, 8.0f, 0.0f }, sw::float3{ 0.0f, 6.0f, 40.0f } );
    sw::vector<sw::GPULight>               listLight;
    const sw::vector<sw::LocalShadowLight> listShadowLight;
    sw::FrameShadowData                    frame;
    sw::LocalShadowAtlas                   atlas;

    sw::FrameShadowData::populateFrame( pLight, &viewProj, Internal::kAtlasResolution, Internal::makeRenderConfig(), listShadowLight, atlas, listLight, frame );
    SW_EXPECT_TRUE( frame._mainView._cascadeLayout._w >= sw::DirectionalShadowCascade::kUnboundedDepth );

    pLight->setShadowFadeFraction( 0.25f );
    const sw::DirectionalShadowProjection projection = pLight->computeShadowProjectionForView( viewProj, Internal::kAtlasResolution / 2u, Internal::makeRenderConfig(), 2 );
    SW_REQUIRE_EQUAL( projection._cascadeCount, 2u );
    const float32 shadowEnd = projection._arrCascade[1]._splitDepth;
    SW_EXPECT_NEAR( shadowEnd, 120.0f, 0.5f );
    SW_EXPECT_NEAR( projection._fadeStartDepth, shadowEnd - ( shadowEnd - 0.1f ) * 0.25f, 0.05f );
    sw::FrameShadowData::populateFrame( pLight, &viewProj, Internal::kAtlasResolution, Internal::makeRenderConfig(), listShadowLight, atlas, listLight, frame );
    SW_EXPECT_NEAR( frame._mainView._cascadeLayout._w, projection._fadeStartDepth, 1e-3f );
}

/**
 * @brief [ShadowSetupTest] 캐스케이드가 하나면 그 캐스케이드가 기준 볼륨이다 — 배율 1 · 이동 0 이고 그리는 행렬이 기준 행렬과 같다
 * @details 직교 카메라는 캐스케이드를 요청해도 하나다(깊이가 텍셀 크기를 바꾸지 않는다).
 */
SW_TEST_CASE( ShadowSetupTest, SingleCascadeIsTheBaseVolume )
{
    using Internal = sw::ShadowSetupTestInternal;
    sw::Scene scene{ "ShadowSingle" };
    SW_REQUIRE_NOT_NULL( scene.getObjectManager() );
    sw::DirectionalLightComponent* pLight = Internal::addSun( scene, 1000.0f, 4 );
    SW_REQUIRE_NOT_NULL( pLight );

    const sw::DirectionalShadowProjection projection = pLight->computeShadowProjectionForView( Internal::makeOrthoRigViewProj( sw::float3{ 20.0f, 0.0f, 20.0f }, 110.0f, 0.785f ),
                                                                                               Internal::kAtlasResolution, Internal::makeRenderConfig(), 4 );
    SW_REQUIRE_EQUAL( projection._cascadeCount, 1u );
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
        SW_EXPECT_NEAR( pDraw[index], pBase[index], 1e-6f );
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
    SW_REQUIRE_NOT_NULL( scene.getObjectManager() );
    sw::DirectionalLightComponent* pLight = Internal::addSun( scene, 1000.0f, 1 );
    SW_REQUIRE_NOT_NULL( pLight );

    const float32 referenceTexel =
        pLight->computeShadowProjectionForView( Internal::makeOrthoRigViewProj( sw::float3{ 20.0f, 0.0f, -35.0f }, 25.0f, 0.785f ), Internal::kAtlasResolution, Internal::makeRenderConfig() )._texelWorldSize;
    for ( const float32 yaw : { 1.1f, 2.356f, 3.0f, 3.927f, 5.498f } )
    {
        const float32 texel =
            pLight->computeShadowProjectionForView( Internal::makeOrthoRigViewProj( sw::float3{ 20.0f, 0.0f, -35.0f }, 25.0f, yaw ), Internal::kAtlasResolution, Internal::makeRenderConfig() )._texelWorldSize;
        SW_EXPECT_NEAR( texel, referenceTexel, 1e-5f );
    }

    pLight->setShadowViewDistance( 60.0f );
    pLight->setShadowCascadeCount( 3 );
    const sw::float3                      eye{ 3.0f, 6.0f, -4.0f };
    const sw::DirectionalShadowProjection reference = pLight->computeShadowProjectionForView( Internal::makePerspectiveViewProj( eye, eye + sw::float3{ 0.0f, -0.3f, 1.0f } ),
                                                                                              Internal::kAtlasResolution / 2u, Internal::makeRenderConfig(), 3 );
    for ( const float32 yaw : { 0.7f, 1.9f, 3.3f } )
    {
        const sw::float3                      forward{ sw::MathUtil::sin( yaw ), -0.3f, sw::MathUtil::cos( yaw ) };
        const sw::DirectionalShadowProjection turned = pLight->computeShadowProjectionForView( Internal::makePerspectiveViewProj( eye, eye + forward ), Internal::kAtlasResolution / 2u, Internal::makeRenderConfig(), 3 );
        SW_REQUIRE_EQUAL( turned._cascadeCount, reference._cascadeCount );
        for ( uint32 cascadeIndex = 0; cascadeIndex < turned._cascadeCount; ++cascadeIndex )
        {
            SW_EXPECT_NEAR( turned._arrCascade[cascadeIndex]._texelWorldSize, reference._arrCascade[cascadeIndex]._texelWorldSize, 1e-4f );
        }
    }
}

/**
 * @brief [ShadowSetupTest] 추가 뷰의 그림자는 그 뷰의 카메라에 맞춘다 — 주 시점에서 먼 곳을 보는 뷰의 바닥이 그 뷰 볼륨 안이고 주 시점 볼륨 밖이다
 * @details 주 시점 볼륨을 쓰면 CCTV · 분할 화면이 다른 곳을 볼 때 그림자가 빠진다(옛 1-3 방향광 (3)).
 */
SW_TEST_CASE( ShadowSetupTest, ExtraViewShadowFollowsItsOwnCamera )
{
    using Internal = sw::ShadowSetupTestInternal;
    sw::Scene scene{ "ShadowExtraView" };
    SW_REQUIRE_NOT_NULL( scene.getObjectManager() );
    sw::DirectionalLightComponent* pLight = Internal::addSun( scene, 1000.0f, 1 );
    SW_REQUIRE_NOT_NULL( pLight );

    const sw::float3   mainFocus{ 0.0f, 0.0f, 0.0f };
    const sw::float3   extraFocus{ 400.0f, 0.0f, 300.0f };
    const sw::float4x4 mainViewProj  = Internal::makeOrthoRigViewProj( mainFocus, 25.0f, 0.785f );
    const sw::float4x4 extraViewProj = Internal::makeOrthoRigViewProj( extraFocus, 25.0f, 0.785f );

    sw::vector<sw::GPULight> listLight;
    sw::FrameShadowData      frame;
    sw::LocalShadowAtlas     atlas;
    sw::FrameShadowData::populateFrame( pLight, &mainViewProj, Internal::kAtlasResolution, Internal::makeRenderConfig(), sw::vector<sw::LocalShadowLight>{}, atlas, listLight, frame );
    SW_REQUIRE_TRUE( frame._mainView._bValid == SW_TRUE );
    const sw::ViewShadowData extra = sw::FrameShadowData::makeViewShadowFor( pLight, extraViewProj, Internal::makeRenderConfig(), frame );
    SW_REQUIRE_TRUE( extra._bValid == SW_TRUE );

    const sw::float3 extraNdc = Internal::projectToNdc( extraFocus, extra._lightViewProj );
    SW_EXPECT_TRUE_MSG( sw::MathUtil::abs( extraNdc._x ) <= 1.0f && sw::MathUtil::abs( extraNdc._y ) <= 1.0f && 0.0f <= extraNdc._z && extraNdc._z <= 1.0f,
                        "추가 뷰의 초점이 그 뷰의 그림자 볼륨 밖이다" );
    const sw::float3 mainNdc = Internal::projectToNdc( extraFocus, frame._mainView._lightViewProj );
    SW_EXPECT_FALSE_MSG( sw::MathUtil::abs( mainNdc._x ) <= 1.0f && sw::MathUtil::abs( mainNdc._y ) <= 1.0f, "주 시점 볼륨이 이미 추가 뷰의 초점을 덮는다 — 테스트 배치가 무의미하다" );

    // 그림자 빛이 없으면 추가 뷰는 주 시점의 것을 그대로 쓴다.
    const sw::ViewShadowData fallback = sw::FrameShadowData::makeViewShadowFor( nullptr, extraViewProj, Internal::makeRenderConfig(), frame );
    SW_EXPECT_NEAR( fallback._lightViewProj._11, frame._mainView._lightViewProj._11, 1e-6f );
    SW_EXPECT_NEAR( fallback._lightViewProj._42, frame._mainView._lightViewProj._42, 1e-6f );
}

/**
 * @brief [ShadowSetupTest] 그림자 컬링 상자는 방향광 볼륨에 점광 · 스폿의 경계 구를 더한 것이다 — 방향광 볼륨 밖의 점광 둘레 물체가 그림자 패스에서 걸러지지 않는다
 */
SW_TEST_CASE( ShadowSetupTest, ShadowCullBoxCoversLocalLights )
{
    using Internal = sw::ShadowSetupTestInternal;
    sw::Scene scene{ "ShadowCull" };
    SW_REQUIRE_NOT_NULL( scene.getObjectManager() );
    sw::DirectionalLightComponent* pLight = Internal::addSun( scene, 1000.0f, 1 );
    SW_REQUIRE_NOT_NULL( pLight );

    const sw::float4x4       mainViewProj = Internal::makeOrthoRigViewProj( sw::float3{ 0.0f, 0.0f, 0.0f }, 25.0f, 0.785f );
    const sw::float3         farPoint{ 300.0f, 2.0f, -250.0f };
    sw::vector<sw::GPULight> listLight{ Internal::makePointLight( farPoint, 6.0f ) };
    sw::FrameShadowData      frame;
    sw::LocalShadowAtlas     atlas;
    sw::FrameShadowData::populateFrame( pLight, &mainViewProj, Internal::kAtlasResolution, Internal::makeRenderConfig(), Internal::makeShadowLights( { 0u } ), atlas, listLight, frame );
    SW_REQUIRE_TRUE( frame._mainView._bValid == SW_TRUE );

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

/**
 * @brief [ShadowSetupTest] 접촉 그림자 채널은 길이를 켠 3D 빛에만 — 방향광이 먼저, 나머지는 목록 순서, 넷을 넘으면 받지 못한다
 * @details 채널을 받은 빛은 `_extra.z = 채널 + 1`, 패스 상수 칸에는 빛 원소 번호다. 2D 빛의 `_extra.z`(자유 모양 변 수)는 건드리지 않는다.
 */
SW_TEST_CASE( ShadowSetupTest, ContactShadowChannelsGoToLightsWithLengthDirectionalFirst )
{
    using Internal = sw::ShadowSetupTestInternal;
    sw::vector<sw::GPULight> listLight;
    for ( uint32 index = 0; index < 5; ++index )
    {
        sw::GPULight point = Internal::makePointLight( sw::float3{ static_cast<float32>( index ), 1.0f, 0.0f }, 4.0f );
        point._extra._y    = 0.3f;
        listLight.push_back( point );
    }
    listLight[1]._extra._y = 0.0f; // 끈 빛
    sw::GPULight sun{};
    sun._directionType = sw::float4{ 0.0f, -1.0f, 0.0f, static_cast<float32>( sw::shaderslot::kLightTypeDirectional ) };
    sun._extra._y      = 0.5f;
    listLight.push_back( sun ); // 원소 5
    sw::GPULight freeform2D{};
    freeform2D._directionType = sw::float4{ 0.0f, 0.0f, 0.0f, static_cast<float32>( sw::shaderslot::kLightTypeFreeform2D ) };
    freeform2D._extra         = sw::float4{ 0.0f, 0.4f, 3.0f, 0.0f }; // y 는 섞는 방식, z 는 변 수 — 접촉 그림자가 아니다
    listLight.push_back( freeform2D );

    sw::FrameShadowData frame;
    sw::FrameShadowData::populateContactShadows( listLight, frame );
    SW_EXPECT_TRUE( frame.hasContactShadows() );
    SW_EXPECT_NEAR( frame._contactShadowLights._x, 5.0f, 1e-6f ); // 방향광이 첫 채널
    SW_EXPECT_NEAR( frame._contactShadowLights._y, 0.0f, 1e-6f );
    SW_EXPECT_NEAR( frame._contactShadowLights._z, 2.0f, 1e-6f ); // 1 은 끈 빛이라 건너뛴다
    SW_EXPECT_NEAR( frame._contactShadowLights._w, 3.0f, 1e-6f );
    SW_EXPECT_NEAR( listLight[5]._extra._z, 1.0f, 1e-6f );
    SW_EXPECT_NEAR( listLight[0]._extra._z, 2.0f, 1e-6f );
    SW_EXPECT_NEAR( listLight[1]._extra._z, 0.0f, 1e-6f );
    SW_EXPECT_NEAR( listLight[4]._extra._z, 0.0f, 1e-6f ); // 채널이 모자란 빛
    SW_EXPECT_NEAR( listLight[6]._extra._z, 3.0f, 1e-6f ); // 2D 빛은 그대로

    // 켠 빛이 없으면 빈 채널뿐이고 패스가 돌지 않는다. 지난 프레임의 채널 번호는 지운다.
    for ( sw::GPULight& light : listLight )
    {
        light._extra._y = 0.0f;
    }
    listLight[6]._extra._y = 0.4f;
    sw::FrameShadowData::populateContactShadows( listLight, frame );
    SW_EXPECT_FALSE( frame.hasContactShadows() );
    SW_EXPECT_NEAR( listLight[0]._extra._z, 0.0f, 1e-6f );
    SW_EXPECT_NEAR( listLight[5]._extra._z, 0.0f, 1e-6f );
}

/**
 * @brief [ShadowSetupTest] 점광 · 스폿 칸은 화면 크기로 고른 크기에서 시작해 넘치면 고루 줄고, 하한에 닿으면 뒤의 빛이 칸을 못 받는다(감사 RN-4)
 * @details 원하는 크기 이상인 가장 작은 (아틀라스 반 ÷ 2ⁿ) — 300 은 하한 128 위의 512, 0 은 하한이다. 스폿 하나는 아틀라스 반, 점광 하나(6)는 4분의 1 로 준다.
 *          스폿 100 은 하한 128 에서 다 들고, 하한을 256 으로 올리면 64 개만 든다.
 */
SW_TEST_CASE( ShadowSetupTest, LocalTileSizeFollowsScreenSizeAndFitsTheAtlas )
{
    using Internal                  = sw::ShadowSetupTestInternal;
    const uint32         resolution = Internal::kAtlasResolution;
    sw::LocalShadowAtlas atlas;

    atlas.build( resolution, 128u, {
                                       sw::LocalShadowRequest{1u, 1u, resolution},
                                       sw::LocalShadowRequest{2u, 1u,       300u},
                                       sw::LocalShadowRequest{3u, 1u,         0u}
    } );
    SW_REQUIRE_TRUE( atlas.getFirstCell( 0 ) != sw::LocalShadowAtlas::kNotPlaced && atlas.getFirstCell( 1 ) != sw::LocalShadowAtlas::kNotPlaced && atlas.getFirstCell( 2 ) != sw::LocalShadowAtlas::kNotPlaced );
    SW_EXPECT_EQUAL( atlas.getCell( atlas.getFirstCell( 0 ) )._size, resolution / 2u );
    SW_EXPECT_EQUAL( atlas.getCell( atlas.getFirstCell( 1 ) )._size, 512u );
    SW_EXPECT_EQUAL( atlas.getCell( atlas.getFirstCell( 2 ) )._size, 128u );

    sw::LocalShadowAtlas pointAtlas;
    pointAtlas.build( resolution, 128u, {
                                            sw::LocalShadowRequest{ 7u, sw::shaderslot::kPointShadowFaceCount, resolution }
    } );
    SW_REQUIRE_TRUE( pointAtlas.getFirstCell( 0 ) != sw::LocalShadowAtlas::kNotPlaced );
    for ( uint32 faceIndex = 0; faceIndex < sw::shaderslot::kPointShadowFaceCount; ++faceIndex )
    {
        SW_EXPECT_EQUAL( pointAtlas.getCell( pointAtlas.getFirstCell( 0 ) + faceIndex )._size, resolution / 4u );
    }

    sw::vector<sw::LocalShadowRequest> listMany;
    for ( uint32 lightIndex = 0; lightIndex < 100; ++lightIndex )
    {
        listMany.push_back( sw::LocalShadowRequest{ 100u + lightIndex, 1u, resolution } );
    }
    sw::LocalShadowAtlas manyAtlas;
    manyAtlas.build( resolution, 128u, listMany );
    for ( uint32 requestIndex = 0; requestIndex < 100; ++requestIndex )
    {
        SW_REQUIRE_TRUE( manyAtlas.getFirstCell( requestIndex ) != sw::LocalShadowAtlas::kNotPlaced );
        SW_EXPECT_EQUAL( manyAtlas.getCell( manyAtlas.getFirstCell( requestIndex ) )._size, 128u );
    }

    sw::LocalShadowAtlas floorAtlas;
    floorAtlas.build( resolution, 256u, listMany );
    uint32 placedCount = 0;
    for ( uint32 requestIndex = 0; requestIndex < 100; ++requestIndex )
    {
        placedCount += floorAtlas.getFirstCell( requestIndex ) != sw::LocalShadowAtlas::kNotPlaced ? 1u : 0u;
    }
    SW_EXPECT_EQUAL( placedCount, 64u );
    SW_EXPECT_EQUAL( floorAtlas.getMinTileResolution(), 256u );
}

/**
 * @brief [ShadowSetupTest] 스폿 그림자를 더해도 방향광 캐스케이드 수와 칸 해상도가 그대로다(감사 RN-4 — 예전에는 한 아틀라스를 나눠 캐스케이드가 줄었다)
 */
SW_TEST_CASE( ShadowSetupTest, AddingSpotShadowKeepsCascades )
{
    using Internal = sw::ShadowSetupTestInternal;
    sw::Scene scene{ "ShadowKeepCascades" };
    SW_REQUIRE_NOT_NULL( scene.getObjectManager() );
    sw::DirectionalLightComponent* pLight = Internal::addSun( scene, 120.0f, 3 );
    SW_REQUIRE_NOT_NULL( pLight );
    const sw::float4x4 viewProj = Internal::makePerspectiveViewProj( sw::float3{ 0.0f, 8.0f, 0.0f }, sw::float3{ 0.0f, 6.0f, 40.0f } );

    sw::vector<sw::GPULight> listNoLocal;
    sw::FrameShadowData      without;
    sw::LocalShadowAtlas     atlas;
    sw::FrameShadowData::populateFrame( pLight, &viewProj, Internal::kAtlasResolution, Internal::makeRenderConfig(), sw::vector<sw::LocalShadowLight>{}, atlas, listNoLocal, without );

    sw::vector<sw::GPULight> listSpot{ Internal::makeSpotLight( sw::float3{ 2.0f, 4.0f, 12.0f }, 10.0f, sw::float3{ 0.0f, -1.0f, 0.0f }, 0.6f ) };
    sw::FrameShadowData      with;
    sw::FrameShadowData::populateFrame( pLight, &viewProj, Internal::kAtlasResolution, Internal::makeRenderConfig(), Internal::makeShadowLights( { 0u } ), atlas, listSpot, with );

    SW_EXPECT_EQUAL( with._layout.getCascadeCount(), without._layout.getCascadeCount() );
    SW_EXPECT_EQUAL( with._mainView._cascadeCount, without._mainView._cascadeCount );
    SW_EXPECT_EQUAL( with._layout.getCascadeTileResolution(), without._layout.getCascadeTileResolution() );
    SW_EXPECT_NEAR( listSpot[0]._params._x, 1.0f, 1e-6f );
    SW_REQUIRE_EQUAL( with._listLocalTile.size(), static_cast<size_t>( 1 ) );
    // 화면에 크게 보이는 스폿은 점광 · 스폿 아틀라스의 가장 큰 칸(반)을 첫 자리에서 받는다.
    SW_EXPECT_EQUAL( with._listLocalTile[0]._cell._size, Internal::kAtlasResolution / 2u );
    SW_EXPECT_EQUAL( with._listLocalTile[0]._cell._x, 0u );
    SW_EXPECT_EQUAL( with._listLocalTile[0]._cell._y, 0u );
}

/**
 * @brief [ShadowSetupTest] 점광 · 스폿마다 그림자 컬링 절두체가 하나씩이고 타일은 자기 빛의 번호를 안다(감사 RN-5 — 빛마다 컬링 슬롯)
 * @details 점광의 면 여섯은 같은 빛 번호이고 그 절두체는 경계 구를 덮는 상자다(구 안의 점은 안, 구 밖으로 멀리 떨어진 점은 밖). 스폿의 절두체는 그 타일의 뷰-투영이다.
 */
SW_TEST_CASE( ShadowSetupTest, LocalLightsCarryTheirOwnCullFrustum )
{
    using Internal = sw::ShadowSetupTestInternal;
    const sw::float3         pointPosition{ 10.0f, 3.0f, -4.0f };
    sw::vector<sw::GPULight> listLight;
    listLight.push_back( Internal::makePointLight( pointPosition, 6.0f ) );
    listLight.push_back( Internal::makeSpotLight( sw::float3{ -5.0f, 4.0f, 2.0f }, 10.0f, sw::float3{ 0.0f, -1.0f, 0.0f }, 0.6f ) );
    sw::FrameShadowData  frame;
    sw::LocalShadowAtlas atlas;
    sw::FrameShadowData::populateFrame( nullptr, nullptr, Internal::kAtlasResolution, Internal::makeRenderConfig(), Internal::makeShadowLights( { 0u, 1u } ), atlas, listLight, frame );

    SW_REQUIRE_EQUAL( frame._listLocalTile.size(), static_cast<size_t>( 7 ) );
    SW_REQUIRE_EQUAL( frame._listLocalTileLight.size(), frame._listLocalTile.size() );
    SW_REQUIRE_EQUAL( frame._listLocalCullViewProj.size(), frame._listLocalBound.size() );
    SW_REQUIRE_EQUAL( frame._listLocalCullViewProj.size(), static_cast<size_t>( 2 ) );
    for ( uint32 tileIndex = 0; tileIndex < 6; ++tileIndex )
    {
        SW_EXPECT_EQUAL( frame._listLocalTileLight[tileIndex], 0u );
    }
    SW_EXPECT_EQUAL( frame._listLocalTileLight[6], 1u );

    const sw::float4x4& pointCull = frame._listLocalCullViewProj[0];
    for ( const sw::float3& offset : {
              sw::float3{5.5f,  0.0f,  0.0f},
              sw::float3{0.0f, -5.5f,  0.0f},
              sw::float3{0.0f,  0.0f,  5.5f},
              sw::float3{0.0f,  0.0f, -5.5f}
    } )
    {
        const sw::float3 ndc = Internal::projectToNdc( pointPosition + offset, pointCull );
        SW_EXPECT_TRUE( sw::MathUtil::abs( ndc._x ) <= 1.0f && sw::MathUtil::abs( ndc._y ) <= 1.0f && 0.0f <= ndc._z && ndc._z <= 1.0f );
    }
    const sw::float3 farNdc = Internal::projectToNdc( pointPosition + sw::float3{ 20.0f, 0.0f, 0.0f }, pointCull );
    SW_EXPECT_FALSE( sw::MathUtil::abs( farNdc._x ) <= 1.0f );

    const sw::float4x4& spotCull = frame._listLocalCullViewProj[1];
    SW_EXPECT_NEAR( spotCull._11, frame._listLocalTile[6]._viewProj._11, 1e-6f );
    SW_EXPECT_NEAR( spotCull._43, frame._listLocalTile[6]._viewProj._43, 1e-6f );
}

/**
 * @brief [ShadowSetupTest] 두 빛의 우선순위가 뒤집혀도 움직이지 않은 빛은 칸이 그대로라 그림자 캐시가 Reuse 다(감사 RN-4 — 예전 캐시 키는 우선순위 정렬 뒤 순번이었다)
 * @details 스폿 둘(10 m · 30 m)을 카메라가 한쪽 끝에서 보다가 반대쪽 끝으로 옮긴다. 둘 다 가장 큰 칸이라 크기가 그대로이고, 타일 차례는 뒤집히지만
 *          할당기가 서 있던 칸을 지키고 캐시가 정체(컴포넌트 id)로 짝짓는다. 언리얼 VSM 의 빛별 캐시와 같은 결과다.
 */
SW_TEST_CASE( ShadowSetupTest, PriorityFlipKeepsStandingLightReused )
{
    using Internal = sw::ShadowSetupTestInternal;
    using Action   = sw::ShadowCacheTileAction;
    sw::vector<sw::GPULight> listLight;
    listLight.push_back( Internal::makeSpotLight( sw::float3{ 0.0f, 3.0f, 10.0f }, 10.0f, sw::float3{ 0.0f, -1.0f, 0.0f }, 0.6f ) );
    listLight.push_back( Internal::makeSpotLight( sw::float3{ 0.0f, 3.0f, 30.0f }, 10.0f, sw::float3{ 0.0f, -1.0f, 0.0f }, 0.6f ) );
    const sw::vector<sw::LocalShadowLight> listShadowLight = Internal::makeShadowLights( { 0u, 1u } );
    const sw::float4x4                     nearFirst       = Internal::makePerspectiveViewProj( sw::float3{ 0.0f, 2.0f, 0.0f }, sw::float3{ 0.0f, 2.0f, 10.0f } );
    const sw::float4x4                     farFirst        = Internal::makePerspectiveViewProj( sw::float3{ 0.0f, 2.0f, 40.0f }, sw::float3{ 0.0f, 2.0f, 30.0f } );
    const uint64                           keyNear         = sw::LocalShadowAtlas::makeTileKey( listShadowLight[0]._lightID, 0 );
    const uint64                           keyFar          = sw::LocalShadowAtlas::makeTileKey( listShadowLight[1]._lightID, 0 );

    sw::LocalShadowAtlas     atlas;
    sw::ShadowCache          cache;
    sw::FrameShadowData      before;
    sw::vector<sw::GPULight> listBefore = listLight;
    sw::FrameShadowData::populateFrame( nullptr, &nearFirst, Internal::kAtlasResolution, Internal::makeRenderConfig(), listShadowLight, atlas, listBefore, before );
    SW_REQUIRE_EQUAL( before._listLocalTile.size(), static_cast<size_t>( 2 ) );
    SW_REQUIRE_TRUE( before._listLocalTile[0]._key == keyNear );                // 가까운 스폿이 먼저다
    cache.update( before._listLocalTile, Internal::kAtlasResolution, 1, true ); // 처음 본 빛 — 다 그린다
    cache.update( before._listLocalTile, Internal::kAtlasResolution, 1, true ); // 멈췄다 — 층을 채운다
    SW_REQUIRE_TRUE( cache.getAction( 0 ) == Action::Rebuild && cache.getAction( 1 ) == Action::Rebuild );

    sw::FrameShadowData      after;
    sw::vector<sw::GPULight> listAfter = listLight;
    sw::FrameShadowData::populateFrame( nullptr, &farFirst, Internal::kAtlasResolution, Internal::makeRenderConfig(), listShadowLight, atlas, listAfter, after );
    SW_REQUIRE_EQUAL( after._listLocalTile.size(), static_cast<size_t>( 2 ) );
    SW_REQUIRE_TRUE( after._listLocalTile[0]._key == keyFar ); // 우선순위가 뒤집혔다
    SW_EXPECT_NEAR( listAfter[1]._params._w, 0.0f, 1e-6f );
    for ( const uint64 key : { keyNear, keyFar } )
    {
        const sw::ShadowTileDraw* pBefore = Internal::findTile( before, key );
        const sw::ShadowTileDraw* pAfter  = Internal::findTile( after, key );
        SW_REQUIRE_NOT_NULL( pBefore );
        SW_REQUIRE_NOT_NULL( pAfter );
        SW_EXPECT_TRUE( sw::ShadowCache::isSameTile( *pBefore, *pAfter ) );
    }
    cache.update( after._listLocalTile, Internal::kAtlasResolution, 1, true );
    SW_EXPECT_TRUE( cache.getAction( 0 ) == Action::Reuse && cache.getAction( 1 ) == Action::Reuse );
    SW_EXPECT_FALSE( cache.needsRebuild() );
}

/**
 * @brief [ShadowSetupTest] 빛이 사라지면 그 칸이 풀린다 — 다음 프레임에 새 빛이 그 자리를 받고, 남은 빛은 자리를 지켜 Reuse 다
 */
SW_TEST_CASE( ShadowSetupTest, VanishedLightReleasesItsCell )
{
    using Internal           = sw::ShadowSetupTestInternal;
    using Action             = sw::ShadowCacheTileAction;
    const sw::GPULight spotA = Internal::makeSpotLight( sw::float3{ -8.0f, 4.0f, 0.0f }, 10.0f, sw::float3{ 0.0f, -1.0f, 0.0f }, 0.6f );
    const sw::GPULight spotB = Internal::makeSpotLight( sw::float3{ 0.0f, 4.0f, 0.0f }, 10.0f, sw::float3{ 0.0f, -1.0f, 0.0f }, 0.6f );
    const sw::GPULight spotC = Internal::makeSpotLight( sw::float3{ 8.0f, 4.0f, 0.0f }, 10.0f, sw::float3{ 0.0f, -1.0f, 0.0f }, 0.6f );
    const uint64       idA   = 11u;
    const uint64       idB   = 22u;
    const uint64       idC   = 33u;

    sw::LocalShadowAtlas     atlas;
    sw::ShadowCache          cache;
    sw::FrameShadowData      first;
    sw::vector<sw::GPULight> listFirst{ spotA, spotB };
    sw::FrameShadowData::populateFrame( nullptr, nullptr, Internal::kAtlasResolution, Internal::makeRenderConfig(),
                                        sw::vector<sw::LocalShadowLight>{
                                            sw::LocalShadowLight{0u, idA},
                                            sw::LocalShadowLight{1u, idB}
    },
                                        atlas, listFirst, first );
    SW_REQUIRE_EQUAL( first._listLocalTile.size(), static_cast<size_t>( 2 ) );
    const sw::ShadowAtlasCell cellA = first._listLocalTile[0]._cell;
    const sw::ShadowAtlasCell cellB = first._listLocalTile[1]._cell;
    cache.update( first._listLocalTile, Internal::kAtlasResolution, 1, true );
    cache.update( first._listLocalTile, Internal::kAtlasResolution, 1, true );

    // A 가 사라지고 C 가 들어온다 — B 는 자리를 지키고 C 는 A 가 놓은 칸을 받는다.
    sw::FrameShadowData      second;
    sw::vector<sw::GPULight> listSecond{ spotB, spotC };
    sw::FrameShadowData::populateFrame( nullptr, nullptr, Internal::kAtlasResolution, Internal::makeRenderConfig(),
                                        sw::vector<sw::LocalShadowLight>{
                                            sw::LocalShadowLight{0u, idB},
                                            sw::LocalShadowLight{1u, idC}
    },
                                        atlas, listSecond, second );
    SW_REQUIRE_EQUAL( second._listLocalTile.size(), static_cast<size_t>( 2 ) );
    SW_EXPECT_EQUAL( second._listLocalTile[0]._cell._x, cellB._x );
    SW_EXPECT_EQUAL( second._listLocalTile[0]._cell._y, cellB._y );
    SW_EXPECT_EQUAL( second._listLocalTile[1]._cell._x, cellA._x );
    SW_EXPECT_EQUAL( second._listLocalTile[1]._cell._y, cellA._y );
    SW_EXPECT_EQUAL( second._listLocalTile[1]._key, sw::LocalShadowAtlas::makeTileKey( idC, 0 ) );

    cache.update( second._listLocalTile, Internal::kAtlasResolution, 1, true );
    SW_EXPECT_TRUE( cache.getAction( 0 ) == Action::Reuse );   // 남은 빛
    SW_EXPECT_TRUE( cache.getAction( 1 ) == Action::DrawAll ); // 새 빛 — A 의 캐시를 물려받지 않는다
}

/**
 * @brief [ShadowSetupTest] 방향광과 점광 · 스폿은 서로 다른 텍스처를 쓴다 — 캐스케이드 넷이 방향광 아틀라스를 다 써도 스폿은 점광 · 스폿 아틀라스의 같은 좌표 칸을 받는다
 * @details 두 아틀라스는 역할이 다르고(ShadowMap · LocalShadowMap — 그림자 패스가 깊이 첨부의 역할로 그릴 타일을 고른다) 에뮬 백엔드의 엔진 텍스처 칸도 따로다.
 */
SW_TEST_CASE( ShadowSetupTest, DirectionalAndLocalShadowsUseSeparateTextures )
{
    using Internal = sw::ShadowSetupTestInternal;
    sw::Scene scene{ "ShadowSeparateAtlas" };
    SW_REQUIRE_NOT_NULL( scene.getObjectManager() );
    sw::DirectionalLightComponent* pLight = Internal::addSun( scene, 120.0f, 4 );
    SW_REQUIRE_NOT_NULL( pLight );
    const sw::float4x4       viewProj = Internal::makePerspectiveViewProj( sw::float3{ 0.0f, 8.0f, 0.0f }, sw::float3{ 0.0f, 6.0f, 40.0f } );
    sw::vector<sw::GPULight> listSpot{ Internal::makeSpotLight( sw::float3{ 2.0f, 4.0f, 12.0f }, 10.0f, sw::float3{ 0.0f, -1.0f, 0.0f }, 0.6f ) };
    sw::FrameShadowData      frame;
    sw::LocalShadowAtlas     atlas;
    sw::FrameShadowData::populateFrame( pLight, &viewProj, Internal::kAtlasResolution, Internal::makeRenderConfig(), Internal::makeShadowLights( { 0u } ), atlas, listSpot, frame );

    // 점광 · 스폿이 있어도 캐스케이드는 넷 다 방향광 아틀라스의 사분면이다.
    SW_EXPECT_EQUAL( frame._layout.getCascadeCount(), 4u );
    SW_EXPECT_EQUAL( frame._mainView._cascadeCount, 4u );
    SW_REQUIRE_EQUAL( frame._listLocalTile.size(), static_cast<size_t>( 1 ) );
    const sw::ShadowAtlasCell cascadeCell = frame._mainView._arrCascadeTile[0]._cell;
    const sw::ShadowAtlasCell spotCell    = frame._listLocalTile[0]._cell;
    SW_EXPECT_TRUE( Internal::overlaps( cascadeCell, spotCell ) ); // 좌표는 겹친다 — 다른 텍스처라서 성립한다

    SW_EXPECT_TRUE( sw::resolveRenderPassInputRole( "ShadowMap", true, "" ) == sw::RenderPassInputRole::ShadowMap );
    SW_EXPECT_TRUE( sw::resolveRenderPassInputRole( "LocalShadowMap", true, "" ) == sw::RenderPassInputRole::LocalShadowMap );
    uint32 directionalOrdinal = 0;
    uint32 localOrdinal       = 0;
    SW_REQUIRE_TRUE( sw::shaderslot::emulated::tryFindOrdinal( "ShadowMap", directionalOrdinal ) );
    SW_REQUIRE_TRUE( sw::shaderslot::emulated::tryFindOrdinal( "LocalShadowMap", localOrdinal ) );
    SW_EXPECT_TRUE( directionalOrdinal != localOrdinal );
}
