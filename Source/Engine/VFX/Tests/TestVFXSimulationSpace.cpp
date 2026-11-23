// VFXSimulationSpaceTest — 생성 상자(입자가 상자 안에서 고르게 태어난다)와 월드 공간(방출기가 옮겨도 입자가 월드 자리를 지킨다). 둘 다 CPU 전용이다. 디바이스 없음(nogpu).
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/VFX/VFXCPUSimulation.h"
#include "Engine/VFX/VFXEffectDef.h"
#include "Engine/VFX/VFXParticle.h"

using namespace sw;

namespace
{
    struct VFXSimulationSpaceTestUtil
    {
        static constexpr float32 kStep = 0.1f;

        static FloatCurve makeFlatCurve()
        {
            FloatCurve    curve;
            FloatCurveKey key;
            key._time  = 0.0f;
            key._value = 1.0f;
            curve._listKey.push_back( key );
            return curve;
        }

        /** @brief 움직이지 않는 입자를 상자에서 내는 방출기입니다. */
        static VFXEmitterDef makeEmitter( VFXSimulationSpace space )
        {
            VFXEmitterDef emitter;
            emitter._name                 = "Rain";
            emitter._texturePath          = "game/rain.dds";
            emitter._maxParticles         = 256;
            emitter._duration             = 10.0f;
            emitter._spawnRate            = 100.0f;
            emitter._lifetimeMin          = 5.0f;
            emitter._lifetimeMax          = 5.0f;
            emitter._spawnHalfExtents     = float3{ 2.0f, 0.5f, 3.0f };
            emitter._space                = space;
            emitter._sizeOverLife         = makeFlatCurve();
            emitter._colorOverLife._red   = makeFlatCurve();
            emitter._colorOverLife._green = makeFlatCurve();
            emitter._colorOverLife._blue  = makeFlatCurve();
            emitter._colorOverLife._alpha = makeFlatCurve();
            return emitter;
        }

        static float32 readFirstX( const VFXCPUSimulation& simulation )
        {
            vector<GPUParticle> listParticle;
            simulation.writeParticles( listParticle );
            return listParticle.empty() ? MathUtil::kMaxFloat : listParticle.front()._positionSize._x;
        }
    };
    using Util = VFXSimulationSpaceTestUtil;
} // namespace

/** @brief [VFXSimulationSpaceTest] 생성 상자의 입자는 모두 상자 안이고 한 점에 모이지 않는다 — 상자와 월드 공간은 CPU 에서만 돈다 */
SW_TEST_CASE( VFXSimulationSpaceTest, ParticlesSpawnInsideTheBox )
{
    const VFXEmitterDef emitter = Util::makeEmitter( VFXSimulationSpace::World );
    SW_EXPECT_TRUE( emitter.validate( "test" ) );
    SW_EXPECT_TRUE( emitter.computeSupportedTargets().has( VFXSimulationTarget::CPU ) );
    SW_EXPECT_FALSE( emitter.computeSupportedTargets().has( VFXSimulationTarget::GPU ) );
    VFXCPUSimulation simulation{ make_shared<const VFXEmitterDef>( emitter ), 7u, 1.0f };
    for ( uint32 step = 0; step < 10; ++step )
        simulation.step( Util::kStep, nullptr );
    vector<GPUParticle> listParticle;
    simulation.writeParticles( listParticle );
    SW_REQUIRE_FALSE( listParticle.empty() );
    float32 minimumX = MathUtil::kMaxFloat;
    float32 maximumX = -MathUtil::kMaxFloat;
    for ( const GPUParticle& particle : listParticle )
    {
        SW_EXPECT_TRUE( MathUtil::abs( particle._positionSize._x ) <= 2.0f );
        SW_EXPECT_TRUE( MathUtil::abs( particle._positionSize._y ) <= 0.5f );
        SW_EXPECT_TRUE( MathUtil::abs( particle._positionSize._z ) <= 3.0f );
        minimumX = MathUtil::min( minimumX, particle._positionSize._x );
        maximumX = MathUtil::max( maximumX, particle._positionSize._x );
    }
    SW_EXPECT_TRUE( maximumX - minimumX > 1.0f );
}

/** @brief [VFXSimulationSpaceTest] 방출기가 옮기면 월드 공간 입자는 로컬 자리가 반대로 밀리고(월드 제자리), 로컬 공간 입자는 그대로다 */
SW_TEST_CASE( VFXSimulationSpaceTest, WorldSpaceParticlesStayWhereTheyWereBorn )
{
    VFXCPUSimulation world{ make_shared<const VFXEmitterDef>( Util::makeEmitter( VFXSimulationSpace::World ) ), 7u, 1.0f };
    VFXCPUSimulation local{ make_shared<const VFXEmitterDef>( Util::makeEmitter( VFXSimulationSpace::Local ) ), 7u, 1.0f };
    for ( uint32 step = 0; step < 5; ++step )
    {
        world.step( Util::kStep, nullptr );
        local.step( Util::kStep, nullptr );
    }
    const float32 before = Util::readFirstX( world );
    SW_EXPECT_NEAR( before, Util::readFirstX( local ), 1.0e-6f );
    world.moveEmitter( float3{ 4.0f, 0.0f, 0.0f } );
    local.moveEmitter( float3{ 4.0f, 0.0f, 0.0f } );
    SW_EXPECT_NEAR( before - 4.0f, Util::readFirstX( world ), 1.0e-5f );
    SW_EXPECT_NEAR( before, Util::readFirstX( local ), 1.0e-6f );
}
