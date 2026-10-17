#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Renderer/Light/LightGrid.h"

#include "TestFramework/TestFramework.h"

// 라이트 그리드(클러스터드 라이팅 CPU 생산자)를 GPU 없이 본다. 조회(`LightGrid::collectLights`)는 셰이더 커서(lighting.hlsli swBeginLights)와 같은 식이다.

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
    SW_ASSERT_TRUE( grid.populate( viewProj, listLight.data(), static_cast<uint32>( listLight.size() ) ) );
    SW_EXPECT_EQUAL( grid.getGlobalLightCount(), 1u );
    SW_EXPECT_EQUAL( grid.getWords().size() % sw::shaderslot::lightgrid::kWordsPerElement, static_cast<size_t>( 0 ) );

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
                    SW_EXPECT_TRUE_MSG( Internal::containsLight( listFound, lightIndex ), ( "light " + sw::to_string( lightIndex ) + " missing at cluster " +
                                                                                            sw::to_string( grid.computeClusterIndex( point ) ) )
                                                                                              .c_str() );
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
    SW_ASSERT_TRUE( grid.populate( viewProj, listLight.data(), static_cast<uint32>( listLight.size() ) ) );
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
    SW_EXPECT_TRUE_MSG( entryCount < sw::LightGrid::kClusterCount / 8u, ( "작은 점광 하나가 클러스터 " + sw::to_string( entryCount ) + " 개를 덮는다" ).c_str() );
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
    SW_ASSERT_TRUE( grid.populate( viewProj, listLight.data(), 1u ) );
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
