#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Graphics/Shader/Binding/GPULight.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/3D/MeshInstanceBatch.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Renderer/Frame/View/RenderView.h"
#include "Engine/Renderer/Light/GPULightBuffer.h"
#include "Engine/Renderer/Scene/GPUSceneBuilder.h"
#include "Engine/Renderer/Vfx/VfxGPUSimulation.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Serialization/Format/XMLSerializer.h"
#include "Engine/Vfx/VfxCPUSimulation.h"
#include "Engine/Vfx/VfxComponent.h"
#include "Engine/Vfx/VfxEffectDef.h"
#include "Engine/Vfx/VfxEmitterClock.h"
#include "Engine/Vfx/VfxGPUEmitter.h"

#include "TestFramework/TestFramework.h"

// 파티클 · VFX — 데이터 형식(`*.vfx.xml`)의 읽기 · 검증, CPU 시뮬레이션의 결정성 · 풀 · 수명, 컴포넌트의 재생과 씬 스냅샷의 입자 그리기.

namespace sw
{
    namespace
    {
        constexpr float32 kTestStepSeconds = VfxEmitterClock::kFixedStepSeconds;
        /** @brief 컴포넌트 테스트가 읽는 엔진 표본 효과입니다(방출기 둘 — 0 연기(알파), 1 불꽃(더하기), 둘 다 반복). */
        constexpr const utf8* kVfxSampleEffectPath = "engine/vfx/sparks.vfx.xml";

        /** @brief 방출기 데이터의 공유 사본입니다(시뮬레이션이 데이터를 공유 소유한다). */
        shared_ptr<const VfxEmitterDef> shareDef( const VfxEmitterDef& def )
        {
            return make_shared<const VfxEmitterDef>( def );
        }

        /** @brief GPU 시뮬레이션 요청을 실은 입자 그리기 하나입니다(`runID` 는 재생 한 번의 정체성). */
        GPUParticleBatch makeGPUBatch( const VfxEmitterDef& def, uint64 runID )
        {
            VfxGPUEmitterRequest request;
            request._pDef  = shareDef( def );
            request._runID = runID;
            GPUParticleBatch batch;
            batch._source = request;
            return batch;
        }

        /**
         * @brief 범위 동안 `gv_vfxSimulation` 을 바꿉니다. Shipping 은 변수가 없어(GPU 고정) `isApplied` 가 false 다.
         * @details 시뮬레이션 대상은 효과를 읽을 때 정하므로 효과를 읽기 전에 만든다.
         */
        class ScopedVfxSimulationTarget
        {
        public:
            explicit ScopedVfxSimulationTarget( const utf8* pTarget )
                : _pVariable{ engine::getSubsystem<GlobalVariableManager>().findVariable( "gv_vfxSimulation" ) }
                , _previous{}
                , _bApplied{ false }
            {
                if ( _pVariable == nullptr )
                    return;
                _previous = _pVariable->getValueAsString();
                _bApplied = _pVariable->setValueAsString( pTarget );
            }

            ~ScopedVfxSimulationTarget()
            {
                if ( _bApplied )
                    (void)_pVariable->setValueAsString( _previous ); // 되돌리기 — 앞에서 같은 변수에 쓴 값이다
            }

            ScopedVfxSimulationTarget( const ScopedVfxSimulationTarget& )            = delete;
            ScopedVfxSimulationTarget& operator=( const ScopedVfxSimulationTarget& ) = delete;

            bool isApplied() const { return _bApplied; }

        private:
            GlobalVariableInfo* _pVariable;
            string              _previous;
            bool                _bApplied;
        };

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
            SW_EXPECT_TRUE( FileUtil::ensureParentDirectoryExists( path ) );
            if ( XMLSerializer::saveFile( path, &effect, *VfxEffectDef::StaticType() ) == false )
                return string{};
            return path;
        }

        /** @brief 시뮬레이션을 @p stepCount 단계 돌려 렌더 입력을 돌려줍니다. */
        vector<GPUParticle> runSteps( VfxCPUSimulation& simulation, uint32 stepCount )
        {
            for ( uint32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
            {
                simulation.step( kTestStepSeconds, nullptr );
            }
            vector<GPUParticle> listParticle;
            simulation.writeParticles( listParticle );
            return listParticle;
        }

        /** @brief 라이트 목록의 점광 수입니다. */
        uint32 countPointLights( const vector<GPULight>& listLight )
        {
            uint32 pointLightCount = 0;
            for ( const GPULight& light : listLight )
            {
                pointLightCount += static_cast<uint32>( light._directionType._w ) == shaderslot::kLightTypePoint ? 1u : 0u;
            }
            return pointLightCount;
        }

        /** @brief 방출기 로컬의 평면 y = @p height(위를 보는 면) 하나뿐인 충돌 씬입니다. */
        class PlaneCollisionScene final : public IVfxCollisionScene
        {
        public:
            explicit PlaneCollisionScene( float32 height )
                : _height{ height }
            {
            }

            bool castSegment( const float3& from, const float3& to, uint32 layerMask, VfxCollisionHit& outHit ) const override
            {
                (void)layerMask;
                const bool bCrossesDownward = from._y >= _height && to._y < _height;
                if ( bCrossesDownward == false )
                    return false;
                const float32 ratio = ( from._y - _height ) / ( from._y - to._y );
                outHit._point       = from + ( to - from ) * ratio;
                outHit._normal      = float3( 0.0f, 1.0f, 0.0f );
                return true;
            }

        private:
            float32 _height;
        };
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
    SW_EXPECT_NEAR_EQUAL( 0.5f, emitter._sizeOverLife.evaluate( 0.5f ).value(), 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 0.25f, emitter._colorOverLife._green.evaluate( 0.5f ).value(), 0.0001f );
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
    SW_EXPECT_TRUE( sw::FileUtil::ensureParentDirectoryExists( path ) );
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
    sw::VfxCPUSimulation    first( sw::shareDef( emitter ), 7, 1.0f );
    sw::VfxCPUSimulation    second( sw::shareDef( emitter ), 7, 1.0f );
    sw::VfxCPUSimulation    other( sw::shareDef( emitter ), 8, 1.0f );

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
    sw::VfxCPUSimulation simulation( sw::shareDef( emitter ), 1, 1.0f );

    simulation.step( sw::kTestStepSeconds, nullptr );
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
    sw::VfxCPUSimulation simulation( sw::shareDef( emitter ), 1, 1.0f );

    (void)sw::runSteps( simulation, 60 ); // 1 초
    SW_EXPECT_NEAR_EQUAL( 30.0f, static_cast<float32>( simulation.getAliveCount() ), 1.0f );
}

/**
 * @brief [VfxCPUSimulationTest] 생성 배율(이펙트 품질)은 생성률과 버스트 수에 함께 곱한다 — 배율 0.5 면 절반이다
 */
SW_TEST_CASE( VfxCPUSimulationTest, SpawnScaleScalesRateAndBursts )
{
    sw::VfxEmitterDef emitter = sw::makeValidEmitter( "Stream" );
    emitter._spawnRate        = 30.0f;
    emitter._lifetimeMin      = 10.0f;
    emitter._lifetimeMax      = 10.0f;
    emitter._listBurst.push_back( sw::makeBurst( 0.0f, 10 ) );
    sw::VfxCPUSimulation full( sw::shareDef( emitter ), 1, 1.0f );
    sw::VfxCPUSimulation half( sw::shareDef( emitter ), 1, 0.5f );

    (void)sw::runSteps( full, 60 );
    (void)sw::runSteps( half, 60 );
    SW_EXPECT_NEAR_EQUAL( 40.0f, static_cast<float32>( full.getAliveCount() ), 1.0f );
    SW_EXPECT_NEAR_EQUAL( 20.0f, static_cast<float32>( half.getAliveCount() ), 1.0f );
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
    sw::VfxCPUSimulation simulation( sw::shareDef( emitter ), 1, 1.0f );

    test::ScopedLogCollector collector;
    (void)sw::runSteps( simulation, 60 );
    SW_EXPECT_EQUAL( uint32( 8 ), simulation.getAliveCount() );
    SW_EXPECT_EQUAL( uint32( 1 ), collector.countWithID( "Vfx.PoolFull" ) );
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
    sw::VfxCPUSimulation simulation( sw::shareDef( emitter ), 1, 1.0f );

    test::ScopedLogCollector collector;
    simulation.update( 1.0f, nullptr ); // 60 단계 몫 — 상한 8 단계만 돈다
    simulation.update( 1.0f, nullptr );
    SW_EXPECT_NEAR_EQUAL( 16.0f, static_cast<float32>( simulation.getAliveCount() ), 1.0f );
    SW_EXPECT_EQUAL( uint32( 1 ), collector.countWithID( "Vfx.StepsDropped" ) );
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
    sw::VfxCPUSimulation simulation( sw::shareDef( emitter ), 1, 1.0f );

    const sw::vector<sw::GPUParticle> listParticle = sw::runSteps( simulation, 30 ); // 0.5 초 = 수명의 절반
    SW_ASSERT_EQUAL( size_t( 1 ), listParticle.size() );
    SW_EXPECT_NEAR_EQUAL( 0.5f, listParticle[0]._rotationLife._y, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, listParticle[0]._positionSize._w, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 0.25f, listParticle[0]._color._y, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, listParticle[0]._color._w, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, listParticle[0]._positionSize._x, 0.0001f );
}

/**
 * @brief [VfxEffectDefTest] 그리는 모양마다 쓰는 칸만 받는다 — 메시는 메시 id 가 있고 텍스처가 없어야 하며, 띠는 입자 둘 이상, 라이트는 반경 · 세기가 있어야 한다
 */
SW_TEST_CASE( VfxEffectDefTest, RenderModeFieldsAreChecked )
{
    test::ScopedLogSuppressor suppressor;
    const sw::VfxEmitterDef   billboard = sw::makeValidEmitter( "Billboard" );

    sw::VfxEmitterDef mesh = billboard;
    mesh._renderMode       = sw::VfxRenderMode::Mesh;
    mesh._texturePath.clear();
    mesh._mesh._meshID = "Cube";
    SW_EXPECT_TRUE( mesh.validate( "mesh" ) );

    sw::VfxEmitterDef meshWithTexture = mesh;
    meshWithTexture._texturePath      = "game/vfx/spark.png";
    sw::VfxEmitterDef meshWithoutMesh = mesh;
    meshWithoutMesh._mesh._meshID.clear();
    sw::VfxEmitterDef billboardWithMesh  = billboard;
    billboardWithMesh._mesh._meshID      = "Cube";
    sw::VfxEmitterDef shortRibbon        = billboard;
    shortRibbon._renderMode              = sw::VfxRenderMode::Ribbon;
    shortRibbon._maxParticles            = 1;
    sw::VfxEmitterDef lightWithoutRadius = billboard;
    lightWithoutRadius._light._maxLights = 2;
    lightWithoutRadius._light._intensity = 1.0f;

    const sw::VfxEmitterDef* arrBroken[] = { &meshWithTexture, &meshWithoutMesh, &billboardWithMesh, &shortRibbon, &lightWithoutRadius };
    for ( const sw::VfxEmitterDef* pBroken : arrBroken )
    {
        SW_EXPECT_FALSE( pBroken->validate( pBroken->_name ) );
    }
}

/**
 * @brief [VfxCPUSimulationTest] 띠는 태어난 순서(오래된 것 먼저)로 낸다 — 지우기가 풀 안의 자리를 섞은 뒤에도
 */
SW_TEST_CASE( VfxCPUSimulationTest, RibbonParticlesAreWrittenInBirthOrder )
{
    sw::VfxEmitterDef emitter = sw::makeValidEmitter( "Trail" );
    emitter._renderMode       = sw::VfxRenderMode::Ribbon;
    emitter._lifetimeMin      = 0.5f; // 수명이 같으면 태어난 순서 = 수명 비율이 큰 순서
    emitter._lifetimeMax      = 0.5f;
    sw::VfxCPUSimulation simulation( sw::shareDef( emitter ), 3, 1.0f );

    const sw::vector<sw::GPUParticle> listParticle = sw::runSteps( simulation, 50 ); // 앞 입자들이 죽어 마지막 입자와 자리를 바꾼 뒤
    SW_ASSERT_TRUE( listParticle.size() > 2 );
    for ( size_t index = 1; index < listParticle.size(); ++index )
    {
        SW_EXPECT_TRUE( listParticle[index - 1]._rotationLife._y > listParticle[index]._rotationLife._y );
    }
}

/**
 * @brief [VfxCPUSimulationTest] 충돌: 튀기는 닿은 면 위로 법선 속도를 반발 계수만큼 뒤집고, 없애기는 닿은 입자를 지운다. 충돌이 꺼진 방출기는 질의를 보지 않는다
 */
SW_TEST_CASE( VfxCPUSimulationTest, CollisionBouncesOrKills )
{
    sw::VfxEmitterDef emitter = sw::makeValidEmitter( "Drop" );
    emitter._spawnRate        = 0.0f;
    emitter._listBurst.push_back( sw::makeBurst( 0.0f, 1 ) );
    emitter._lifetimeMin = 10.0f;
    emitter._lifetimeMax = 10.0f;
    emitter._velocityMin = sw::float3( 0.0f, -6.0f, 0.0f );
    emitter._velocityMax = sw::float3( 0.0f, -6.0f, 0.0f );
    emitter._gravity     = sw::float3( 0.0f, 0.0f, 0.0f );
    const sw::PlaneCollisionScene floor( -0.5f );

    sw::VfxEmitterDef bounce       = emitter;
    bounce._collision._mode        = sw::VfxCollisionMode::Bounce;
    bounce._collision._restitution = 0.5f;
    sw::VfxCPUSimulation bounceSimulation( sw::shareDef( bounce ), 1, 1.0f );
    for ( uint32 stepIndex = 0; stepIndex < 30; ++stepIndex ) // 0.5 초 — 0.083 초 뒤에 바닥에 닿는다
    {
        bounceSimulation.step( sw::kTestStepSeconds, &floor );
    }
    sw::vector<sw::GPUParticle> listBounced;
    bounceSimulation.writeParticles( listBounced );
    SW_ASSERT_EQUAL( size_t( 1 ), listBounced.size() );
    SW_EXPECT_TRUE( listBounced[0]._positionSize._y > -0.5f ); // 바닥 위로 튀어 올라 있다(속도 3 으로 위로)

    sw::VfxEmitterDef kill = emitter;
    kill._collision._mode  = sw::VfxCollisionMode::Kill;
    sw::VfxCPUSimulation killSimulation( sw::shareDef( kill ), 1, 1.0f );
    for ( uint32 stepIndex = 0; stepIndex < 30; ++stepIndex )
    {
        killSimulation.step( sw::kTestStepSeconds, &floor );
    }
    SW_EXPECT_EQUAL( uint32( 0 ), killSimulation.getAliveCount() );

    sw::VfxCPUSimulation noCollision( sw::shareDef( emitter ), 1, 1.0f ); // 충돌이 꺼져 있으면 질의가 있어도 뚫고 내려간다
    for ( uint32 stepIndex = 0; stepIndex < 30; ++stepIndex )
    {
        noCollision.step( sw::kTestStepSeconds, &floor );
    }
    sw::vector<sw::GPUParticle> listFallen;
    noCollision.writeParticles( listFallen );
    SW_ASSERT_EQUAL( size_t( 1 ), listFallen.size() );
    SW_EXPECT_NEAR_EQUAL( -3.0f, listFallen[0]._positionSize._y, 0.01f );
}

/**
 * @brief [VfxComponentTest] 효과를 읽으면 방출기마다 시뮬레이션이 서고, 재생하면 방출기마다 입자 배열을 내며, 멈추면 비운다
 */
SW_TEST_CASE( VfxComponentTest, PlayPublishesParticlesPerEmitterAndStopClearsThem )
{
    const sw::ScopedVfxSimulationTarget cpuTarget( "CPU" );
    if ( cpuTarget.isApplied() == false )
        SW_TEST_SKIP( "gv_vfxSimulation is a development-build switch (Shipping simulates these emitters on the GPU)" );
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
    SW_EXPECT_EQUAL( pVfx->getCPUAliveParticleCount(), publishedCount );

    // 낸 배열은 고쳐 쓰지 않는다 — 렌더 스레드가 들고 있는 사본은 다음 틱 뒤에도 그대로다.
    const sw::shared_ptr<const sw::vector<sw::GPUParticle>> pHeld     = pVfx->getEmitterParticles( 1 );
    const size_t                                            heldCount = pHeld->size();
    sw::tickComponentFor( *pVfx, 0.25f );
    SW_EXPECT_TRUE( pVfx->getEmitterParticles( 1 ) != pHeld );
    SW_EXPECT_EQUAL( heldCount, pHeld->size() );

    pVfx->stop();
    SW_EXPECT_FALSE( pVfx->isPlaying() );
    SW_EXPECT_EQUAL( uint32( 0 ), pVfx->getCPUAliveParticleCount() );
    SW_EXPECT_TRUE( pVfx->getEmitterParticles( 0 ) == nullptr );
    SW_EXPECT_TRUE( pVfx->getEmitterParticles( 1 ) == nullptr );
}

/**
 * @brief [VfxComponentTest] 같은 씨앗은 같은 입자를 내고, 다른 씨앗은 다른 입자를 낸다
 */
SW_TEST_CASE( VfxComponentTest, SameSeedReplaysTheSameParticles )
{
    const sw::ScopedVfxSimulationTarget cpuTarget( "CPU" );
    if ( cpuTarget.isApplied() == false )
        SW_TEST_SKIP( "gv_vfxSimulation is a development-build switch (Shipping simulates these emitters on the GPU)" );
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
    const sw::ScopedVfxSimulationTarget cpuTarget( "CPU" );
    if ( cpuTarget.isApplied() == false )
        SW_TEST_SKIP( "gv_vfxSimulation is a development-build switch (Shipping simulates these emitters on the GPU)" );
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
    SW_EXPECT_TRUE( arrVfx[0]->getCPUAliveParticleCount() > 0 );
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
        SW_EXPECT_TRUE( batch.findCPUParticles() != nullptr && batch.findCPUParticles()->empty() == false );
        SW_EXPECT_NEAR_EQUAL( 1.0f, batch._sizeScale, 0.001f );
        additiveCount += batch._bAdditive == SW_TRUE ? 1u : 0u;
    }
    SW_EXPECT_EQUAL( uint32( 2 ), additiveCount ); // 효과마다 불꽃 방출기 하나

    // 씬이 그대로인 프레임에도 입자는 다시 모은다(메시의 "바뀐 것 없음" 건너뛰기와 별개다).
    sw::tickComponentFor( *arrVfx[0], 0.1f );
    builder.populateFromScene( &scene, sw::float3( 0.0f, 0.0f, 0.0f ) );
    SW_EXPECT_TRUE( builder.getParticleBatches()[2].findCPUParticles() == arrVfx[0]->getEmitterParticles( 0 ).get() ||
                    builder.getParticleBatches()[3].findCPUParticles() == arrVfx[0]->getEmitterParticles( 0 ).get() );
}

/**
 * @brief [VfxComponentTest] 입자 그리기는 메시 투명 배치와 한 순서로 섞인다 — 주 시점은 주 배치 순서 안에, 추가 뷰는 그 뷰의 배치 순서 안에 자리를 받는다
 * @details 메시 투명 둘(z 30 · 10, 메시가 달라 배치 둘)과 효과 둘(z 20 · 5, 방출기 둘씩). 주 시점(원점)에서는 30 → 20 → 10 → 5 순이고,
 *          z 40 의 추가 뷰에서는 5 → 10 → 20 → 30 순이다(배치는 첫 인스턴스의 키, 방출기는 위치의 키로 비교한다).
 */
SW_TEST_CASE( VfxComponentTest, ParticlesInterleaveWithTransparentMeshes )
{
    sw::Scene scene( "VfxInterleave" );
    SW_EXPECT_TRUE( scene.ensureDefaultCameras() );
    sw::GameObjectManager* pObjects = scene.getObjectManager();
    SW_ASSERT_NOT_NULL( pObjects );

    const float32 arrMeshDistance[2] = { 30.0f, 10.0f };
    for ( uint32 index = 0; index < 2; ++index )
    {
        sw::MeshComponent* pMesh = pObjects->createGameObject( sw::hashed_string( "Glass" ) )->addComponent<sw::MeshComponent>();
        SW_ASSERT_NOT_NULL( pMesh );
        pMesh->setMesh( sw::MeshUtil::createUnitCube() ); // 메시마다 따로 만들어 배치가 합쳐지지 않는다
        pMesh->setLocalPosition( sw::float3( 0.0f, 0.0f, arrMeshDistance[index] ) );
        pMesh->setBlendMode( sw::RHIBlendMode::Transparent );
        pMesh->setVisible( true );
    }
    const float32 arrVfxDistance[2] = { 20.0f, 5.0f };
    for ( uint32 index = 0; index < 2; ++index )
    {
        sw::VfxComponent* pVfx = pObjects->createGameObject( sw::hashed_string( "Vfx" ) )->addComponent<sw::VfxComponent>();
        SW_ASSERT_NOT_NULL( pVfx );
        pVfx->setWorldPosition( sw::float3( 0.0f, 0.0f, arrVfxDistance[index] ) );
        pVfx->setEffectPath( sw::kVfxSampleEffectPath );
        pVfx->play();
        sw::tickComponentFor( *pVfx, 0.5f );
    }

    sw::GPUSceneBuilder builder;
    builder.populateFromScene( &scene, sw::float3( 0.0f, 0.0f, 0.0f ) );
    SW_ASSERT_EQUAL( size_t( 2 ), builder.getTransparentBatches().size() );
    const sw::vector<sw::GPUParticleBatch>&           listBatch = builder.getParticleBatches();
    const sw::vector<sw::GPUTransparentParticleSlot>& listSlot  = builder.getParticleSlots();
    SW_ASSERT_EQUAL( size_t( 4 ), listBatch.size() );
    SW_ASSERT_EQUAL( listBatch.size(), listSlot.size() );
    // 주 시점: 자리 목록은 입자 그리기 순서 그대로이고, z 20 의 방출기는 첫 배치(z 30) 뒤, z 5 의 방출기는 모든 배치 뒤다.
    for ( uint32 slotIndex = 0; slotIndex < 4; ++slotIndex )
    {
        SW_EXPECT_EQUAL( slotIndex, listSlot[slotIndex]._particleBatchIndex );
        const bool bNearEffect = listBatch[slotIndex]._world.getTranslation()._z < 10.0f;
        SW_EXPECT_EQUAL( bNearEffect ? 2u : 1u, listSlot[slotIndex]._drawPosition );
    }

    // 추가 뷰(z 40, 원점 쪽을 본다): 배치 순서가 뒤집히고, z 5 의 방출기가 모든 배치 앞, z 20 의 방출기는 z 10 배치 뒤다.
    sw::vector<sw::RenderViewRequest> listView( 1 );
    listView[0]._viewID   = 7u;
    listView[0]._position = sw::float3( 0.0f, 0.0f, 40.0f );
    builder.computeViewTransparentOrders( listView );
    SW_ASSERT_EQUAL( size_t( 1 ), builder.getViewTransparentOrders().size() );
    const sw::GPUViewTransparentOrder& order = builder.getViewTransparentOrders()[0];
    SW_ASSERT_EQUAL( size_t( 2 ), order._listBatchOrder.size() );
    SW_EXPECT_EQUAL( 1u, order._listBatchOrder[0] );
    SW_EXPECT_EQUAL( 0u, order._listBatchOrder[1] );
    SW_ASSERT_EQUAL( size_t( 4 ), order._listParticleSlot.size() );
    for ( uint32 slotIndex = 0; slotIndex < 4; ++slotIndex )
    {
        const sw::GPUTransparentParticleSlot& slot        = order._listParticleSlot[slotIndex];
        const bool                            bNearEffect = listBatch[slot._particleBatchIndex]._world.getTranslation()._z < 10.0f;
        SW_EXPECT_EQUAL( slotIndex < 2, bNearEffect ); // 이 뷰에서는 z 5 가 가장 멀다
        SW_EXPECT_EQUAL( bNearEffect ? 0u : 1u, slot._drawPosition );
    }
}

/**
 * @brief [VfxComponentTest] 띠 · 메시 · 라이트 표본 효과 — 띠는 입자 그리기 하나(띠 표시), 메시 방출기는 풀 크기의 인스턴스 배치로 메시 경로를 타고
 *        입자 그리기를 내지 않으며, 파티클 라이트는 씬 라이트 목록에 점광으로 붙는다(방출기마다 `_maxLights` 개까지)
 */
SW_TEST_CASE( VfxComponentTest, TrailSampleDrivesRibbonMeshAndLights )
{
    // 라이트 방출기도 GPU 로 돌 수 있다(VfxLightDef) — 이 테스트는 CPU 방출기의 라이트(씬 라이트 목록)를 본다. GPU 방출기의 빛은 라이트 버퍼의 GPU 구간이 맡는다.
    const sw::ScopedVfxSimulationTarget cpuTarget( "CPU" );
    if ( cpuTarget.isApplied() == false )
        SW_TEST_SKIP( "gv_vfxSimulation is a development-build switch (Shipping simulates these emitters on the GPU)" );
    sw::Scene scene( "VfxTrail" );
    SW_EXPECT_TRUE( scene.ensureDefaultCameras() );
    sw::VfxComponent* pVfx = scene.getObjectManager()->createGameObject( sw::hashed_string( "Trail" ) )->addComponent<sw::VfxComponent>();
    SW_ASSERT_NOT_NULL( pVfx );
    pVfx->setEffectPath( "engine/vfx/trail.vfx.xml" );
    SW_ASSERT_EQUAL( uint32( 2 ), pVfx->getEmitterCount() );
    SW_EXPECT_TRUE( pVfx->getEmitterDef( 0 )._renderMode == sw::VfxRenderMode::Ribbon );
    SW_EXPECT_TRUE( pVfx->findEmitterMeshBatch( 0 ) == nullptr );
    const sw::MeshInstanceBatch* pMeshBatch = pVfx->findEmitterMeshBatch( 1 );
    SW_ASSERT_NOT_NULL( pMeshBatch );
    SW_EXPECT_EQUAL( pVfx->getEmitterDef( 1 )._maxParticles, pMeshBatch->getCount() );
    SW_EXPECT_TRUE( pMeshBatch->isRegistered() );

    pVfx->play();
    sw::tickComponentFor( *pVfx, 0.3f ); // 물리 씬이 없다 — 충돌 없이 돈다
    SW_ASSERT_NOT_NULL( pVfx->getEmitterParticles( 1 ).get() );
    SW_EXPECT_EQUAL( size_t( 12 ), pVfx->getEmitterParticles( 1 )->size() );
    // 살아 있는 자리는 입자 크기 · 위치의 행렬, 남는 자리는 크기 0 이다.
    SW_EXPECT_TRUE( pMeshBatch->getEntry( 0 )._world.getMaximumAxisScale() > 0.0f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, pMeshBatch->getEntry( 15 )._world.getMaximumAxisScale(), 0.0001f );

    sw::GPUSceneBuilder builder;
    builder.populateFromScene( &scene, sw::float3( 0.0f, 0.0f, -10.0f ) );
    SW_ASSERT_EQUAL( size_t( 1 ), builder.getParticleBatches().size() );
    SW_EXPECT_TRUE( builder.getParticleBatches()[0]._bRibbon == SW_TRUE );

    // 씬에 라이트 컴포넌트가 없으므로 점광은 모두 파티클 라이트다.
    sw::vector<sw::GPULight> listLight;
    sw::collectSceneLights( &scene, listLight );
    SW_EXPECT_EQUAL( pVfx->getEmitterDef( 1 )._light._maxLights, sw::countPointLights( listLight ) );

    // 멈추면 메시 항목은 모두 크기 0, 파티클 라이트도 빠진다.
    pVfx->stop();
    SW_EXPECT_NEAR_EQUAL( 0.0f, pMeshBatch->getEntry( 0 )._world.getMaximumAxisScale(), 0.0001f );
    sw::collectSceneLights( &scene, listLight );
    SW_EXPECT_EQUAL( uint32( 0 ), sw::countPointLights( listLight ) );
}

/**
 * @brief [VfxEffectDefTest] 방출기의 대상은 모듈 대상의 교집합이다 — 빌보드 · 띠 · 메시는 둘 다, 물리 충돌 · 라이트는 CPU, 깊이 충돌은 GPU 이고,
 *        CPU 전용과 GPU 전용을 함께 쓰면 비어 로드가 실패한다
 */
SW_TEST_CASE( VfxEffectDefTest, ModuleTargetsIntersect )
{
    const sw::VfxSimulationTarget kCPU      = sw::VfxSimulationTarget::CPU;
    const sw::VfxSimulationTarget kGPU      = sw::VfxSimulationTarget::GPU;
    sw::VfxEmitterDef             billboard = sw::makeValidEmitter( "Billboard" );
    SW_EXPECT_TRUE( billboard.computeSupportedTargets().has( kCPU ) && billboard.computeSupportedTargets().has( kGPU ) );
    SW_EXPECT_TRUE( billboard.findModuleExcluding( kGPU ) == nullptr );
    sw::VfxEmitterDef ribbon = billboard;
    ribbon._renderMode       = sw::VfxRenderMode::Ribbon;
    SW_EXPECT_TRUE( ribbon.computeSupportedTargets().has( kGPU ) );
    sw::VfxEmitterDef mesh = billboard;
    mesh._renderMode       = sw::VfxRenderMode::Mesh;
    SW_EXPECT_TRUE( mesh.computeSupportedTargets().has( kCPU ) && mesh.computeSupportedTargets().has( kGPU ) );

    sw::VfxEmitterDef raycast  = billboard;
    raycast._collision._mode   = sw::VfxCollisionMode::Bounce;
    raycast._collision._method = sw::VfxCollisionMethod::PhysicsRaycast;
    SW_EXPECT_FALSE( raycast.computeSupportedTargets().has( kGPU ) );
    SW_ASSERT_NOT_NULL( raycast.findModuleExcluding( kGPU ) );
    SW_EXPECT_STREQ( "_collision", raycast.findModuleExcluding( kGPU ) );

    sw::VfxEmitterDef depth          = billboard;
    depth._collision._mode           = sw::VfxCollisionMode::Kill;
    depth._collision._method         = sw::VfxCollisionMethod::SceneDepth;
    depth._collision._depthThickness = 0.2f;
    SW_EXPECT_FALSE( depth.computeSupportedTargets().has( kCPU ) );
    SW_EXPECT_TRUE( depth.computeSupportedTargets().has( kGPU ) );
    SW_EXPECT_TRUE( depth.validate( "test" ) );

    // 라이트는 CPU(씬 라이트 목록) · GPU(라이트 버퍼의 GPU 구간) 어디서나 돈다 — VfxGpuLights.md L4.
    sw::VfxEmitterDef light = billboard;
    light._light._maxLights = 2;
    light._light._radius    = 1.0f;
    light._light._intensity = 1.0f;
    SW_EXPECT_TRUE( light.computeSupportedTargets().has( kCPU ) && light.computeSupportedTargets().has( kGPU ) );
    SW_EXPECT_TRUE( light.findModuleExcluding( kGPU ) == nullptr );
    SW_EXPECT_TRUE( light.validate( "test" ) );

    // 깊이 충돌(GPU 전용) + 라이트 — 옛 규칙(라이트는 CPU 전용)에서는 어느 곳에서도 돌 수 없었다. 이제 GPU 에서 돌고 로드가 성공한다.
    sw::VfxEmitterDef depthLight = depth;
    depthLight._light            = light._light;
    SW_EXPECT_FALSE( depthLight.computeSupportedTargets().has( kCPU ) );
    SW_EXPECT_TRUE( depthLight.computeSupportedTargets().has( kGPU ) );
    SW_EXPECT_TRUE( depthLight.validate( "test" ) );

    // 물리 충돌(CPU 전용) + 라이트 — CPU 에서 돈다.
    sw::VfxEmitterDef raycastLight = raycast;
    raycastLight._light            = light._light;
    SW_EXPECT_TRUE( raycastLight.computeSupportedTargets().has( kCPU ) );
    SW_EXPECT_FALSE( raycastLight.computeSupportedTargets().has( kGPU ) );

    // 모듈이 허락한 곳의 교집합이 비면 로드가 실패한다 — 충돌 방법이 알 수 없는 값이면(읽기가 거절하는 값) 어느 곳도 허락하지 않는다.
    sw::VfxEmitterDef impossible  = depth;
    impossible._collision._method = static_cast<sw::VfxCollisionMethod>( 99 );
    SW_EXPECT_TRUE( impossible.computeSupportedTargets().isEmpty() );
    {
        test::ScopedLogCollector collector;
        SW_EXPECT_FALSE( impossible.validate( "test" ) );
        SW_EXPECT_EQUAL( uint32( 1 ), collector.countWithID( "Vfx.NoSimulationTargetForEmitter" ) );
    }
    // 깊이 방법은 두께가 있어야 한다.
    sw::VfxEmitterDef thin          = depth;
    thin._collision._depthThickness = 0.0f;
    {
        test::ScopedLogSuppressor suppressor;
        SW_EXPECT_FALSE( thin.validate( "test" ) );
    }
    // 수명을 늘리는 모듈은 없다 — GPU 방출기의 "끝났다" 가 일정에서 구해지는 근거다.
    SW_EXPECT_TRUE( billboard.isLifetimeBoundedByMax() );
}

/**
 * @brief [GPULightBufferTest] GPU 구간(파티클 라이트)은 GPU 생산자만 받는다 — CPU 생산자와 그리드 끔은 0 이고 조용히 GPU 로 바꾸지 않으며, 요청이 상한을 넘으면 상한까지다
 */
SW_TEST_CASE( GPULightBufferTest, GPULightSectionIsGrantedOnlyToTheGPUProducer )
{
    constexpr uint32 kRequested = 40;
    SW_EXPECT_EQUAL( kRequested, sw::GPULightBuffer::selectGPULightSlotCount( sw::LightGridProducer::GPU, kRequested ) );
    SW_EXPECT_EQUAL( uint32( 0 ), sw::GPULightBuffer::selectGPULightSlotCount( sw::LightGridProducer::CPU, kRequested ) );
    SW_EXPECT_EQUAL( uint32( 0 ), sw::GPULightBuffer::selectGPULightSlotCount( sw::LightGridProducer::None, kRequested ) );
    SW_EXPECT_EQUAL( sw::GPULightBuffer::kMaxGPULight, sw::GPULightBuffer::selectGPULightSlotCount( sw::LightGridProducer::GPU, sw::GPULightBuffer::kMaxGPULight + 1 ) );
    // CPU 라이트와 GPU 구간이 프레임 라이트 상한을 나눠 쓴다.
    SW_EXPECT_TRUE( sw::GPULightBuffer::kMaxGPULight < sw::shaderslot::kMaxFrameLight );
}

/**
 * @brief [VfxGPUSimulationTest] 파티클 라이트 슬롯은 GPU 방출기에 차례로 나뉜다 — CPU 방출기와 라이트가 없는 방출기는 받지 않고, 상한을 넘는 방출기는 라이트가 없다
 */
SW_TEST_CASE( VfxGPUSimulationTest, LightRangesAreAssignedInOrderWithinTheGPUSection )
{
    sw::VfxEmitterDef lit   = sw::makeValidEmitter( "Lit" );
    lit._light._maxLights   = 100;
    lit._light._radius      = 1.0f;
    lit._light._intensity   = 1.0f;
    sw::VfxEmitterDef unlit = sw::makeValidEmitter( "Unlit" );
    sw::VfxEmitterDef big   = lit;
    big._light._maxLights   = 200;

    sw::vector<sw::GPUParticleBatch> listBatch;
    listBatch.push_back( sw::makeGPUBatch( lit, 1 ) );   // 0..99
    listBatch.push_back( sw::makeGPUBatch( unlit, 2 ) ); // 라이트 없음
    listBatch.push_back( sw::makeGPUBatch( lit, 3 ) );   // 100..199
    listBatch.push_back( sw::makeGPUBatch( big, 4 ) );   // 200 + 200 > 상한 256 — 라이트 없음
    sw::GPUParticleBatch cpuBatch;
    cpuBatch._source = sw::make_shared<const sw::vector<sw::GPUParticle>>();
    listBatch.push_back( cpuBatch ); // CPU 방출기는 GPU 구간을 쓰지 않는다
    const sw::vector<sw::GPUVfxMeshEmitter> listMesh;

    sw::vector<sw::VfxGPULightRange> listRange;
    const sw::VfxGPULightAssignment  assignment = sw::VfxGPUSimulation::assignLightRanges( listBatch, listMesh, listRange );
    SW_ASSERT_EQUAL( listBatch.size(), listRange.size() );
    SW_EXPECT_EQUAL( uint32( 200 ), assignment._slotCount );
    SW_EXPECT_EQUAL( uint32( 1 ), assignment._overflowEmitterCount );
    SW_EXPECT_EQUAL( uint32( 0 ), listRange[0]._first );
    SW_EXPECT_EQUAL( uint32( 100 ), listRange[0]._count );
    SW_EXPECT_EQUAL( uint32( 0 ), listRange[1]._count );
    SW_EXPECT_EQUAL( uint32( 100 ), listRange[2]._first );
    SW_EXPECT_EQUAL( uint32( 100 ), listRange[2]._count );
    SW_EXPECT_EQUAL( uint32( 0 ), listRange[3]._count );
    SW_EXPECT_EQUAL( uint32( 0 ), listRange[4]._count );
    // 구간 합이 상한 안이라 라이트 버퍼가 그대로 받는다.
    SW_EXPECT_TRUE( assignment._slotCount <= sw::GPULightBuffer::kMaxGPULight );
}

/**
 * @brief [VfxEffectDefTest] 방출기의 모든 칸(리플렉션 프로퍼티)이 모듈 표에 대상을 선언했다 — 칸을 더하고 줄을 빠뜨리면 여기서 진다
 */
SW_TEST_CASE( VfxEffectDefTest, EveryEmitterFieldDeclaresItsTargets )
{
    const sw::TypeInfo* pType = sw::VfxEmitterDef::StaticType();
    SW_ASSERT_NOT_NULL( pType );
    uint32 fieldCount = 0;
    pType->forEachProperty( [&fieldCount]( const sw::PropertyInfo& property )
    {
        ++fieldCount;
        SW_EXPECT_TRUE_MSG( sw::VfxEmitterDef::hasModuleRow( property._name.view() ), property._name.c_str() );
    } );
    SW_EXPECT_TRUE( fieldCount > 0 );
}

/**
 * @brief [VfxEmitterClockTest] 시계의 일정은 CPU 시뮬레이션이 만든 입자와 같다 — 번호는 끊기지 않고, 풀이 넉넉하면 만든 수가 일정의 합이다
 * @details GPU 시뮬레이션은 같은 시계로 일정을 얻는다(`VfxGPUSimulation`) — 이 테스트가 두 쪽이 같은 수 · 같은 번호로 입자를 만드는 근거다.
 */
SW_TEST_CASE( VfxEmitterClockTest, ScheduleMatchesTheCPUSimulation )
{
    sw::VfxEmitterDef emitter = sw::makeValidEmitter( "Schedule" );
    emitter._spawnRate        = 45.0f;
    emitter._lifetimeMin      = 10.0f; // 이 테스트 동안 아무도 죽지 않는다
    emitter._lifetimeMax      = 10.0f;
    emitter._maxParticles     = 1000;
    emitter._listBurst.push_back( sw::makeBurst( 0.25f, 5 ) );
    sw::VfxEmitterClock  clock( sw::shareDef( emitter ), 1.0f );
    sw::VfxCPUSimulation simulation( sw::shareDef( emitter ), 1, 1.0f );

    uint64 expectedIndex = 0;
    uint64 scheduledSum  = 0;
    for ( uint32 stepIndex = 0; stepIndex < 60; ++stepIndex )
    {
        const sw::VfxSpawnStep spawn = clock.spawnStep( sw::kTestStepSeconds );
        SW_EXPECT_EQUAL( expectedIndex, spawn._firstParticleIndex );
        expectedIndex += spawn._count;
        scheduledSum += spawn._count;
        simulation.step( sw::kTestStepSeconds, nullptr );
    }
    SW_EXPECT_EQUAL( scheduledSum, clock.getSpawnedCount() );
    SW_EXPECT_EQUAL( clock.getSpawnedCount(), simulation.getSpawnedCount() );
    SW_EXPECT_EQUAL( static_cast<uint64>( simulation.getAliveCount() ), scheduledSum );
}

/**
 * @brief [VfxGPUEmitterTest] GPU 방출기는 누적 단계 수를 싣고, 방출이 끝난 뒤 가장 긴 수명이 지나야 끝나며, 다시 시작하면 새 재생 번호를 받는다
 */
SW_TEST_CASE( VfxGPUEmitterTest, FinishesAfterTheLongestLifetimeAndRestartsWithANewRun )
{
    sw::VfxEmitterDef emitter = sw::makeValidEmitter( "Once" );
    emitter._spawnRate        = 0.0f;
    emitter._duration         = 0.5f;
    emitter._lifetimeMin      = 0.5f;
    emitter._lifetimeMax      = 1.0f;
    emitter._listBurst.push_back( sw::makeBurst( 0.0f, 4 ) );
    sw::VfxGPUEmitter gpuEmitter( sw::shareDef( emitter ), 3, 1.0f );

    for ( uint32 stepIndex = 0; stepIndex < 54; ++stepIndex ) // 0.9 초 — 가장 긴 수명 전
    {
        gpuEmitter.update( sw::kTestStepSeconds );
    }
    const sw::VfxGPUEmitterRequest firstRequest = gpuEmitter.makeRequest();
    SW_EXPECT_FALSE( gpuEmitter.isFinished() );
    SW_EXPECT_EQUAL( uint64( 4 ), gpuEmitter.getSpawnedCount() );
    SW_EXPECT_NEAR_EQUAL( 54.0f, static_cast<float32>( firstRequest._stepCount ), 1.0f );
    SW_EXPECT_EQUAL( uint64( 3 ), firstRequest._seed );
    SW_EXPECT_TRUE( firstRequest._pDef != nullptr );

    for ( uint32 stepIndex = 0; stepIndex < 18; ++stepIndex ) // 1.2 초
    {
        gpuEmitter.update( sw::kTestStepSeconds );
    }
    SW_EXPECT_TRUE( gpuEmitter.isFinished() );

    gpuEmitter.restart();
    const sw::VfxGPUEmitterRequest restarted = gpuEmitter.makeRequest();
    SW_EXPECT_TRUE( restarted._runID != firstRequest._runID );
    SW_EXPECT_EQUAL( uint64( 0 ), restarted._stepCount );
    SW_EXPECT_FALSE( gpuEmitter.isFinished() );
}

/**
 * @brief [VfxComponentTest] 방출기마다 시뮬레이션 대상을 정한다 — GPU 로 고른 빌보드 방출기는 입자 배열 대신 요청을 싣고, CPU 전용 모듈(메시 · 충돌 · 라이트)의
 *        방출기는 고른 값과 무관하게 CPU 다
 * @details Shipping 은 변수가 없고 GPU 로 고정이라 같은 기대가 맞는다.
 */
SW_TEST_CASE( VfxComponentTest, EmittersPickTheirSimulationTarget )
{
    const sw::ScopedVfxSimulationTarget gpuTarget( "GPU" );
    (void)gpuTarget; // Shipping 에서는 적용되지 않는다 — GPU 고정이라 기대가 같다
    sw::Scene scene( "VfxTarget" );
    SW_EXPECT_TRUE( scene.ensureDefaultCameras() );
    sw::VfxComponent* pSparks = scene.getObjectManager()->createGameObject( sw::hashed_string( "Sparks" ) )->addComponent<sw::VfxComponent>();
    SW_ASSERT_NOT_NULL( pSparks );
    pSparks->setEffectPath( sw::kVfxSampleEffectPath );
    SW_ASSERT_EQUAL( uint32( 2 ), pSparks->getEmitterCount() );
    SW_EXPECT_TRUE( pSparks->getEmitterSimulationTarget( 0 ) == sw::VfxSimulationTarget::GPU );
    SW_EXPECT_TRUE( pSparks->getEmitterSimulationTarget( 1 ) == sw::VfxSimulationTarget::GPU );
    SW_EXPECT_TRUE( pSparks->findEmitterGPURequest( 0 ) == nullptr ); // 재생 전

    pSparks->play();
    sw::tickComponentFor( *pSparks, 0.5f );
    SW_EXPECT_TRUE( pSparks->isPlaying() );
    SW_EXPECT_TRUE( pSparks->getEmitterParticles( 0 ) == nullptr );
    const sw::VfxGPUEmitterRequest* pRequest = pSparks->findEmitterGPURequest( 1 );
    SW_ASSERT_NOT_NULL( pRequest );
    SW_EXPECT_NEAR_EQUAL( 30.0f, static_cast<float32>( pRequest->_stepCount ), 1.0f );
    SW_EXPECT_EQUAL( uint32( 0 ), pSparks->getCPUAliveParticleCount() );
    SW_EXPECT_TRUE( pSparks->getSpawnedParticleCount() > 0 );

    sw::GPUSceneBuilder builder;
    builder.populateFromScene( &scene, sw::float3( 0.0f, 0.0f, -10.0f ) );
    SW_ASSERT_EQUAL( size_t( 2 ), builder.getParticleBatches().size() );
    for ( const sw::GPUParticleBatch& batch : builder.getParticleBatches() )
    {
        SW_EXPECT_TRUE( batch.findGPURequest() != nullptr );
        SW_EXPECT_TRUE( batch.findCPUParticles() == nullptr );
    }

    pSparks->stop();
    SW_EXPECT_TRUE( pSparks->findEmitterGPURequest( 1 ) == nullptr );
    builder.populateFromScene( &scene, sw::float3( 0.0f, 0.0f, -10.0f ) );
    SW_EXPECT_TRUE( builder.getParticleBatches().empty() );

    // 띠는 GPU, 메시 · 충돌 · 라이트를 쓰는 파편은 CPU 다.
    sw::VfxComponent* pTrail = scene.getObjectManager()->createGameObject( sw::hashed_string( "Trail" ) )->addComponent<sw::VfxComponent>();
    SW_ASSERT_NOT_NULL( pTrail );
    pTrail->setEffectPath( "engine/vfx/trail.vfx.xml" );
    SW_ASSERT_EQUAL( uint32( 2 ), pTrail->getEmitterCount() );
    SW_EXPECT_TRUE( pTrail->getEmitterSimulationTarget( 0 ) == sw::VfxSimulationTarget::GPU );
    SW_EXPECT_TRUE( pTrail->getEmitterSimulationTarget( 1 ) == sw::VfxSimulationTarget::CPU );
}

/**
 * @brief [VfxComponentTest] GPU 메시 방출기 — 배치는 GPU 가 쓰는 형식(`MeshInstanceWriter::GPU`)이라 항목이 자리표로 남고, 스냅샷은 풀 자리마다 이번 프레임의
 *        인스턴스 칸을 싣는다(컴퓨트가 그 칸에 쓴다). 깊이 충돌이 GPU 전용이라 표본은 모든 빌드에서 GPU 다
 */
SW_TEST_CASE( VfxComponentTest, GPUMeshEmitterPublishesInstanceSlots )
{
    sw::Scene scene( "VfxGPUMesh" );
    SW_EXPECT_TRUE( scene.ensureDefaultCameras() );
    sw::VfxComponent* pVfx = scene.getObjectManager()->createGameObject( sw::hashed_string( "GPUDebris" ) )->addComponent<sw::VfxComponent>();
    SW_ASSERT_NOT_NULL( pVfx );
    pVfx->setEffectPath( "engine/vfx/gpudebris.vfx.xml" );
    SW_ASSERT_EQUAL( uint32( 1 ), pVfx->getEmitterCount() );
    SW_EXPECT_TRUE( pVfx->getEmitterSimulationTarget( 0 ) == sw::VfxSimulationTarget::GPU );
    const sw::MeshInstanceBatch* pBatch = pVfx->findEmitterMeshBatch( 0 );
    SW_ASSERT_NOT_NULL( pBatch );
    SW_EXPECT_TRUE( pBatch->getInstanceWriter() == sw::MeshInstanceWriter::GPU );
    SW_EXPECT_EQUAL( pVfx->getEmitterDef( 0 )._maxParticles, pBatch->getCount() );
    SW_EXPECT_NEAR_EQUAL( 0.0f, pBatch->getEntry( 0 )._world.getMaximumAxisScale(), 0.0001f ); // 자리표

    pVfx->play();
    sw::tickComponentFor( *pVfx, 0.3f );
    SW_EXPECT_TRUE( pVfx->getEmitterParticles( 0 ) == nullptr );                               // 입자는 GPU 에 있다
    SW_EXPECT_NEAR_EQUAL( 0.0f, pBatch->getEntry( 0 )._world.getMaximumAxisScale(), 0.0001f ); // CPU 는 항목을 쓰지 않는다

    sw::GPUSceneBuilder builder;
    builder.populateFromScene( &scene, sw::float3( 0.0f, 0.0f, -10.0f ) );
    SW_EXPECT_TRUE( builder.getParticleBatches().empty() ); // 메시 방출기는 입자 그리기가 아니다
    SW_ASSERT_EQUAL( size_t( 1 ), builder.getVfxMeshEmitters().size() );
    const sw::GPUVfxMeshEmitter& emitter = builder.getVfxMeshEmitters()[0];
    SW_ASSERT_NOT_NULL( emitter._pListInstanceSlot.get() );
    SW_ASSERT_EQUAL( size_t( pBatch->getCount() ), emitter._pListInstanceSlot->size() );
    for ( const uint32 slot : *emitter._pListInstanceSlot )
    {
        SW_EXPECT_TRUE( slot < builder.getInstances().size() );
    }
    SW_EXPECT_TRUE( emitter._meshRadius > 0.0f );
    SW_EXPECT_TRUE( emitter._request._pDef != nullptr );

    pVfx->stop();
    builder.populateFromScene( &scene, sw::float3( 0.0f, 0.0f, -10.0f ) );
    SW_EXPECT_TRUE( builder.getVfxMeshEmitters().empty() );
}
