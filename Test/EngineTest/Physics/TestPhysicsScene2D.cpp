#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/PhysicsSettings.h"
#include "Engine/Physics/PhysicsSystem.h"

#include "TestFramework/TestFramework.h"

// 2D 물리 씬(Box2D 백엔드)을 인터페이스로만 잰다 — 3D 시험(`PhysicsScene3DTest`)과 같은 질문을 평면에서.

namespace
{
    constexpr float32 kStep2D = 1.0f / 60.0f;

    sw::PhysicsSettings makeSettings()
    {
        sw::PhysicsSettings settings;
        sw::PhysicsLayerDef defaultLayer;
        defaultLayer._name = sw::hashed_string( "Default" );
        defaultLayer._listCollidesWith.push_back( sw::hashed_string( "Default" ) );
        sw::PhysicsLayerDef ghostLayer;
        ghostLayer._name    = sw::hashed_string( "Ghost" );
        settings._listLayer = { defaultLayer, ghostLayer };

        sw::PhysicsMaterialDef defaultMaterial;
        defaultMaterial._name = sw::hashed_string( "Default" );
        sw::PhysicsMaterialDef bouncy;
        bouncy._name           = sw::hashed_string( "Bouncy" );
        bouncy._restitution    = 0.8f;
        settings._listMaterial = { defaultMaterial, bouncy };
        return settings;
    }

    sw::unique_ptr<sw::IPhysicsScene2D> makeScene()
    {
        return sw::engine::getPhysicsSystem().createScene2D( makeSettings() );
    }

    sw::PhysicsShapeDesc2D makeBox( const sw::float2& halfExtents )
    {
        sw::PhysicsShapeDesc2D shape;
        shape._type        = sw::PhysicsShapeType2D::Box;
        shape._halfExtents = halfExtents;
        return shape;
    }

    sw::PhysicsShapeDesc2D makeCircle( float32 radius )
    {
        sw::PhysicsShapeDesc2D shape;
        shape._type   = sw::PhysicsShapeType2D::Circle;
        shape._radius = radius;
        return shape;
    }

    sw::PhysicsBodyHandle addBody( sw::IPhysicsScene2D& scene, const sw::PhysicsShapeDesc2D& shape, const sw::float2& position, sw::PhysicsBodyType type,
                                   uint8 layer = 0, const utf8* pMaterial = "", uint64 userData = 0 )
    {
        sw::PhysicsBodyDesc2D desc;
        desc._listShape.push_back( shape );
        desc._position = position;
        desc._type     = type;
        desc._layer    = layer;
        desc._material = sw::hashed_string( pMaterial );
        desc._userData = userData;
        return scene.createBody( desc );
    }

    sw::PhysicsBodyHandle addFloor( sw::IPhysicsScene2D& scene, const utf8* pMaterial = "" )
    {
        return addBody( scene, makeBox( sw::float2{ 20.0f, 0.5f } ), sw::float2{ 0.0f, -0.5f }, sw::PhysicsBodyType::Static, 0, pMaterial );
    }

    sw::float2 getPosition( const sw::IPhysicsScene2D& scene, sw::PhysicsBodyHandle body )
    {
        sw::float2 position{};
        float32    angle = 0.0f;
        (void)scene.getBodyTransform( body, position, angle );
        return position;
    }

    void stepFor( sw::IPhysicsScene2D& scene, uint32 stepCount )
    {
        for ( uint32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
        {
            scene.step( kStep2D );
        }
    }
} // namespace

/**
 * @brief [PhysicsScene2DTest] 떨어진 상자는 바닥 위(반 높이)에서 멈춘다
 */
SW_TEST_CASE( PhysicsScene2DTest, BoxComesToRestOnFloor )
{
    sw::unique_ptr<sw::IPhysicsScene2D> pScene = makeScene();
    SW_ASSERT_NOT_NULL( pScene.get() );
    addFloor( *pScene );
    const sw::PhysicsBodyHandle box = addBody( *pScene, makeBox( sw::float2{ 0.5f, 0.5f } ), sw::float2{ 0.0f, 3.0f }, sw::PhysicsBodyType::Dynamic );
    SW_ASSERT_TRUE( box.isValid() );
    stepFor( *pScene, 300 );
    SW_EXPECT_NEAR_EQUAL( 0.5f, getPosition( *pScene, box )._y, 0.02f );
    SW_EXPECT_TRUE( pScene->getLinearVelocity( box ).getLength() < 0.05f );
    SW_EXPECT_NEAR_EQUAL( 1000.0f, pScene->getBodyMass( box ), 1.0f ); // 1 m^2 × 밀도 1000
}

/**
 * @brief [PhysicsScene2DTest] 반발이 큰 재질은 튀어 오르고, 기본 재질(반발 0)은 튀지 않는다
 */
SW_TEST_CASE( PhysicsScene2DTest, RestitutionBouncesByMaterial )
{
    float32     arrPeak[2]     = { 0.0f, 0.0f };
    const utf8* arrMaterial[2] = { "Bouncy", "Default" };
    for ( uint32 caseIndex = 0; caseIndex < 2; ++caseIndex )
    {
        sw::unique_ptr<sw::IPhysicsScene2D> pScene = makeScene();
        SW_ASSERT_NOT_NULL( pScene.get() );
        addFloor( *pScene, arrMaterial[caseIndex] );
        const sw::PhysicsBodyHandle ball     = addBody( *pScene, makeCircle( 0.5f ), sw::float2{ 0.0f, 5.5f }, sw::PhysicsBodyType::Dynamic, 0, arrMaterial[caseIndex] );
        bool                        bTouched = false;
        for ( uint32 stepIndex = 0; stepIndex < 240; ++stepIndex )
        {
            pScene->step( kStep2D );
            const float32 height = getPosition( *pScene, ball )._y;
            if ( height < 0.55f )
                bTouched = true;
            if ( bTouched && height > arrPeak[caseIndex] )
                arrPeak[caseIndex] = height;
        }
        SW_EXPECT_TRUE( bTouched );
    }
    SW_EXPECT_TRUE_MSG( arrPeak[0] > 2.0f, "a bouncy ball rises again" );
    SW_EXPECT_TRUE_MSG( arrPeak[1] < 0.7f, "a dead ball stays down" );
}

/**
 * @brief [PhysicsScene2DTest] 레이캐스트는 닿은 거리 · 법선 · 바디를 돌려주고, 마스크 밖 레이어는 지나친다
 */
SW_TEST_CASE( PhysicsScene2DTest, RaycastReportsHitDistance )
{
    sw::unique_ptr<sw::IPhysicsScene2D> pScene = makeScene();
    SW_ASSERT_NOT_NULL( pScene.get() );
    const sw::PhysicsBodyHandle target = addBody( *pScene, makeBox( sw::float2{ 0.5f, 0.5f } ), sw::float2{ 5.0f, 0.0f }, sw::PhysicsBodyType::Static, 0, "", 77 );

    sw::PhysicsQueryFilter filter;
    sw::PhysicsCastHit2D   hit;
    SW_ASSERT_TRUE( pScene->raycast( sw::float2{}, sw::float2{ 1.0f, 0.0f }, 100.0f, filter, hit ) );
    SW_EXPECT_NEAR_EQUAL( 4.5f, hit._distance, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( -1.0f, hit._normal._x, 1e-3f );
    SW_EXPECT_TRUE( hit._body == target );
    SW_EXPECT_EQUAL( static_cast<uint64>( 77 ), hit._userData );
    filter._layerMask = 1u << 1;
    SW_EXPECT_FALSE( pScene->raycast( sw::float2{}, sw::float2{ 1.0f, 0.0f }, 100.0f, filter, hit ) );

    filter._layerMask = sw::MathUtil::kMaxUInt32;
    SW_ASSERT_TRUE( pScene->shapeCast( makeCircle( 0.5f ), sw::float2{}, 0.0f, sw::float2{ 1.0f, 0.0f }, 100.0f, filter, hit ) );
    SW_EXPECT_NEAR_EQUAL( 4.0f, hit._distance, 2e-2f );
    sw::vector<sw::PhysicsBodyHandle> listFound;
    SW_EXPECT_EQUAL( 1u, pScene->overlapShape( makeBox( sw::float2{ 1.0f, 1.0f } ), sw::float2{ 5.5f, 0.0f }, 0.0f, filter, listFound ) );
    SW_EXPECT_EQUAL( 0u, pScene->overlapShape( makeBox( sw::float2{ 1.0f, 1.0f } ), sw::float2{ -5.0f, 0.0f }, 0.0f, filter, listFound ) );
}

/**
 * @brief [PhysicsScene2DTest] 레이어 표가 막은 쌍은 지나치고 허락한 쌍은 막는다. 쌍 하나만 끄면 그 둘만 지나친다
 */
SW_TEST_CASE( PhysicsScene2DTest, LayerFilterBlocksPair )
{
    sw::unique_ptr<sw::IPhysicsScene2D> pScene = makeScene();
    SW_ASSERT_NOT_NULL( pScene.get() );
    addFloor( *pScene );
    const sw::PhysicsBodyHandle ghost = addBody( *pScene, makeBox( sw::float2{ 0.5f, 0.5f } ), sw::float2{ -3.0f, 2.0f }, sw::PhysicsBodyType::Dynamic, 1 );
    const sw::PhysicsBodyHandle solid = addBody( *pScene, makeBox( sw::float2{ 0.5f, 0.5f } ), sw::float2{ 3.0f, 2.0f }, sw::PhysicsBodyType::Dynamic, 0 );
    stepFor( *pScene, 120 );
    SW_EXPECT_TRUE( getPosition( *pScene, ghost )._y < -5.0f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, getPosition( *pScene, solid )._y, 0.02f );

    const sw::PhysicsBodyHandle floor2  = addBody( *pScene, makeBox( sw::float2{ 2.0f, 0.5f } ), sw::float2{ 30.0f, -0.5f }, sw::PhysicsBodyType::Static );
    const sw::PhysicsBodyHandle pairBox = addBody( *pScene, makeBox( sw::float2{ 0.5f, 0.5f } ), sw::float2{ 30.0f, 2.0f }, sw::PhysicsBodyType::Dynamic );
    pScene->setPairCollision( floor2, pairBox, false );
    stepFor( *pScene, 120 );
    SW_EXPECT_TRUE( getPosition( *pScene, pairBox )._y < -1.0f );
}

/**
 * @brief [PhysicsScene2DTest] 트리거를 지나간 공은 시작 · 끝을 한 번씩 내고 막히지 않는다
 */
SW_TEST_CASE( PhysicsScene2DTest, TriggerBeginAndEndFireOnce )
{
    sw::unique_ptr<sw::IPhysicsScene2D> pScene = makeScene();
    SW_ASSERT_NOT_NULL( pScene.get() );
    sw::PhysicsBodyDesc2D triggerDesc;
    triggerDesc._listShape.push_back( makeBox( sw::float2{ 2.0f, 1.0f } ) );
    triggerDesc._type                   = sw::PhysicsBodyType::Static;
    triggerDesc._bTrigger               = true;
    const sw::PhysicsBodyHandle trigger = pScene->createBody( triggerDesc );
    const sw::PhysicsBodyHandle ball    = addBody( *pScene, makeCircle( 0.25f ), sw::float2{ 0.0f, 4.0f }, sw::PhysicsBodyType::Dynamic );

    uint32 beginCount = 0;
    uint32 stayCount  = 0;
    uint32 endCount   = 0;
    for ( uint32 stepIndex = 0; stepIndex < 180; ++stepIndex )
    {
        pScene->step( kStep2D );
        for ( const sw::PhysicsContactEvent2D& event : pScene->getContactEvents() )
        {
            SW_EXPECT_TRUE( event.involvesTrigger() );
            SW_EXPECT_TRUE( ( event._bodyA == trigger && event._bodyB == ball ) || ( event._bodyA == ball && event._bodyB == trigger ) );
            if ( event._phase == sw::PhysicsContactPhase::Begin )
                ++beginCount;
            else if ( event._phase == sw::PhysicsContactPhase::Stay )
                ++stayCount;
            else
                ++endCount;
        }
    }
    SW_EXPECT_EQUAL( 1u, beginCount );
    SW_EXPECT_EQUAL( 1u, endCount );
    SW_EXPECT_TRUE( stayCount > 0 );
    SW_EXPECT_TRUE( getPosition( *pScene, ball )._y < -2.0f );
}

/**
 * @brief [PhysicsScene2DTest] 바닥에 부딪힌 상자의 시작 이벤트는 충격량을 싣는다(스텝이 푼 매니폴드에서)
 */
SW_TEST_CASE( PhysicsScene2DTest, ContactBeginCarriesImpulse )
{
    sw::unique_ptr<sw::IPhysicsScene2D> pScene = makeScene();
    SW_ASSERT_NOT_NULL( pScene.get() );
    addFloor( *pScene );
    addBody( *pScene, makeBox( sw::float2{ 0.5f, 0.5f } ), sw::float2{ 0.0f, 5.5f }, sw::PhysicsBodyType::Dynamic );
    float32 beginImpulse = 0.0f;
    float32 stayImpulse  = 0.0f;
    uint32  beginCount   = 0;
    for ( uint32 stepIndex = 0; stepIndex < 120; ++stepIndex )
    {
        pScene->step( kStep2D );
        for ( const sw::PhysicsContactEvent2D& event : pScene->getContactEvents() )
        {
            if ( event._phase == sw::PhysicsContactPhase::Stay )
                stayImpulse = event._impulse;
            if ( event._phase != sw::PhysicsContactPhase::Begin )
                continue;
            ++beginCount;
            beginImpulse = event._impulse;
        }
    }
    SW_EXPECT_EQUAL( 1u, beginCount );
    // 질량 1000 kg, 약 9.9 m/s.
    SW_EXPECT_TRUE_MSG( beginImpulse > 5000.0f && beginImpulse < 15000.0f, "impact impulse ~ mass x speed" );
    // 쉬는 상자의 유지 충격량은 무게 × 스텝 = 1000 × 9.81 / 60 ≈ 163.5.
    SW_EXPECT_NEAR_EQUAL( 163.5f, stayImpulse, 25.0f );
}

/**
 * @brief [PhysicsScene2DTest] 회전 관절 한계가 막는다(한계를 끄면 한참 넘어간다)
 */
SW_TEST_CASE( PhysicsScene2DTest, HingeLimitHolds )
{
    float32 arrAngle[2] = { 0.0f, 0.0f };
    for ( uint32 caseIndex = 0; caseIndex < 2; ++caseIndex )
    {
        sw::unique_ptr<sw::IPhysicsScene2D> pScene = makeScene();
        SW_ASSERT_NOT_NULL( pScene.get() );
        const sw::PhysicsBodyHandle bar = addBody( *pScene, makeBox( sw::float2{ 0.5f, 0.05f } ), sw::float2{ 0.5f, 0.0f }, sw::PhysicsBodyType::Dynamic );
        sw::PhysicsJointDesc2D      joint;
        joint._bodyA                       = bar;
        joint._type                        = sw::PhysicsJointType::Hinge;
        joint._anchor                      = sw::float2{};
        joint._bLimitsEnabled              = caseIndex == 0;
        joint._minLimit                    = -0.3f;
        joint._maxLimit                    = 0.3f;
        const sw::PhysicsJointHandle hinge = pScene->createJoint( joint );
        SW_ASSERT_TRUE( pScene->isJointValid( hinge ) );
        stepFor( *pScene, 180 );
        arrAngle[caseIndex] = pScene->getJointPosition( hinge );
        SW_EXPECT_NEAR_EQUAL( 0.5f, getPosition( *pScene, bar ).getLength(), 0.02f );
    }
    SW_EXPECT_NEAR_EQUAL( 0.3f, sw::MathUtil::abs( arrAngle[0] ), 0.03f );
    SW_EXPECT_TRUE_MSG( sw::MathUtil::abs( arrAngle[1] ) > 1.0f, "without limits the bar swings far below" );

    // 2D 에는 Cone 이 없다 — 만들면 오류이고 무효 핸들이다.
    sw::unique_ptr<sw::IPhysicsScene2D> pScene = makeScene();
    const sw::PhysicsBodyHandle         body   = addBody( *pScene, makeCircle( 0.2f ), sw::float2{}, sw::PhysicsBodyType::Dynamic );
    sw::PhysicsJointDesc2D              cone;
    cone._bodyA = body;
    cone._type  = sw::PhysicsJointType::Cone;
    SW_TEST_DEFENSIVE_SCOPE( "2D Cone joints are rejected" );
    SW_EXPECT_FALSE( pScene->createJoint( cone ).isValid() );
}

/**
 * @brief [PhysicsScene2DTest] 2D 캐릭터는 작은 턱(0.1)을 둥근 바닥으로 타고 넘고 벽에 막혀 선다
 */
SW_TEST_CASE( PhysicsScene2DTest, CharacterClimbsStepAndStopsAtWall )
{
    sw::unique_ptr<sw::IPhysicsScene2D> pScene = makeScene();
    SW_ASSERT_NOT_NULL( pScene.get() );
    addFloor( *pScene );
    addBody( *pScene, makeBox( sw::float2{ 4.0f, 0.05f } ), sw::float2{ 5.0f, 0.05f }, sw::PhysicsBodyType::Static );
    addBody( *pScene, makeBox( sw::float2{ 0.5f, 2.0f } ), sw::float2{ 5.5f, 2.0f }, sw::PhysicsBodyType::Static );
    sw::PhysicsCharacterDesc2D desc;
    desc._radius                               = 0.3f;
    desc._halfHeight                           = 0.4f;
    desc._position                             = sw::float2{ 0.0f, 0.01f };
    const sw::PhysicsCharacterHandle character = pScene->createCharacter( desc );
    SW_ASSERT_TRUE( character.isValid() );
    sw::PhysicsCharacterState2D state;
    float32                     verticalSpeed = 0.0f;
    for ( uint32 stepIndex = 0; stepIndex < 300; ++stepIndex )
    {
        verticalSpeed = state._bGrounded ? -0.5f : verticalSpeed - 9.81f * kStep2D;
        state         = pScene->moveCharacter( character, sw::float2{ 2.0f, verticalSpeed }, kStep2D );
    }
    SW_EXPECT_TRUE_MSG( state._position._x > 4.5f && state._position._x < 4.75f, "stops against the wall" );
    SW_EXPECT_NEAR_EQUAL( 0.1f, state._position._y, 0.05f );
    SW_EXPECT_TRUE( state._bGrounded );
}

/**
 * @brief [PhysicsScene2DTest] 같은 입력이면 같은 자리다(비트까지)
 */
SW_TEST_CASE( PhysicsScene2DTest, StepIsDeterministic )
{
    sw::vector<sw::float2> arrListPosition[2];
    for ( uint32 runIndex = 0; runIndex < 2; ++runIndex )
    {
        sw::unique_ptr<sw::IPhysicsScene2D> pScene = makeScene();
        SW_ASSERT_NOT_NULL( pScene.get() );
        addFloor( *pScene );
        sw::vector<sw::PhysicsBodyHandle> listBody;
        for ( uint32 bodyIndex = 0; bodyIndex < 24; ++bodyIndex )
        {
            const float32 x = static_cast<float32>( bodyIndex % 4 ) * 0.7f - 1.0f;
            const float32 y = 1.0f + static_cast<float32>( bodyIndex / 4 ) * 1.1f;
            listBody.push_back(
                addBody( *pScene, ( bodyIndex % 2 ) == 0 ? makeBox( sw::float2{ 0.4f, 0.4f } ) : makeCircle( 0.4f ), sw::float2{ x, y }, sw::PhysicsBodyType::Dynamic ) );
        }
        stepFor( *pScene, 180 );
        for ( const sw::PhysicsBodyHandle& body : listBody )
        {
            arrListPosition[runIndex].push_back( getPosition( *pScene, body ) );
        }
    }
    for ( size_t bodyIndex = 0; bodyIndex < arrListPosition[0].size(); ++bodyIndex )
    {
        SW_EXPECT_TRUE( arrListPosition[0][bodyIndex]._x == arrListPosition[1][bodyIndex]._x );
        SW_EXPECT_TRUE( arrListPosition[0][bodyIndex]._y == arrListPosition[1][bodyIndex]._y );
    }
}

/**
 * @brief [PhysicsScene2DTest] 지어 둔 셰이프를 묶은 컴파운드 — 자식의 로컬 자리가 그대로 들고, 자식을 지워도 산다. 지운 자식으로는 만들지 않는다
 */
SW_TEST_CASE( PhysicsScene2DTest, CompoundOfBuiltShapesKeepsChildOffsets )
{
    sw::unique_ptr<sw::IPhysicsScene2D> pScene = makeScene();
    SW_ASSERT_NOT_NULL( pScene.get() );
    const sw::PhysicsShapeDesc2D lower       = makeBox( sw::float2{ 0.5f, 0.5f } );
    sw::PhysicsShapeDesc2D       upper       = makeBox( sw::float2{ 0.5f, 0.5f } );
    upper._localPosition                     = sw::float2{ 0.0f, 1.0f };
    const sw::PhysicsShapeHandle lowerShape  = pScene->createShape( sw::span<const sw::PhysicsShapeDesc2D>{ &lower, 1 }, sw::hashed_string{} );
    const sw::PhysicsShapeHandle upperShape  = pScene->createShape( sw::span<const sw::PhysicsShapeDesc2D>{ &upper, 1 }, sw::hashed_string{} );
    const sw::PhysicsShapeHandle arrChild[2] = { lowerShape, upperShape };
    const sw::PhysicsShapeHandle compound    = pScene->createCompoundShape( sw::span<const sw::PhysicsShapeHandle>{ arrChild, 2 } );
    SW_ASSERT_TRUE( compound.isValid() );
    pScene->destroyShape( lowerShape );
    pScene->destroyShape( upperShape );

    sw::PhysicsBodyDesc2D desc;
    desc._sharedShape = compound;
    desc._type        = sw::PhysicsBodyType::Static;
    SW_ASSERT_TRUE( pScene->createBody( desc ).isValid() );
    sw::PhysicsQueryFilter filter;
    sw::PhysicsCastHit2D   hit;
    SW_ASSERT_TRUE( pScene->raycast( sw::float2{ 0.0f, 10.0f }, sw::float2{ 0.0f, -1.0f }, 100.0f, filter, hit ) );
    SW_EXPECT_NEAR_EQUAL( 8.5f, hit._distance, 1e-3f ); // 위 자식의 윗면(y = 1.5)
    SW_ASSERT_TRUE( pScene->raycast( sw::float2{ 0.0f, -10.0f }, sw::float2{ 0.0f, 1.0f }, 100.0f, filter, hit ) );
    SW_EXPECT_NEAR_EQUAL( 9.5f, hit._distance, 1e-3f ); // 아래 자식의 밑면(y = -0.5)

    SW_TEST_DEFENSIVE_SCOPE( "a compound of a destroyed shape is rejected" );
    const sw::PhysicsShapeHandle arrStale[1] = { lowerShape };
    SW_EXPECT_FALSE( pScene->createCompoundShape( sw::span<const sw::PhysicsShapeHandle>{ arrStale, 1 } ).isValid() );
}
