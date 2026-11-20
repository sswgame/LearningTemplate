#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Renderer/Lighting/GPULightBuffer.h"
#include "Engine/Renderer/Lighting/LightGrid.h"

// 라이트 그리드(클러스터드 라이팅 CPU 생산자)를 GPU 없이 본다. 조회(`LightGrid::collectLights`)는 셰이더 커서(lighting.hlsli LightCursor::begin)와 같은 식이다.

namespace sw
{
    namespace
    {
        struct LightGridTestInternal
        {
            /** @brief 원근 카메라의 뷰-투영입니다(눈 · 바라보는 점 · 16:9 · 세로 60°, 근 0.1 · 원 500 m). */
            static float4x4 makePerspectiveViewProj( const float3& eye, const float3& target )
            {
                return float4x4::makeLookAt( eye, target, float3::Up ) * float4x4::makePerspectiveFieldOfView( 1.0471976f, 16.0f / 9.0f, 0.1f, 500.0f );
            }

            static GPULight makePointLight( const float3& position, float32 radius )
            {
                GPULight light{};
                light._positionRadius = float4{ position._x, position._y, position._z, radius };
                light._colorIntensity = float4{ 1.0f, 1.0f, 1.0f, 1.0f };
                light._directionType  = float4{ 0.0f, 0.0f, 0.0f, static_cast<float32>( shaderslot::kLightTypePoint ) };
                return light;
            }

            static GPULight makeDirectionalLight()
            {
                GPULight light{};
                light._colorIntensity = float4{ 1.0f, 1.0f, 1.0f, 1.0f };
                light._directionType  = float4{ 0.0f, -1.0f, 0.0f, static_cast<float32>( shaderslot::kLightTypeDirectional ) };
                return light;
            }

            static bool containsLight( const vector<uint32>& listLightIndex, uint32 lightIndex )
            {
                for ( const uint32 index : listLightIndex )
                {
                    if ( index == lightIndex )
                        return true;
                }
                return false;
            }
        };
    } // namespace
} // namespace sw

/**
 * @brief [LightGridTest] 점광 경계 구 안의 점은 그 점의 클러스터 목록에서 그 점광을 받고, 방향광은 어디서나 전역 구간으로 받는다
 * @details 보수적 범위의 계약이다 — 빠지면 그 픽셀이 라이트를 잃는다. 구 안의 점을 격자로 훑는다(카메라 앞, 화면 안팎 모두).
 */
SW_TEST_CASE( LightGridTest, PointsInsideALightSphereSeeThatLight )
{
    using Internal                    = sw::LightGridTestInternal;
    const sw::float4x4       viewProj = Internal::makePerspectiveViewProj( sw::float3{ 0.0f, 2.0f, -10.0f }, sw::float3{ 0.0f, 1.0f, 0.0f } );
    sw::vector<sw::GPULight> listLight;
    listLight.push_back( Internal::makeDirectionalLight() );
    listLight.push_back( Internal::makePointLight( sw::float3{ -3.0f, 1.0f, 4.0f }, 2.5f ) );
    listLight.push_back( Internal::makePointLight( sw::float3{ 4.0f, 0.5f, 30.0f }, 6.0f ) );
    listLight.push_back( Internal::makePointLight( sw::float3{ 0.0f, 2.0f, -9.0f }, 3.0f ) ); // 카메라를 감싼다

    sw::LightGrid grid;
    SW_REQUIRE_TRUE( grid.populate( viewProj, listLight.data(), static_cast<uint32>( listLight.size() ) ) );
    SW_EXPECT_EQUAL( grid.getGlobalLightCount(), 1u );
    // 단어 수는 목록 시작 + 담은 항목 수다(헤더의 항목 수 단어가 그 수다 — GPU 생산자도 같은 단어를 쓴다).
    const uint32 entryCount = grid.getWords()[sw::shaderslot::lightgrid::kWordEntryCount];
    SW_EXPECT_EQUAL( grid.getWords().size(), static_cast<size_t>( sw::shaderslot::lightgrid::kIndexBase + entryCount ) );

    sw::vector<uint32> listFound;
    for ( uint32 lightIndex = 1; lightIndex < static_cast<uint32>( listLight.size() ); ++lightIndex )
    {
        const sw::float4& sphere = listLight[lightIndex]._positionRadius;
        for ( int32 stepX = -3; stepX <= 3; ++stepX )
        {
            for ( int32 stepY = -3; stepY <= 3; ++stepY )
            {
                for ( int32 stepZ = -3; stepZ <= 3; ++stepZ )
                {
                    const sw::float3 offset{ static_cast<float32>( stepX ), static_cast<float32>( stepY ), static_cast<float32>( stepZ ) };
                    const sw::float3 scaled = offset * ( sphere._w * 0.3f );
                    if ( scaled.getLength() >= sphere._w )
                        continue;
                    const sw::float3 point = sw::float3{ sphere._x, sphere._y, sphere._z } + scaled;
                    grid.collectLights( point, listFound );
                    sw::fixed_string<sw::constant::buffer::kMax128> message;
                    sw::formatstring( message.data(), message.capacity(), "light %# missing at cluster %#", lightIndex, grid.computeClusterIndex( point ) );
                    SW_EXPECT_TRUE_MSG( Internal::containsLight( listFound, lightIndex ), message );
                    SW_EXPECT_TRUE( Internal::containsLight( listFound, 0u ) );
                }
            }
        }
    }
}

/**
 * @brief [LightGridTest] 라이트에서 먼 클러스터는 그 라이트를 받지 않고, 카메라 뒤 · 화면 밖 라이트는 어느 클러스터에도 없다 — 컬링이 실제로 일어난다
 */
SW_TEST_CASE( LightGridTest, FarClustersAndHiddenLightsAreCulled )
{
    using Internal                    = sw::LightGridTestInternal;
    const sw::float4x4       viewProj = Internal::makePerspectiveViewProj( sw::float3{ 0.0f, 2.0f, -10.0f }, sw::float3{ 0.0f, 1.0f, 0.0f } );
    sw::vector<sw::GPULight> listLight;
    listLight.push_back( Internal::makePointLight( sw::float3{ -3.0f, 1.0f, 4.0f }, 1.0f ) );
    listLight.push_back( Internal::makePointLight( sw::float3{ 0.0f, 2.0f, -40.0f }, 2.0f ) ); // 카메라 뒤
    listLight.push_back( Internal::makePointLight( sw::float3{ 200.0f, 2.0f, 5.0f }, 2.0f ) ); // 화면 밖
    sw::LightGrid grid;
    SW_REQUIRE_TRUE( grid.populate( viewProj, listLight.data(), static_cast<uint32>( listLight.size() ) ) );
    SW_EXPECT_EQUAL( grid.getGlobalLightCount(), 0u );

    sw::vector<uint32> listFound;
    grid.collectLights( sw::float3{ 5.0f, 1.0f, 60.0f }, listFound );
    SW_EXPECT_FALSE( Internal::containsLight( listFound, 0u ) );

    uint32 entryCount = 0;
    for ( uint32 cluster = 0; cluster < sw::LightGrid::kClusterCount; ++cluster )
    {
        entryCount += grid.getClusterLightCount( cluster );
    }
    SW_EXPECT_TRUE( entryCount > 0u );
    sw::fixed_string<sw::constant::buffer::kMax128> message;
    sw::formatstring( message.data(), message.capacity(), "작은 점광 하나가 클러스터 %# 개를 덮는다", entryCount );
    SW_EXPECT_TRUE_MSG( entryCount < sw::LightGrid::kClusterCount / 8u, message );
    const sw::vector<uint32>& listWord  = grid.getWords();
    const uint32              indexBase = listWord[sw::shaderslot::lightgrid::kWordIndexBase];
    for ( uint32 entry = 0; entry < entryCount; ++entry )
    {
        SW_EXPECT_EQUAL( listWord[indexBase + entry], 0u ); // 숨은 두 라이트(1, 2)는 목록에 없다
    }
}

/**
 * @brief [LightGridTest] 직교 카메라도 같은 형식이다(깊이는 NDC z 의 선형 조각) — 경계 구 안의 점이 그 라이트를 받는다
 */
SW_TEST_CASE( LightGridTest, OrthographicViewUsesLinearSlices )
{
    using Internal              = sw::LightGridTestInternal;
    const sw::float4x4 viewProj = sw::float4x4::makeLookAt( sw::float3{ 0.0f, 50.0f, -50.0f }, sw::float3::Zero, sw::float3::Up ) *
                                  sw::float4x4::makeOrthographic( 40.0f, 22.5f, 0.1f, 200.0f );
    sw::vector<sw::GPULight> listLight{ Internal::makePointLight( sw::float3{ 3.0f, 0.0f, 2.0f }, 2.0f ) };
    sw::LightGrid            grid;
    SW_REQUIRE_TRUE( grid.populate( viewProj, listLight.data(), 1u ) );
    SW_EXPECT_EQUAL( grid.getWords()[sw::shaderslot::lightgrid::kWordPerspective], 0u );
    sw::vector<uint32> listFound;
    for ( const sw::float3& offset : {
              sw::float3{0.0f, 0.0f,  0.0f},
              sw::float3{1.5f, 0.0f,  0.0f},
              sw::float3{0.0f, 1.5f,  0.0f},
              sw::float3{0.0f, 0.0f, -1.5f}
    } )
    {
        grid.collectLights( sw::float3{ 3.0f, 0.0f, 2.0f } + offset, listFound );
        SW_EXPECT_TRUE( Internal::containsLight( listFound, 0u ) );
    }
    grid.collectLights( sw::float3{ -15.0f, 0.0f, -8.0f }, listFound );
    SW_EXPECT_FALSE( Internal::containsLight( listFound, 0u ) );
}

namespace sw
{
    namespace
    {
        struct FrameLightCullTestInternal
        {
            static GPULight makeTyped( uint32 lightType, const float3& position, float32 radius )
            {
                GPULight light       = LightGridTestInternal::makePointLight( position, radius );
                light._directionType = float4{ 0.0f, 0.0f, 0.0f, static_cast<float32>( lightType ) };
                return light;
            }

            /** @brief 방향광 · 가까운 점광 · 카메라 뒤 점광 · 먼 점광 · 자유 모양 2D 빛(변 둘)의 목록입니다(원소 7). */
            static vector<GPULight> makeList()
            {
                vector<GPULight> listLight;
                listLight.push_back( makeTyped( shaderslot::kLightTypeDirectional, float3{}, 0.0f ) );
                listLight.push_back( LightGridTestInternal::makePointLight( float3{ 0.0f, 2.0f, 10.0f }, 5.0f ) );
                listLight.push_back( LightGridTestInternal::makePointLight( float3{ 0.0f, 2.0f, -50.0f }, 5.0f ) );
                listLight.push_back( LightGridTestInternal::makePointLight( float3{ 0.0f, 2.0f, 200.0f }, 5.0f ) );
                listLight.push_back( makeTyped( shaderslot::kLightTypeFreeform2D, float3{ 0.0f, 2.0f, 20.0f }, 5.0f ) );
                listLight.push_back( makeTyped( shaderslot::kLightTypeFreeform2DEdge, float3{}, 0.0f ) );
                listLight.push_back( makeTyped( shaderslot::kLightTypeFreeform2DEdge, float3{}, 0.0f ) );
                return listLight;
            }
        };
    } // namespace
} // namespace sw

/**
 * @brief [LightGridTest] 상한 안이면 빛 목록과 번호가 그대로다
 */
SW_TEST_CASE( LightGridTest, FrameLightCullKeepsListWithinLimit )
{
    using Internal                           = sw::FrameLightCullTestInternal;
    sw::vector<sw::GPULight>       listLight = Internal::makeList();
    sw::vector<uint32>             listMap{ 9u };
    const sw::vector<sw::float4x4> listViewProj{ sw::LightGridTestInternal::makePerspectiveViewProj( sw::float3{ 0.0f, 2.0f, 0.0f }, sw::float3{ 0.0f, 2.0f, 10.0f } ) };
    SW_EXPECT_EQUAL( sw::GPULightBuffer::cullLightsToLimit( listLight, 7u, listViewProj, listMap ), 7u );
    SW_EXPECT_EQUAL( listLight.size(), static_cast<size_t>( 7 ) );
    SW_EXPECT_TRUE( listMap.empty() );
}

/**
 * @brief [LightGridTest] 상한을 넘으면 뷰 밖의 빛을 빼고 중요도 순서로 고르며, 꼬리 원소(자유 모양 2D 빛의 변)는 머리와 함께 남거나 빠진다(감사 RN-3)
 * @details 상한 5 — 방향광(늘) · 가까운 점광 · 2D 빛(셋) 이 남고, 카메라 뒤 점광은 뷰 밖이라 빠지고, 먼 점광은 중요도가 낮아 자리가 없다.
 *          상한 4 — 2D 빛(셋)은 들어가지 않아 통째로 빠지고 먼 점광이 그 자리에 든다. 남은 원소는 원래 순서다.
 */
SW_TEST_CASE( LightGridTest, FrameLightCullDropsOutsideViewAndLeastImportant )
{
    using Internal                       = sw::FrameLightCullTestInternal;
    const uint32                   kNone = sw::constant::invalid_index::kUint32;
    const sw::vector<sw::float4x4> listViewProj{ sw::LightGridTestInternal::makePerspectiveViewProj( sw::float3{ 0.0f, 2.0f, 0.0f }, sw::float3{ 0.0f, 2.0f, 10.0f } ) };

    sw::vector<sw::GPULight> listLight = Internal::makeList();
    sw::vector<uint32>       listMap;
    SW_REQUIRE_EQUAL( sw::GPULightBuffer::cullLightsToLimit( listLight, 5u, listViewProj, listMap ), 5u );
    SW_REQUIRE_EQUAL( listMap.size(), static_cast<size_t>( 7 ) );
    SW_EXPECT_EQUAL( listMap[0], 0u );
    SW_EXPECT_EQUAL( listMap[1], 1u );
    SW_EXPECT_EQUAL( listMap[2], kNone ); // 카메라 뒤
    SW_EXPECT_EQUAL( listMap[3], kNone ); // 자리 없음
    SW_EXPECT_EQUAL( listMap[4], 2u );
    SW_EXPECT_EQUAL( listMap[5], 3u );
    SW_EXPECT_EQUAL( listMap[6], 4u );
    SW_EXPECT_NEAR( listLight[2]._directionType._w, static_cast<float32>( sw::shaderslot::kLightTypeFreeform2D ), 1e-6f );
    SW_EXPECT_NEAR( listLight[4]._directionType._w, static_cast<float32>( sw::shaderslot::kLightTypeFreeform2DEdge ), 1e-6f );

    sw::vector<sw::GPULight> listSmall = Internal::makeList();
    SW_REQUIRE_EQUAL( sw::GPULightBuffer::cullLightsToLimit( listSmall, 4u, listViewProj, listMap ), 3u );
    SW_EXPECT_EQUAL( listMap[3], 2u ); // 먼 점광이 든다
    SW_EXPECT_EQUAL( listMap[4], kNone );
    SW_EXPECT_EQUAL( listMap[5], kNone );
    SW_EXPECT_EQUAL( listMap[6], kNone );
    SW_EXPECT_NEAR( listSmall[2]._positionRadius._z, 200.0f, 1e-4f );
}

/**
 * @brief [LightGridTest] 뷰가 없으면 절두체 판정 없이 세기 × 반경 순서다 — 카메라 뒤 자리의 빛도 남을 수 있다
 */
SW_TEST_CASE( LightGridTest, FrameLightCullWithoutViewsUsesPowerTimesRadius )
{
    sw::vector<sw::GPULight> listLight;
    listLight.push_back( sw::LightGridTestInternal::makePointLight( sw::float3{ 0.0f, 0.0f, 0.0f }, 2.0f ) );
    listLight.push_back( sw::LightGridTestInternal::makePointLight( sw::float3{ 0.0f, 0.0f, -50.0f }, 8.0f ) );
    listLight.push_back( sw::LightGridTestInternal::makePointLight( sw::float3{ 0.0f, 0.0f, 9.0f }, 4.0f ) );
    sw::vector<uint32> listMap;
    SW_REQUIRE_EQUAL( sw::GPULightBuffer::cullLightsToLimit( listLight, 2u, sw::vector<sw::float4x4>{}, listMap ), 2u );
    SW_EXPECT_EQUAL( listMap[0], sw::constant::invalid_index::kUint32 );
    SW_EXPECT_EQUAL( listMap[1], 0u );
    SW_EXPECT_EQUAL( listMap[2], 1u );
}
