#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Shader/Binding/GPULight.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Physics/Collision/PhysicsQuery.h"
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

        /**
         * @brief 물리 씬 — 높이 0 의 무한한 바닥(위를 향한다) 하나입니다. 굽기가 쓰는 질의(레이캐스트)만 답하고 나머지는 닿지 않습니다.
         * @details 실제 물리 백엔드 없이 가림 · 튕김의 식을 본다. 바닥보다 아래에서 쏘거나 위로 쏜 레이는 닿지 않는다.
         */
        class FloorPhysicsScene final : public IPhysicsQueryScene3D
        {
        public:
            bool raycast( const float3& origin, const float3& direction, float32 maxDistance, const PhysicsQueryFilter& filter, CastHit& outHit ) const override
            {
                (void)filter;
                const bool bAway = origin._y <= 0.0f || direction._y >= 0.0f;
                if ( bAway )
                    return false;
                const float32 distance = origin._y / -direction._y;
                if ( distance > maxDistance )
                    return false;
                outHit           = CastHit{};
                outHit._point    = float3{ origin._x + direction._x * distance, 0.0f, origin._z + direction._z * distance };
                outHit._normal   = float3{ 0.0f, 1.0f, 0.0f };
                outHit._distance = distance;
                outHit._fraction = distance / maxDistance;
                return true;
            }
            bool   shapeCast( const ShapeDesc&, const float3&, const quaternion&, const float3&, float32, const PhysicsQueryFilter&, CastHit& ) const override { return false; }
            uint32 raycastAll( const float3&, const float3&, float32, const PhysicsQueryFilter&, vector<CastHit>& ) const override { return 0; }
            uint32 shapeCastAll( const ShapeDesc&, const float3&, const quaternion&, const float3&, float32, const PhysicsQueryFilter&, vector<CastHit>& ) const override
            {
                return 0;
            }
            uint32 overlapShape( const ShapeDesc&, const float3&, const quaternion&, const PhysicsQueryFilter&, vector<PhysicsBodyHandle>& ) const override { return 0; }
        };

        /** @brief 바닥 위 1 m 의 프로브 2 × 1 × 2, 빨간 방향광(위에서 아래로), 레이 512 개, 반사율 0.5 입니다. 하늘은 부르는 쪽이 정한다. */
        struct FloorBakeFixture
        {
            FloorPhysicsScene    _physics;
            LightProbeBakeInput  _input{};
            LightProbeGridLayout _layout{};

            FloorBakeFixture()
            {
                _input._pPhysics      = &_physics;
                GPULight light        = LightProbeBakerTestInternal::makeDirectionalLight( float3{ 0.0f, -1.0f, 0.0f }, 2.0f );
                light._colorIntensity = float4{ 1.0f, 0.0f, 0.0f, 2.0f }; // 바닥 튕김은 빨강만 — 하늘(파랑)과 채널로 가른다
                _input._listLight.push_back( light );
                _input._sampleCount  = 512;
                _input._bounceAlbedo = 0.5f;
                _layout._origin      = float3{ 0.0f, 1.0f, 0.0f };
                _layout._spacing     = float3{ 1.0f, 1.0f, 1.0f };
                _layout._countX      = 2;
                _layout._countY      = 1;
                _layout._countZ      = 2;
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

/**
 * @brief [LightProbeBakerTest] 물리 씬이 있는 굽기 — 위쪽은 하늘(스카이 그러데이션), 아래쪽은 바닥이 튕긴 직접광이다. 스카이가 없으면 하늘 채널은 정확히 0 이다
 * @details 하늘은 파랑, 라이트는 빨강이라 두 항이 채널로 갈린다. 반구 계단을 SH9 로 투영하면 극에서 1.25 · 반대 극에서 −0.25 쯤이다(몬테카를로 잡음 포함).
 */
SW_TEST_CASE( LightProbeBakerTest, BakesSkyAboveAndFloorBounceBelow )
{
    sw::FloorBakeFixture fixture;
    fixture._input._sky._zenithColor  = sw::float3{ 0.0f, 0.0f, 1.0f };
    fixture._input._sky._horizonColor = sw::float3{ 0.0f, 0.0f, 1.0f };
    fixture._input._sky._groundColor  = sw::float3{ 0.0f, 0.0f, 1.0f };
    fixture._input._sky._intensity    = 1.0f;
    sw::LightProbeGrid grid{};
    sw::string         error;
    SW_ASSERT_TRUE( sw::LightProbeBaker::bakeGrid( fixture._layout, fixture._input, grid, &error ) );
    SW_ASSERT_EQUAL( size_t( 4 ), grid._listProbe.size() );
    const sw::float3 up   = grid._listProbe[0].evaluateRadiance( sw::float3{ 0.0f, 1.0f, 0.0f } );
    const sw::float3 down = grid._listProbe[0].evaluateRadiance( sw::float3{ 0.0f, -1.0f, 0.0f } );
    SW_EXPECT_TRUE( up._z > 0.8f );   // 하늘
    SW_EXPECT_TRUE( down._z < 0.2f ); // 바닥이 가린다
    const sw::float3 floorLight = sw::LightProbeBaker::computeDirectLight( fixture._input, sw::float3{ 0.0f, 0.01f, 0.0f }, sw::float3{ 0.0f, 1.0f, 0.0f } );
    SW_EXPECT_NEAR_EQUAL( 2.0f, floorLight._x, 1e-4f );      // 바닥은 라이트를 정면으로 받는다(가려지지 않는다)
    SW_EXPECT_TRUE( down._x > 0.5f * floorLight._x * 0.8f ); // 반사율 0.5 의 튕김(극에서 1.25 배쯤)
    SW_EXPECT_TRUE( up._x < down._x );

    // 스카이가 없으면(세기 0) 하늘 채널의 계수는 모두 0 이다 — 대신 채울 기본 하늘이 없다.
    sw::FloorBakeFixture noSky;
    sw::LightProbeGrid   darkGrid{};
    SW_ASSERT_TRUE( sw::LightProbeBaker::bakeGrid( noSky._layout, noSky._input, darkGrid, &error ) );
    for ( uint32 basis = 0; basis < sw::SH9Color::kBasisCount; ++basis )
    {
        SW_EXPECT_EQUAL( 0.0f, darkGrid._listProbe[0]._arrCoefficient[basis * sw::SH9Color::kChannelCount + 2] );
    }
}

/**
 * @brief [LightProbeBakerTest] 나눠 굽기(프레임마다 프로브 하나)는 한 번에 구운 것과 비트까지 같다 — 에디터의 나눠 굽기가 결과를 바꾸지 않는다
 */
SW_TEST_CASE( LightProbeBakerTest, SteppedBakeMatchesOneShotBake )
{
    sw::FloorBakeFixture fixture;
    sw::LightProbeGrid   oneShot{};
    SW_ASSERT_TRUE( sw::LightProbeBaker::bakeGrid( fixture._layout, fixture._input, oneShot ) );

    sw::LightProbeBakeJob job;
    SW_ASSERT_TRUE( job.begin( fixture._layout, fixture._input ) );
    SW_EXPECT_EQUAL( 4u, job.getProbeCount() );
    uint32 stepCount = 0;
    while ( job.step( 1 ) == false )
    {
        ++stepCount;
        SW_ASSERT_TRUE( stepCount < 16u );
    }
    SW_EXPECT_EQUAL( 3u, stepCount );
    SW_EXPECT_TRUE( job.isDone() );
    sw::LightProbeGrid stepped{};
    SW_ASSERT_TRUE( job.takeGrid( stepped ) );
    SW_ASSERT_EQUAL( oneShot._listProbe.size(), stepped._listProbe.size() );
    for ( size_t probeIndex = 0; probeIndex < oneShot._listProbe.size(); ++probeIndex )
    {
        for ( uint32 index = 0; index < sw::SH9Color::kCoefficientCount; ++index )
        {
            SW_EXPECT_EQUAL( oneShot._listProbe[probeIndex]._arrCoefficient[index], stepped._listProbe[probeIndex]._arrCoefficient[index] );
        }
    }
    sw::LightProbeGrid again{};
    SW_EXPECT_FALSE( job.takeGrid( again ) ); // 넘긴 뒤 작업은 비어 있다
}
