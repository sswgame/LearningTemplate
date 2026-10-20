#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Shader/Binding/GPULight.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Renderer/Light/LightProbeBaker.h"

#include "TestFramework/TestFramework.h"

namespace sw
{
    namespace
    {
        /** @brief 라이트 원소 도우미입니다. */
        struct LightProbeBakerTestInternal
        {
            static GPULight makePointLight( const float3& position, float32 radius, float32 intensity )
            {
                GPULight light{};
                light._positionRadius = float4{ position._x, position._y, position._z, radius };
                light._colorIntensity = float4{ 1.0f, 1.0f, 1.0f, intensity };
                light._directionType  = float4{ 0.0f, 0.0f, 0.0f, static_cast<float32>( shaderslot::kLightTypePoint ) };
                return light;
            }

            static GPULight makeDirectionalLight( const float3& direction, float32 intensity )
            {
                GPULight light{};
                light._colorIntensity = float4{ 1.0f, 1.0f, 1.0f, intensity };
                light._directionType  = float4{ direction._x, direction._y, direction._z, static_cast<float32>( shaderslot::kLightTypeDirectional ) };
                return light;
            }
        };
    } // namespace
} // namespace sw

/**
 * @brief [LightProbeBakerTest] 점광의 감쇠는 셰이더와 같은 모양이다 — 반경 밖 0, 중심 쪽으로 (1 − d / r)²
 */
SW_TEST_CASE( LightProbeBakerTest, PointLightIncidenceMatchesTheShader )
{
    using Internal           = sw::LightProbeBakerTestInternal;
    const sw::GPULight light = Internal::makePointLight( sw::float3{ 0.0f, 4.0f, 0.0f }, 8.0f, 2.0f );
    sw::float3         toLight{};
    float32            attenuation = 0.0f;
    float32            distance    = 0.0f;
    SW_ASSERT_TRUE( sw::LightProbeBaker::computeLightIncidence( light, sw::float3{ 0.0f, 0.0f, 0.0f }, toLight, attenuation, distance ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f, toLight._y, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 4.0f, distance, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.25f, attenuation, 1e-5f ); // (1 − 4 / 8)²
    SW_ASSERT_TRUE( sw::LightProbeBaker::computeLightIncidence( light, sw::float3{ 0.0f, -5.0f, 0.0f }, toLight, attenuation, distance ) );
    SW_EXPECT_NEAR_EQUAL( 0.0f, attenuation, 1e-6f );
}

/**
 * @brief [LightProbeBakerTest] 면이 받는 직접광은 세기 × 감쇠 × max(0, n·l) 의 합이고, 등진 면은 0 이다
 */
SW_TEST_CASE( LightProbeBakerTest, DirectLightSumsFacingLights )
{
    using Internal = sw::LightProbeBakerTestInternal;
    sw::LightProbeBakeInput input{};
    input._listLight.push_back( Internal::makeDirectionalLight( sw::float3{ 0.0f, -1.0f, 0.0f }, 1.5f ) );
    input._listLight.push_back( Internal::makePointLight( sw::float3{ 0.0f, 2.0f, 0.0f }, 4.0f, 2.0f ) );
    const sw::float3 up = sw::LightProbeBaker::computeDirectLight( input, sw::float3{ 0.0f, 0.0f, 0.0f }, sw::float3{ 0.0f, 1.0f, 0.0f } );
    SW_EXPECT_NEAR_EQUAL( 1.5f + 2.0f * 0.25f, up._x, 1e-4f );
    const sw::float3 down = sw::LightProbeBaker::computeDirectLight( input, sw::float3{ 0.0f, 0.0f, 0.0f }, sw::float3{ 0.0f, -1.0f, 0.0f } );
    SW_EXPECT_NEAR_EQUAL( 0.0f, down._x, 1e-6f );
}

/**
 * @brief [LightProbeBakerTest] 물리 씬이 없으면 굽지 않는다 — 가림 없이 구운 프로브는 벽 너머의 빛을 담는다
 */
SW_TEST_CASE( LightProbeBakerTest, RefusesToBakeWithoutPhysics )
{
    sw::LightProbeBakeInput input{};
    input._sampleCount = 64;
    sw::LightProbeGridLayout layout{};
    layout._spacing = sw::float3{ 1.0f, 1.0f, 1.0f };
    layout._countX  = 2;
    layout._countY  = 2;
    layout._countZ  = 2;
    sw::LightProbeGrid grid{};
    sw::string         error;
    SW_EXPECT_FALSE( sw::LightProbeBaker::bakeGrid( layout, input, grid, &error ) );
    SW_EXPECT_TRUE( grid._listProbe.empty() );
    SW_EXPECT_FALSE( error.empty() );
}
