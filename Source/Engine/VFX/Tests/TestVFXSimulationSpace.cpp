// VFXSimulationSpaceTest — 생성 상자(입자가 상자 안에서 고르게 태어난다)와 월드 공간(방출기가 옮겨도 입자가 월드 자리를 지킨다). CPU · GPU 방출기가 같은 뜻이다. 디바이스 없음(nogpu).
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Testing/ScopedGlobalVariableValue.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/VFX/VFXCPUSimulation.h"
#include "Engine/VFX/VFXComponent.h"
#include "Engine/VFX/VFXEffectDef.h"
#include "Engine/VFX/VFXGPUEmitter.h"
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

        /** @brief x 가 음수인 쪽의 위에만 지붕이 있는 장면입니다 — 위로 쏜 선분만 막힌다. */
        class HalfRoofScene final : public IVFXCollisionScene
        {
        public:
            bool castSegment( const float3& from, const float3& to, uint32 layerMask, VFXCollisionHit& outHit ) const override
            {
                (void)layerMask;
                (void)outHit;
                return to._y > from._y && from._x < 0.0f;
            }
        };
    };
    using Util = VFXSimulationSpaceTestUtil;
} // namespace

/** @brief [VFXSimulationSpaceTest] 생성 상자의 입자는 모두 상자 안이고 한 점에 모이지 않는다 — 두 공간 모두 CPU · GPU 에서 돈다 */
SW_TEST_CASE( VFXSimulationSpaceTest, ParticlesSpawnInsideTheBox )
{
    const VFXEmitterDef emitter = Util::makeEmitter( VFXSimulationSpace::World );
    SW_EXPECT_TRUE( emitter.validate( "test" ) );
    SW_EXPECT_TRUE( emitter.computeSupportedTargets().has( VFXSimulationTarget::CPU ) );
    SW_EXPECT_TRUE( emitter.computeSupportedTargets().has( VFXSimulationTarget::GPU ) );
    SW_EXPECT_TRUE( Util::makeEmitter( VFXSimulationSpace::Local ).computeSupportedTargets().has( VFXSimulationTarget::GPU ) );
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

/** @brief [VFXSimulationSpaceTest] 방출기가 옮기고 돌면 이미 태어난 입자는 제자리이고, 새 입자만 옮긴 자리 · 돌린 속도로 태어난다 — GPU 방출기의 요청도 같은 행렬을 싣는다 */
SW_TEST_CASE( VFXSimulationSpaceTest, WorldSpaceParticlesStayWhereTheyWereBorn )
{
    VFXEmitterDef emitter                      = Util::makeEmitter( VFXSimulationSpace::World );
    emitter._velocityMin                       = float3{ 1.0f, 0.0f, 0.0f };
    emitter._velocityMax                       = float3{ 1.0f, 0.0f, 0.0f };
    const shared_ptr<const VFXEmitterDef> pDef = make_shared<const VFXEmitterDef>( emitter );
    VFXCPUSimulation                      moved{ pDef, 7u, 1.0f };
    VFXCPUSimulation                      still{ pDef, 7u, 1.0f };
    for ( uint32 step = 0; step < 5; ++step )
    {
        moved.step( Util::kStep, nullptr );
        still.step( Util::kStep, nullptr );
    }
    const size_t bornCount = moved.getAliveCount();
    // x 축이 z 축으로 가는 회전과 x 4 이동(행벡터 규약).
    float4x4 emitterToSimulation{};
    emitterToSimulation._11 = 0.0f;
    emitterToSimulation._13 = 1.0f;
    emitterToSimulation._31 = -1.0f;
    emitterToSimulation._33 = 0.0f;
    emitterToSimulation._41 = 4.0f;
    moved.setEmitterTransform( emitterToSimulation );
    moved.step( Util::kStep, nullptr );
    still.step( Util::kStep, nullptr );
    vector<GPUParticle> listMoved;
    vector<GPUParticle> listStill;
    moved.writeParticles( listMoved );
    still.writeParticles( listStill );
    SW_REQUIRE_TRUE( listMoved.size() == listStill.size() && listMoved.size() > bornCount );
    for ( size_t index = 0; index < listMoved.size(); ++index )
    {
        const float4& movedPosition = listMoved[index]._positionSize;
        const float4& stillPosition = listStill[index]._positionSize;
        if ( index < bornCount )
        {
            SW_EXPECT_NEAR( stillPosition._x, movedPosition._x, 1.0e-6f );
            SW_EXPECT_NEAR( stillPosition._z, movedPosition._z, 1.0e-6f );
            continue;
        }
        // 로컬 (x, z) 가 (4 - z, x) 로 간다 — 한 단계 움직인 x 속도도 함께 돈다.
        SW_EXPECT_NEAR( 4.0f - stillPosition._z, movedPosition._x, 1.0e-5f );
        SW_EXPECT_NEAR( stillPosition._x, movedPosition._z, 1.0e-5f );
    }

    VFXGPUEmitter gpuEmitter{ pDef, 7u, 1.0f };
    gpuEmitter.setEmitterTransform( emitterToSimulation );
    const VFXGPUEmitterRequest request = gpuEmitter.makeRequest();
    SW_EXPECT_EQUAL( 0, Memory::compare( &emitterToSimulation, &request._emitterToSimulation, sizeof( float4x4 ) ) );
}

/** @brief [VFXSimulationSpaceTest] 하늘을 보는 방출기는 지붕 아래에서 입자를 내지 않는다 — 물리 광선 충돌이 없으면 검증이 거절한다 */
SW_TEST_CASE( VFXSimulationSpaceTest, OpenSkyEmitterIsNotBornUnderARoof )
{
    VFXEmitterDef emitter               = Util::makeEmitter( VFXSimulationSpace::World );
    emitter._collision._openSkyDistance = 30.0f;
    {
        SW_EXPECT_LOG( "VFXEffectDef.VFXEmitter" );
        SW_EXPECT_FALSE( emitter.validate( "test" ) );
    }
    emitter._collision._mode = VFXCollisionMode::Kill;
    SW_EXPECT_TRUE( emitter.validate( "test" ) );
    const Util::HalfRoofScene scene;
    VFXCPUSimulation          simulation{ make_shared<const VFXEmitterDef>( emitter ), 7u, 1.0f };
    for ( uint32 step = 0; step < 10; ++step )
        simulation.step( Util::kStep, &scene );
    vector<GPUParticle> listParticle;
    simulation.writeParticles( listParticle );
    SW_REQUIRE_FALSE( listParticle.empty() );
    for ( const GPUParticle& particle : listParticle )
        SW_EXPECT_TRUE( particle._positionSize._x >= 0.0f );
    vector<uint32> listSlot;
    simulation.writeInstanceSlots( listSlot );
    SW_EXPECT_EQUAL( listParticle.size(), listSlot.size() ); // 태어나지 않은 입자는 칸을 쓰지 않는다
}

/**
 * @brief [VFXSimulationSpaceTest] 이펙트 품질(생성 배율)을 바꾸면 다음 재생부터 따른다 — 효과를 다시 읽지 않는 풀의 효과가 옛 품질에 남지 않는다
 * @details 변이: `play` 가 배율을 견주지 않으면 배율 0 에서도 입자가 나와 진다.
 */
SW_TEST_CASE( VFXSimulationSpaceTest, ReplayFollowsTheEffectsQuality )
{
    GlobalVariableInfo* const pScale = engine::getSubsystem<GlobalVariableManager>().findVariable( "gv_effectsSpawnScale" );
    SW_REQUIRE_NOT_NULL( pScale );
    shared_ptr<VFXEffectDef> pEffect = make_shared<VFXEffectDef>();
    pEffect->_listEmitter.push_back( Util::makeEmitter( VFXSimulationSpace::Local ) );

    GameObjectManager manager;
    GameObject* const pObject = manager.createGameObject( hashed_string( "Effect" ) );
    SW_REQUIRE_NOT_NULL( pObject );
    VFXComponent* const pComponent = pObject->addComponent<VFXComponent>();
    SW_REQUIRE_NOT_NULL( pComponent );
    manager.mergePendingAdds();
    const test::ScopedGlobalVariableValue full{ *pScale, "1" };
    pComponent->setEffect( pEffect );
    manager.beginPlay();
    pComponent->play();
    manager.update( 0.1f ); // 한 프레임이 도는 걸음 한도(8) 안
    SW_EXPECT_TRUE( pComponent->getSpawnedParticleCount() > 0u );

    const test::ScopedGlobalVariableValue off{ *pScale, "0" };
    pComponent->play();
    manager.update( 0.1f ); // 한 프레임이 도는 걸음 한도(8) 안
    SW_EXPECT_EQUAL( uint64{ 0 }, pComponent->getSpawnedParticleCount() );
}
