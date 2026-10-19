#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Renderer/Scene/GPUSceneBuilder.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Serialization/Format/XMLSerializer.h"
#include "Engine/Vfx/VfxCPUSimulation.h"
#include "Engine/Vfx/VfxComponent.h"
#include "Engine/Vfx/VfxEffectDef.h"

#include "TestFramework/TestFramework.h"

// 파티클 · VFX — 데이터 형식(`*.vfx.xml`)의 읽기 · 검증, CPU 시뮬레이션의 결정성 · 풀 · 수명, 컴포넌트의 재생과 씬 스냅샷의 입자 그리기.

namespace sw
{
    namespace
    {
        constexpr float32 kTestStepSeconds = VfxCPUSimulation::kFixedStepSeconds;
        /** @brief 컴포넌트 테스트가 읽는 엔진 표본 효과입니다(방출기 둘 — 0 연기(알파), 1 불꽃(더하기), 둘 다 반복). */
        constexpr const utf8* kVfxSampleEffectPath = "engine/vfx/sparks.vfx.xml";

        /** @brief 컴포넌트를 @p seconds 동안 고정 간격 틱으로 돌립니다(한 틱이 단계 상한을 넘지 않게). */
        void tickComponentFor( VfxComponent& component, float32 seconds )
        {
            const uint32 tickCount = static_cast<uint32>( seconds / kTestStepSeconds + 0.5f );
            for ( uint32 tickIndex = 0; tickIndex < tickCount; ++tickIndex )
            {
                component.onTick( kTestStepSeconds );
            }
        }

        /** @brief 두 점(0, 1)으로 이은 직선 커브입니다. */
        FloatCurve makeLinearCurve( float32 startValue, float32 endValue )
        {
            FloatCurve curve;
            (void)curve.addKey( 0.0f, startValue, CurveInterpolation::Linear );
            (void)curve.addKey( 1.0f, endValue, CurveInterpolation::Linear );
            return curve;
        }

        /** @brief 버스트 하나입니다. */
        VfxBurstDef makeBurst( float32 time, uint32 count )
        {
            VfxBurstDef burst;
            burst._time  = time;
            burst._count = count;
            return burst;
        }

        /** @brief 검증을 통과하는 방출기입니다. 초당 60 개, 수명 0.5 ~ 1 초, 풀 256 개. */
        VfxEmitterDef makeValidEmitter( const utf8* pName )
        {
            VfxEmitterDef emitter;
            emitter._name                 = pName;
            emitter._texturePath          = "game/vfx/spark.png";
            emitter._blendMode            = VfxBlendMode::Additive;
            emitter._maxParticles         = 256;
            emitter._duration             = 2.0f;
            emitter._spawnRate            = 60.0f;
            emitter._lifetimeMin          = 0.5f;
            emitter._lifetimeMax          = 1.0f;
            emitter._velocityMin          = float3( -1.0f, 2.0f, -1.0f );
            emitter._velocityMax          = float3( 1.0f, 4.0f, 1.0f );
            emitter._gravity              = float3( 0.0f, -9.8f, 0.0f );
            emitter._sizeOverLife         = makeLinearCurve( 1.0f, 0.0f );
            emitter._colorOverLife._red   = makeLinearCurve( 1.0f, 1.0f );
            emitter._colorOverLife._green = makeLinearCurve( 0.5f, 0.0f );
            emitter._colorOverLife._blue  = makeLinearCurve( 0.0f, 0.0f );
            emitter._colorOverLife._alpha = makeLinearCurve( 1.0f, 0.0f );
            return emitter;
        }

        /** @brief 방출기 하나를 가진 효과를 디스크 XML 로 쓰고 경로를 돌려줍니다. */
        string writeEffectFile( const VfxEffectDef& effect, string_view fileName )
        {
            const string path = test::makeTempPath( fileName );
            FileUtil::ensureParentDirectoryExists( path );
            if ( XMLSerializer::saveFile( path, &effect, *VfxEffectDef::StaticType() ) == false )
                return string{};
            return path;
        }

        /** @brief 시뮬레이션을 @p stepCount 단계 돌려 렌더 입력을 돌려줍니다. */
        vector<GPUParticle> runSteps( VfxCPUSimulation& simulation, uint32 stepCount )
        {
            for ( uint32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
            {
                simulation.step( kTestStepSeconds );
            }
            vector<GPUParticle> listParticle;
            simulation.writeParticles( listParticle );
            return listParticle;
        }
    } // namespace
} // namespace sw

/**
 * @brief [VfxEffectDefTest] 효과를 XML 로 쓰고 다시 읽으면 값이 그대로다
 */
SW_TEST_CASE( VfxEffectDefTest, XMLRoundTripKeepsValues )
{
    sw::VfxEffectDef source;
    source._listEmitter.push_back( sw::makeValidEmitter( "Sparks" ) );
    source._listEmitter[0]._bLooping = true;
    source._listEmitter[0]._listBurst.push_back( sw::makeBurst( 0.5f, 12 ) );
    const sw::string path = sw::writeEffectFile( source, "round_trip.vfx.xml" );
    SW_ASSERT_FALSE( path.empty() );

    sw::VfxEffectDef loaded;
    SW_ASSERT_TRUE( loaded.loadFromDiskFile( path ) );
    SW_ASSERT_EQUAL( size_t( 1 ), loaded._listEmitter.size() );

    const sw::VfxEmitterDef& emitter = loaded._listEmitter[0];
    SW_EXPECT_STREQ( "Sparks", emitter._name.c_str() );
    SW_EXPECT_STREQ( "game/vfx/spark.png", emitter._texturePath.c_str() );
    SW_EXPECT_TRUE( emitter._blendMode == sw::VfxBlendMode::Additive );
    SW_EXPECT_TRUE( emitter._bLooping );
    SW_EXPECT_EQUAL( uint32( 256 ), emitter._maxParticles );
    SW_EXPECT_NEAR_EQUAL( 60.0f, emitter._spawnRate, 0.0001f );
    SW_EXPECT_NEAR_EQUAL( -9.8f, emitter._gravity._y, 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 4.0f, emitter._velocityMax._y, 0.0001f );
    SW_ASSERT_EQUAL( size_t( 1 ), emitter._listBurst.size() );
    SW_EXPECT_EQUAL( uint32( 12 ), emitter._listBurst[0]._count );
    SW_EXPECT_NEAR_EQUAL( 0.5f, emitter._sizeOverLife.evaluate( 0.5f ), 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 0.25f, emitter._colorOverLife._green.evaluate( 0.5f ), 0.0001f );
}

/**
 * @brief [VfxEffectDefTest] 필수 값이 빠진 효과는 로드가 실패하고 빈 효과가 남는다
 * @details 필수 칸의 기본값은 틀린 값이라 파일에서 빠지면 검증이 잡는다 — 조용히 기본 텍스처 · 기본 수명으로 돌지 않는다.
 */
SW_TEST_CASE( VfxEffectDefTest, MissingRequiredValuesFailTheLoad )
{
    const sw::VfxEmitterDef valid = sw::makeValidEmitter( "Sparks" );

    sw::VfxEmitterDef noTexture = valid;
    noTexture._texturePath.clear();
    sw::VfxEmitterDef noLifetime                             = valid;
    noLifetime._lifetimeMin                                  = 0.0f;
    noLifetime._lifetimeMax                                  = 0.0f;
    sw::VfxEmitterDef noPool                                 = valid;
    noPool._maxParticles                                     = 0;
    sw::VfxEmitterDef noSizeCurve                            = valid;
    noSizeCurve._sizeOverLife                                = sw::FloatCurve{};
    sw::VfxEmitterDef noEmission                             = valid;
    noEmission._spawnRate                                    = 0.0f;
    sw::VfxEmitterDef curveOutsideLife                       = valid;
    curveOutsideLife._colorOverLife._alpha                   = sw::makeLinearCurve( 1.0f, 0.0f );
    curveOutsideLife._colorOverLife._alpha._listKey[1]._time = 2.0f;

    const sw::VfxEmitterDef* arrBroken[] = { &noTexture, &noLifetime, &noPool, &noSizeCurve, &noEmission, &curveOutsideLife };
    for ( const sw::VfxEmitterDef* pBroken : arrBroken )
    {
        sw::VfxEffectDef effect;
        effect._listEmitter.push_back( *pBroken );
        const sw::string path = sw::writeEffectFile( effect, "broken.vfx.xml" );
        SW_ASSERT_FALSE( path.empty() );

        sw::VfxEffectDef loaded;
        loaded._listEmitter.push_back( valid );
        {
            test::ScopedLogSuppressor suppressor;
            SW_EXPECT_FALSE( loaded.loadFromDiskFile( path ) );
        }
        SW_EXPECT_TRUE( loaded._listEmitter.empty() );
    }
}

/**
 * @brief [VfxEffectDefTest] 방출기가 없거나 이름이 겹치면 검증이 실패한다
 */
SW_TEST_CASE( VfxEffectDefTest, EmptyAndDuplicateEmittersAreRejected )
{
    test::ScopedLogSuppressor suppressor;

    sw::VfxEffectDef empty;
    SW_EXPECT_FALSE( empty.validate( "empty" ) );

    sw::VfxEffectDef duplicate;
    duplicate._listEmitter.push_back( sw::makeValidEmitter( "Same" ) );
    duplicate._listEmitter.push_back( sw::makeValidEmitter( "Same" ) );
    SW_EXPECT_FALSE( duplicate.validate( "duplicate" ) );

    sw::VfxEffectDef distinct;
    distinct._listEmitter.push_back( sw::makeValidEmitter( "A" ) );
    distinct._listEmitter.push_back( sw::makeValidEmitter( "B" ) );
    SW_EXPECT_TRUE( distinct.validate( "distinct" ) );
}

/**
 * @brief [VfxEffectDefTest] 모르는 이름이 든 파일은 로드가 실패한다
 */
SW_TEST_CASE( VfxEffectDefTest, UnknownNamesFailTheLoad )
{
    const sw::string path = test::makeTempPath( "unknown.vfx.xml" );
    sw::FileUtil::ensureParentDirectoryExists( path );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( path, "<VfxEffectDef><_listEmitters/></VfxEffectDef>" ) );

    sw::VfxEffectDef          effect;
    test::ScopedLogSuppressor suppressor;
    SW_EXPECT_FALSE( effect.loadFromDiskFile( path ) );
    SW_EXPECT_TRUE( effect._listEmitter.empty() );
}

/**
 * @brief [VfxCPUSimulationTest] 같은 씨앗이면 같은 단계 수에서 입자가 같고, 다른 씨앗이면 다르다
 */
SW_TEST_CASE( VfxCPUSimulationTest, SameSeedGivesSameParticles )
{
    const sw::VfxEmitterDef emitter = sw::makeValidEmitter( "Sparks" );
    sw::VfxCPUSimulation    first( emitter, 7 );
    sw::VfxCPUSimulation    second( emitter, 7 );
    sw::VfxCPUSimulation    other( emitter, 8 );

    const sw::vector<sw::GPUParticle> listFirst  = sw::runSteps( first, 45 );
    const sw::vector<sw::GPUParticle> listSecond = sw::runSteps( second, 45 );
    const sw::vector<sw::GPUParticle> listOther  = sw::runSteps( other, 45 );

    SW_ASSERT_TRUE( listFirst.empty() == false );
    SW_ASSERT_EQUAL( listFirst.size(), listSecond.size() );
    for ( size_t index = 0; index < listFirst.size(); ++index )
    {
        SW_EXPECT_EQUAL( listFirst[index]._positionSize._x, listSecond[index]._positionSize._x );
        SW_EXPECT_EQUAL( listFirst[index]._positionSize._y, listSecond[index]._positionSize._y );
        SW_EXPECT_EQUAL( listFirst[index]._positionSize._z, listSecond[index]._positionSize._z );
        SW_EXPECT_EQUAL( listFirst[index]._rotationLife._y, listSecond[index]._rotationLife._y );
    }

    SW_ASSERT_TRUE( listOther.empty() == false );
    const bool bAnyDifferent = listOther.size() != listFirst.size() || listOther[0]._positionSize._x != listFirst[0]._positionSize._x;
    SW_EXPECT_TRUE( bAnyDifferent );
}

/**
 * @brief [VfxCPUSimulationTest] 입자는 수명이 다하면 사라지고, 방출이 끝나면 시뮬레이션이 끝난다
 */
SW_TEST_CASE( VfxCPUSimulationTest, ParticlesExpireAndTheEmitterFinishes )
{
    sw::VfxEmitterDef emitter = sw::makeValidEmitter( "Burst" );
    emitter._spawnRate        = 0.0f;
    emitter._duration         = 1.0f;
    emitter._listBurst.push_back( sw::makeBurst( 0.0f, 10 ) );
    sw::VfxCPUSimulation simulation( emitter, 1 );

    simulation.step( sw::kTestStepSeconds );
    SW_EXPECT_EQUAL( uint32( 10 ), simulation.getAliveCount() );
    SW_EXPECT_FALSE( simulation.isFinished() );

    // 가장 긴 수명 1 초 + 여유 — 버스트는 한 번뿐이라 더 늘지 않는다.
    (void)sw::runSteps( simulation, 90 );
    SW_EXPECT_EQUAL( uint32( 0 ), simulation.getAliveCount() );
    SW_EXPECT_TRUE( simulation.isFinished() );
}

/**
 * @brief [VfxCPUSimulationTest] 생성률은 방출 시간 안에서 초당 개수만큼 입자를 만든다
 */
SW_TEST_CASE( VfxCPUSimulationTest, SpawnRateEmitsPerSecond )
{
    sw::VfxEmitterDef emitter = sw::makeValidEmitter( "Stream" );
    emitter._spawnRate        = 30.0f;
    emitter._lifetimeMin      = 10.0f; // 이 테스트 동안 아무도 죽지 않는다
    emitter._lifetimeMax      = 10.0f;
    sw::VfxCPUSimulation simulation( emitter, 1 );

    (void)sw::runSteps( simulation, 60 ); // 1 초
    SW_EXPECT_NEAR_EQUAL( 30.0f, static_cast<float32>( simulation.getAliveCount() ), 1.0f );
}

/**
 * @brief [VfxCPUSimulationTest] 풀이 차면 새 입자를 만들지 않고 경고를 한 번만 남긴다
 */
SW_TEST_CASE( VfxCPUSimulationTest, FullPoolDropsNewParticlesAndWarnsOnce )
{
    sw::VfxEmitterDef emitter = sw::makeValidEmitter( "Tiny" );
    emitter._maxParticles     = 8;
    emitter._lifetimeMin      = 10.0f;
    emitter._lifetimeMax      = 10.0f;
    sw::VfxCPUSimulation simulation( emitter, 1 );

    test::ScopedLogCollector collector;
    (void)sw::runSteps( simulation, 60 );
    SW_EXPECT_EQUAL( uint32( 8 ), simulation.getAliveCount() );
    SW_EXPECT_EQUAL( uint32( 1 ), collector.countContaining( "pool of 8 particles is full" ) );
}

/**
 * @brief [VfxCPUSimulationTest] 긴 프레임은 단계 상한까지만 돌고 경고를 한 번만 남긴다
 */
SW_TEST_CASE( VfxCPUSimulationTest, LongFrameRunsAtMostTheStepLimit )
{
    sw::VfxEmitterDef emitter = sw::makeValidEmitter( "Stream" );
    emitter._spawnRate        = 60.0f;
    emitter._lifetimeMin      = 10.0f;
    emitter._lifetimeMax      = 10.0f;
    sw::VfxCPUSimulation simulation( emitter, 1 );

    test::ScopedLogCollector collector;
    simulation.update( 1.0f ); // 60 단계 몫 — 상한 8 단계만 돈다
    simulation.update( 1.0f );
    SW_EXPECT_NEAR_EQUAL( 16.0f, static_cast<float32>( simulation.getAliveCount() ), 1.0f );
    SW_EXPECT_EQUAL( uint32( 1 ), collector.countContaining( "dropping the rest" ) );
}

/**
 * @brief [VfxCPUSimulationTest] 렌더 입력은 위치와 크기 · 수명 커브의 색 · 수명 비율을 담는다
 */
SW_TEST_CASE( VfxCPUSimulationTest, WrittenParticlesFollowTheLifeCurves )
{
    sw::VfxEmitterDef emitter = sw::makeValidEmitter( "One" );
    emitter._spawnRate        = 0.0f;
    emitter._listBurst.push_back( sw::makeBurst( 0.0f, 1 ) );
    emitter._lifetimeMin = 1.0f;
    emitter._lifetimeMax = 1.0f;
    emitter._velocityMin = sw::float3( 0.0f, 0.0f, 0.0f );
    emitter._velocityMax = sw::float3( 0.0f, 0.0f, 0.0f );
    emitter._gravity     = sw::float3( 0.0f, 0.0f, 0.0f );
    sw::VfxCPUSimulation simulation( emitter, 1 );

    const sw::vector<sw::GPUParticle> listParticle = sw::runSteps( simulation, 30 ); // 0.5 초 = 수명의 절반
    SW_ASSERT_EQUAL( size_t( 1 ), listParticle.size() );
    SW_EXPECT_NEAR_EQUAL( 0.5f, listParticle[0]._rotationLife._y, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, listParticle[0]._positionSize._w, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 0.25f, listParticle[0]._color._y, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, listParticle[0]._color._w, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, listParticle[0]._positionSize._x, 0.0001f );
}

/**
 * @brief [VfxComponentTest] 효과를 읽으면 방출기마다 시뮬레이션이 서고, 재생하면 방출기마다 입자 배열을 내며, 멈추면 비운다
 */
SW_TEST_CASE( VfxComponentTest, PlayPublishesParticlesPerEmitterAndStopClearsThem )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pObject = manager.createGameObject( sw::hashed_string( "Vfx" ) );
    sw::VfxComponent*     pVfx    = pObject->addComponent<sw::VfxComponent>();
    SW_ASSERT_NOT_NULL( pVfx );
    pVfx->setEffectPath( sw::kVfxSampleEffectPath );
    SW_ASSERT_EQUAL( uint32( 2 ), pVfx->getEmitterCount() );
    SW_EXPECT_STREQ( "Smoke", pVfx->getEmitterDef( 0 )._name.c_str() );
    SW_EXPECT_FALSE( pVfx->isPlaying() );
    SW_EXPECT_TRUE( pVfx->getEmitterParticles( 0 ) == nullptr );

    pVfx->play();
    sw::tickComponentFor( *pVfx, 0.5f );
    SW_EXPECT_TRUE( pVfx->isPlaying() );
    uint32 publishedCount = 0;
    for ( uint32 emitterIndex = 0; emitterIndex < pVfx->getEmitterCount(); ++emitterIndex )
    {
        const sw::shared_ptr<const sw::vector<sw::GPUParticle>>& pListParticle = pVfx->getEmitterParticles( emitterIndex );
        SW_ASSERT_NOT_NULL( pListParticle.get() );
        publishedCount += static_cast<uint32>( pListParticle->size() );
    }
    SW_EXPECT_TRUE( publishedCount > 0 );
    SW_EXPECT_EQUAL( pVfx->getAliveParticleCount(), publishedCount );

    // 낸 배열은 고쳐 쓰지 않는다 — 렌더 스레드가 들고 있는 사본은 다음 틱 뒤에도 그대로다.
    const sw::shared_ptr<const sw::vector<sw::GPUParticle>> pHeld     = pVfx->getEmitterParticles( 1 );
    const size_t                                            heldCount = pHeld->size();
    sw::tickComponentFor( *pVfx, 0.25f );
    SW_EXPECT_TRUE( pVfx->getEmitterParticles( 1 ) != pHeld );
    SW_EXPECT_EQUAL( heldCount, pHeld->size() );

    pVfx->stop();
    SW_EXPECT_FALSE( pVfx->isPlaying() );
    SW_EXPECT_EQUAL( uint32( 0 ), pVfx->getAliveParticleCount() );
    SW_EXPECT_TRUE( pVfx->getEmitterParticles( 0 ) == nullptr );
    SW_EXPECT_TRUE( pVfx->getEmitterParticles( 1 ) == nullptr );
}

/**
 * @brief [VfxComponentTest] 같은 씨앗은 같은 입자를 내고, 다른 씨앗은 다른 입자를 낸다
 */
SW_TEST_CASE( VfxComponentTest, SameSeedReplaysTheSameParticles )
{
    sw::GameObjectManager manager;
    sw::VfxComponent*     arrVfx[3]{};
    const uint32          arrSeed[3] = { 7, 7, 8 };
    for ( uint32 index = 0; index < 3; ++index )
    {
        arrVfx[index] = manager.createGameObject( sw::hashed_string( "Vfx" ) )->addComponent<sw::VfxComponent>();
        SW_ASSERT_NOT_NULL( arrVfx[index] );
        arrVfx[index]->setSeed( arrSeed[index] );
        arrVfx[index]->setEffectPath( sw::kVfxSampleEffectPath );
        arrVfx[index]->play();
        sw::tickComponentFor( *arrVfx[index], 0.5f );
    }
    const sw::vector<sw::GPUParticle>& listFirst = *arrVfx[0]->getEmitterParticles( 1 );
    const sw::vector<sw::GPUParticle>& listSame  = *arrVfx[1]->getEmitterParticles( 1 );
    const sw::vector<sw::GPUParticle>& listOther = *arrVfx[2]->getEmitterParticles( 1 );
    SW_ASSERT_EQUAL( listFirst.size(), listSame.size() );
    SW_EXPECT_EQUAL( 0, sw::Memory::compare( listFirst.data(), listSame.data(), listFirst.size() * sizeof( sw::GPUParticle ) ) );
    const bool bOtherDiffers = listOther.size() != listFirst.size() ||
                               sw::Memory::compare( listFirst.data(), listOther.data(), listFirst.size() * sizeof( sw::GPUParticle ) ) != 0;
    SW_EXPECT_TRUE( bOtherDiffers );
}

/**
 * @brief [VfxComponentTest] 읽지 못한 효과는 방출기가 없고 재생해도 아무것도 내지 않는다(기본 효과로 대신하지 않는다)
 */
SW_TEST_CASE( VfxComponentTest, MissingEffectHasNoEmitters )
{
    sw::GameObjectManager manager;
    sw::VfxComponent*     pVfx = manager.createGameObject( sw::hashed_string( "Vfx" ) )->addComponent<sw::VfxComponent>();
    SW_ASSERT_NOT_NULL( pVfx );
    {
        test::ScopedLogSuppressor suppressor;
        pVfx->setEffectPath( "engine/vfx/does_not_exist.vfx.xml" );
    }
    SW_EXPECT_EQUAL( uint32( 0 ), pVfx->getEmitterCount() );
    pVfx->play();
    SW_EXPECT_FALSE( pVfx->isPlaying() );
}

/**
 * @brief [VfxComponentTest] 씬 스냅샷은 켜진 효과의 방출기마다 입자 그리기 하나를 싣고, 주 시점에서 먼 효과부터 놓는다
 * @details 방출기 하나가 투명 정렬 후보 하나다(입자 단위 정렬은 없다). 꺼진 컴포넌트와 입자가 없는 방출기는 싣지 않는다.
 */
SW_TEST_CASE( VfxComponentTest, SceneSnapshotCarriesOneBatchPerEmitterFarFirst )
{
    sw::Scene scene( "VfxSnapshot" );
    SW_EXPECT_TRUE( scene.ensureDefaultCameras() );
    const float32     arrDistance[3] = { 5.0f, 20.0f, 12.0f };
    sw::VfxComponent* arrVfx[3]{};
    for ( uint32 index = 0; index < 3; ++index )
    {
        arrVfx[index] = scene.getObjectManager()->createGameObject( sw::hashed_string( "Vfx" ) )->addComponent<sw::VfxComponent>();
        SW_ASSERT_NOT_NULL( arrVfx[index] );
        arrVfx[index]->setWorldPosition( sw::float3( 0.0f, 0.0f, arrDistance[index] ) );
        arrVfx[index]->setEffectPath( sw::kVfxSampleEffectPath );
        arrVfx[index]->play();
        sw::tickComponentFor( *arrVfx[index], 0.5f );
    }
    arrVfx[2]->setActive( false );
    SW_EXPECT_TRUE( arrVfx[0]->getAliveParticleCount() > 0 );
    SW_EXPECT_TRUE( arrVfx[0]->isActive() );
    SW_EXPECT_EQUAL( size_t( 3 ), scene.getObjectManager()->getComponentRegistry().getAll<sw::VfxComponent>().size() );

    sw::GPUSceneBuilder builder;
    builder.populateFromScene( &scene, sw::float3( 0.0f, 0.0f, 0.0f ) );
    const sw::vector<sw::GPUParticleBatch>& listBatch = builder.getParticleBatches();
    SW_ASSERT_EQUAL( size_t( 4 ), listBatch.size() ); // 켜진 효과 둘 × 방출기 둘
    SW_EXPECT_NEAR_EQUAL( 20.0f, listBatch[0]._world.getTranslation()._z, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 20.0f, listBatch[1]._world.getTranslation()._z, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 5.0f, listBatch[2]._world.getTranslation()._z, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 5.0f, listBatch[3]._world.getTranslation()._z, 0.001f );
    uint32 additiveCount = 0;
    for ( const sw::GPUParticleBatch& batch : listBatch )
    {
        SW_EXPECT_TRUE( batch._pListParticle != nullptr && batch._pListParticle->empty() == false );
        SW_EXPECT_NEAR_EQUAL( 1.0f, batch._sizeScale, 0.001f );
        additiveCount += batch._bAdditive == SW_TRUE ? 1u : 0u;
    }
    SW_EXPECT_EQUAL( uint32( 2 ), additiveCount ); // 효과마다 불꽃 방출기 하나

    // 씬이 그대로인 프레임에도 입자는 다시 모은다(메시의 "바뀐 것 없음" 건너뛰기와 별개다).
    sw::tickComponentFor( *arrVfx[0], 0.1f );
    builder.populateFromScene( &scene, sw::float3( 0.0f, 0.0f, 0.0f ) );
    SW_EXPECT_TRUE( builder.getParticleBatches()[2]._pListParticle == arrVfx[0]->getEmitterParticles( 0 ) ||
                    builder.getParticleBatches()[3]._pListParticle == arrVfx[0]->getEmitterParticles( 0 ) );
}
