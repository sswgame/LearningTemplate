#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/PhysicsSettings.h"
#include "Engine/Physics/PhysicsSystem.h"

#include "TestFramework/TestFramework.h"

// 3D 물리 씬(Jolt 백엔드)을 인터페이스로만 잰다 — 바디 · 재질 · 질의 · 레이어 · 트리거 · 관절 · 캐릭터 · 결정성 · 대량 생성 · 충격량.

namespace
{
    constexpr float32 kStep3D = 1.0f / 60.0f;

    /** @brief 시험용 설정 — 레이어 Default(0) · Ghost(1, Default 와 부딪히지 않는다) · Solid(2), 재질 Default · Bouncy(반발 0.8) · Dead(반발 0). */
    sw::PhysicsSettings makeSettings()
    {
        sw::PhysicsSettings settings;
        sw::PhysicsLayerDef defaultLayer;
        defaultLayer._name = sw::hashed_string( "Default" );
        defaultLayer._listCollidesWith.push_back( sw::hashed_string( "Default" ) );
        defaultLayer._listCollidesWith.push_back( sw::hashed_string( "Solid" ) );
        sw::PhysicsLayerDef ghostLayer;
        ghostLayer._name = sw::hashed_string( "Ghost" );
        ghostLayer._listCollidesWith.push_back( sw::hashed_string( "Solid" ) );
        sw::PhysicsLayerDef solidLayer;
        solidLayer._name    = sw::hashed_string( "Solid" );
        settings._listLayer = { defaultLayer, ghostLayer, solidLayer };

        sw::PhysicsMaterialDef defaultMaterial;
        defaultMaterial._name = sw::hashed_string( "Default" );
        sw::PhysicsMaterialDef bouncy;
        bouncy._name        = sw::hashed_string( "Bouncy" );
        bouncy._restitution = 0.8f;
        sw::PhysicsMaterialDef dead;
        dead._name             = sw::hashed_string( "Dead" );
        dead._restitution      = 0.0f;
        settings._listMaterial = { defaultMaterial, bouncy, dead };
        return settings;
    }

    sw::unique_ptr<sw::IPhysicsScene3D> makeScene()
    {
        return sw::engine::getPhysicsSystem().createScene3D( makeSettings() );
    }

    sw::PhysicsShapeDesc3D makeBox( const sw::float3& halfExtents )
    {
        sw::PhysicsShapeDesc3D shape;
        shape._type        = sw::PhysicsShapeType3D::Box;
        shape._halfExtents = halfExtents;
        return shape;
    }

    sw::PhysicsShapeDesc3D makeSphere( float32 radius )
    {
        sw::PhysicsShapeDesc3D shape;
        shape._type   = sw::PhysicsShapeType3D::Sphere;
        shape._radius = radius;
        return shape;
    }

    sw::PhysicsBodyHandle addBody( sw::IPhysicsScene3D& scene, const sw::PhysicsShapeDesc3D& shape, const sw::float3& position, sw::PhysicsBodyType type,
                                   uint8 layer = 0, const utf8* pMaterial = "", uint64 userData = 0 )
    {
        sw::PhysicsBodyDesc3D desc;
        desc._listShape.push_back( shape );
        desc._position = position;
        desc._type     = type;
        desc._layer    = layer;
        desc._material = sw::hashed_string( pMaterial );
        desc._userData = userData;
        return scene.createBody( desc );
    }

    /** @brief 바닥 — 윗면이 y = 0 인 넓은 정적 상자입니다. */
    sw::PhysicsBodyHandle addFloor( sw::IPhysicsScene3D& scene, uint8 layer = 0, const utf8* pMaterial = "" )
    {
        return addBody( scene, makeBox( sw::float3{ 20.0f, 0.5f, 20.0f } ), sw::float3{ 0.0f, -0.5f, 0.0f }, sw::PhysicsBodyType::Static, layer, pMaterial );
    }

    sw::float3 getPosition( const sw::IPhysicsScene3D& scene, sw::PhysicsBodyHandle body )
    {
        sw::float3     position{};
        sw::quaternion rotation{};
        (void)scene.getBodyTransform( body, position, rotation );
        return position;
    }

    void stepFor( sw::IPhysicsScene3D& scene, uint32 stepCount )
    {
        for ( uint32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
            scene.step( kStep3D );
    }
} // namespace

/**
 * @brief [PhysicsScene3DTest] 떨어진 상자는 바닥 위(반 높이)에서 멈추고 잠든다
 */
SW_TEST_CASE( PhysicsScene3DTest, BoxComesToRestOnFloor )
{
    sw::unique_ptr<sw::IPhysicsScene3D> pScene = makeScene();
    SW_ASSERT_NOT_NULL( pScene.get() );
    addFloor( *pScene );
    const sw::PhysicsBodyHandle box = addBody( *pScene, makeBox( sw::float3{ 0.5f, 0.5f, 0.5f } ), sw::float3{ 0.0f, 3.0f, 0.0f }, sw::PhysicsBodyType::Dynamic );
    SW_ASSERT_TRUE( box.isValid() );

    stepFor( *pScene, 60 );
    SW_EXPECT_TRUE( getPosition( *pScene, box )._y < 2.0f ); // 떨어지고 있다
    stepFor( *pScene, 240 );
    SW_EXPECT_NEAR_EQUAL( 0.5f, getPosition( *pScene, box )._y, 0.03f );
    SW_EXPECT_TRUE( pScene->getLinearVelocity( box ).getLength() < 0.05f );
    SW_EXPECT_NEAR_EQUAL( 1000.0f, pScene->getBodyMass( box ), 1.0f ); // 1 m^3 × 밀도 1000
}

/**
 * @brief [PhysicsScene3DTest] 반발이 큰 재질은 튀어 오르고, 반발 0 은 튀지 않는다(셰이프 재질이 접촉에 섞인다)
 */
SW_TEST_CASE( PhysicsScene3DTest, RestitutionBouncesByMaterial )
{
    float32     arrPeak[2]     = { 0.0f, 0.0f };
    const utf8* arrMaterial[2] = { "Bouncy", "Dead" };
    for ( uint32 caseIndex = 0; caseIndex < 2; ++caseIndex )
    {
        sw::unique_ptr<sw::IPhysicsScene3D> pScene = makeScene();
        SW_ASSERT_NOT_NULL( pScene.get() );
        addFloor( *pScene, 0, arrMaterial[caseIndex] );
        const sw::PhysicsBodyHandle ball =
            addBody( *pScene, makeSphere( 0.5f ), sw::float3{ 0.0f, 5.5f, 0.0f }, sw::PhysicsBodyType::Dynamic, 0, arrMaterial[caseIndex] );
        // 바닥에 닿을 때까지 떨어진 뒤, 그다음 1 초 동안의 가장 높은 자리.
        bool bTouched = false;
        for ( uint32 stepIndex = 0; stepIndex < 240; ++stepIndex )
        {
            pScene->step( kStep3D );
            const float32 height = getPosition( *pScene, ball )._y;
            if ( height < 0.55f )
                bTouched = true;
            if ( bTouched && height > arrPeak[caseIndex] )
                arrPeak[caseIndex] = height;
        }
        SW_EXPECT_TRUE( bTouched );
    }
    // 떨어진 높이 5 m 에 반발 0.8 이면 약 0.64 × 5 ≈ 3 m 다시 오른다.
    SW_EXPECT_TRUE_MSG( arrPeak[0] > 2.0f, "a bouncy ball rises again" );
    SW_EXPECT_TRUE_MSG( arrPeak[1] < 0.7f, "a dead ball stays down" );
}

/**
 * @brief [PhysicsScene3DTest] 레이캐스트는 닿은 거리 · 법선 · 바디 · 사용자 값을 돌려주고, 마스크 밖 레이어는 지나친다
 */
SW_TEST_CASE( PhysicsScene3DTest, RaycastReportsHitDistance )
{
    sw::unique_ptr<sw::IPhysicsScene3D> pScene = makeScene();
    SW_ASSERT_NOT_NULL( pScene.get() );
    const sw::PhysicsBodyHandle target =
        addBody( *pScene, makeBox( sw::float3{ 0.5f, 0.5f, 0.5f } ), sw::float3{ 5.0f, 0.0f, 0.0f }, sw::PhysicsBodyType::Static, 2, "", 77 );

    sw::PhysicsQueryFilter filter;
    sw::PhysicsCastHit3D   hit;
    SW_ASSERT_TRUE( pScene->raycast( sw::float3{}, sw::float3{ 1.0f, 0.0f, 0.0f }, 100.0f, filter, hit ) );
    SW_EXPECT_NEAR_EQUAL( 4.5f, hit._distance, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( -1.0f, hit._normal._x, 1e-3f );
    SW_EXPECT_TRUE( hit._body == target );
    SW_EXPECT_EQUAL( static_cast<uint64>( 77 ), hit._userData );

    filter._layerMask = 1u << 0; // Default 만 — 대상은 Solid(2)
    SW_EXPECT_FALSE( pScene->raycast( sw::float3{}, sw::float3{ 1.0f, 0.0f, 0.0f }, 100.0f, filter, hit ) );
    filter._layerMask = 1u << 2;
    SW_EXPECT_FALSE( pScene->raycast( sw::float3{}, sw::float3{ 1.0f, 0.0f, 0.0f }, 4.0f, filter, hit ) ); // 닿기 전에 끝난다

    // 셰이프 캐스트: 반지름 0.5 구가 x 로 가면 표면까지 4.5 - 0.5 = 4 m 에서 닿는다.
    filter._layerMask = sw::MathUtil::kMaxUInt32;
    SW_ASSERT_TRUE( pScene->shapeCast( makeSphere( 0.5f ), sw::float3{}, sw::quaternion{}, sw::float3{ 1.0f, 0.0f, 0.0f }, 100.0f, filter, hit ) );
    SW_EXPECT_NEAR_EQUAL( 4.0f, hit._distance, 1e-2f );
    SW_EXPECT_TRUE( hit._body == target );

    // 겹침: 대상과 겹치는 상자는 대상을 한 번, 먼 상자는 아무것도.
    sw::vector<sw::PhysicsBodyHandle> listFound;
    SW_EXPECT_EQUAL( 1u, pScene->overlapShape( makeBox( sw::float3{ 1.0f, 1.0f, 1.0f } ), sw::float3{ 5.5f, 0.0f, 0.0f }, sw::quaternion{}, filter, listFound ) );
    SW_EXPECT_EQUAL( 0u, pScene->overlapShape( makeBox( sw::float3{ 1.0f, 1.0f, 1.0f } ), sw::float3{ -5.0f, 0.0f, 0.0f }, sw::quaternion{}, filter, listFound ) );
}

/**
 * @brief [PhysicsScene3DTest] 레이어 표가 막은 쌍은 지나치고, 허락한 쌍은 막는다(같은 바닥 · 같은 상자, 레이어만 다르다)
 */
SW_TEST_CASE( PhysicsScene3DTest, LayerFilterBlocksPair )
{
    sw::unique_ptr<sw::IPhysicsScene3D> pScene = makeScene();
    SW_ASSERT_NOT_NULL( pScene.get() );
    addFloor( *pScene, 0 ); // Default
    const sw::PhysicsBodyHandle ghost = addBody( *pScene, makeBox( sw::float3{ 0.5f, 0.5f, 0.5f } ), sw::float3{ -3.0f, 2.0f, 0.0f }, sw::PhysicsBodyType::Dynamic, 1 );
    const sw::PhysicsBodyHandle solid = addBody( *pScene, makeBox( sw::float3{ 0.5f, 0.5f, 0.5f } ), sw::float3{ 3.0f, 2.0f, 0.0f }, sw::PhysicsBodyType::Dynamic, 0 );
    stepFor( *pScene, 120 );
    SW_EXPECT_TRUE_MSG( getPosition( *pScene, ghost )._y < -5.0f, "Ghost does not collide with Default - it falls through the floor" );
    SW_EXPECT_NEAR_EQUAL( 0.5f, getPosition( *pScene, solid )._y, 0.03f );

    // 쌍 하나만 끈다 — 같은 레이어여도 그 두 바디는 지나친다.
    const sw::PhysicsBodyHandle floor2  = addBody( *pScene, makeBox( sw::float3{ 2.0f, 0.5f, 2.0f } ), sw::float3{ 30.0f, -0.5f, 0.0f }, sw::PhysicsBodyType::Static );
    const sw::PhysicsBodyHandle pairBox = addBody( *pScene, makeBox( sw::float3{ 0.5f, 0.5f, 0.5f } ), sw::float3{ 30.0f, 2.0f, 0.0f }, sw::PhysicsBodyType::Dynamic );
    pScene->setPairCollision( floor2, pairBox, false );
    stepFor( *pScene, 120 );
    SW_EXPECT_TRUE( getPosition( *pScene, pairBox )._y < -1.0f );
}

/**
 * @brief [PhysicsScene3DTest] 트리거를 지나간 공은 시작 · 끝을 한 번씩 내고, 그 사이에는 유지만 낸다. 막지 않는다
 */
SW_TEST_CASE( PhysicsScene3DTest, TriggerBeginAndEndFireOnce )
{
    sw::unique_ptr<sw::IPhysicsScene3D> pScene = makeScene();
    SW_ASSERT_NOT_NULL( pScene.get() );
    sw::PhysicsBodyDesc3D triggerDesc;
    triggerDesc._listShape.push_back( makeBox( sw::float3{ 2.0f, 1.0f, 2.0f } ) );
    triggerDesc._type                   = sw::PhysicsBodyType::Static;
    triggerDesc._bTrigger               = true;
    triggerDesc._userData               = 1;
    const sw::PhysicsBodyHandle trigger = pScene->createBody( triggerDesc );
    const sw::PhysicsBodyHandle ball    = addBody( *pScene, makeSphere( 0.25f ), sw::float3{ 0.0f, 4.0f, 0.0f }, sw::PhysicsBodyType::Dynamic, 0, "", 2 );

    uint32 beginCount = 0;
    uint32 stayCount  = 0;
    uint32 endCount   = 0;
    for ( uint32 stepIndex = 0; stepIndex < 180; ++stepIndex )
    {
        pScene->step( kStep3D );
        for ( const sw::PhysicsContactEvent3D& event : pScene->getContactEvents() )
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
    SW_EXPECT_TRUE( getPosition( *pScene, ball )._y < -2.0f ); // 트리거는 막지 않는다
}

/**
 * @brief [PhysicsScene3DTest] 떨어져 바닥에 부딪힌 상자의 시작 이벤트는 충격량(대략 질량 × 닿는 속도)을 싣는다
 */
SW_TEST_CASE( PhysicsScene3DTest, ContactBeginCarriesImpulse )
{
    sw::unique_ptr<sw::IPhysicsScene3D> pScene = makeScene();
    SW_ASSERT_NOT_NULL( pScene.get() );
    addFloor( *pScene, 0, "Dead" );
    const sw::PhysicsBodyHandle box =
        addBody( *pScene, makeBox( sw::float3{ 0.5f, 0.5f, 0.5f } ), sw::float3{ 0.0f, 5.5f, 0.0f }, sw::PhysicsBodyType::Dynamic, 0, "Dead", 9 );
    float32 beginImpulse = 0.0f;
    float32 stayImpulse  = 0.0f;
    uint32  beginCount   = 0;
    for ( uint32 stepIndex = 0; stepIndex < 120; ++stepIndex )
    {
        pScene->step( kStep3D );
        for ( const sw::PhysicsContactEvent3D& event : pScene->getContactEvents() )
        {
            if ( event._phase == sw::PhysicsContactPhase::Stay )
                stayImpulse = event._impulse;
            if ( event._phase != sw::PhysicsContactPhase::Begin )
                continue;
            ++beginCount;
            beginImpulse = event._impulse;
            SW_EXPECT_FALSE( event.involvesTrigger() );
            SW_EXPECT_TRUE( event._userDataA == 9 || event._userDataB == 9 );
        }
    }
    (void)box;
    SW_EXPECT_EQUAL( 1u, beginCount );
    // 5 m 를 떨어지면 약 9.9 m/s, 질량 1000 kg → 약 9900 N·s. 어림값이라 넓게 본다.
    SW_EXPECT_TRUE_MSG( beginImpulse > 5000.0f && beginImpulse < 15000.0f, "impact impulse ~ mass x speed" );
    // 쉬는 상자의 유지 충격량은 무게 × 스텝 = 1000 × 9.81 / 60 ≈ 163.5.
    SW_EXPECT_NEAR_EQUAL( 163.5f, stayImpulse, 25.0f );
}

/**
 * @brief [PhysicsScene3DTest] 힌지 한계가 막는다 — 중력이 끌어도 각이 한계 안에 머문다(한계를 끄면 한참 넘어간다)
 */
SW_TEST_CASE( PhysicsScene3DTest, HingeLimitHolds )
{
    float32 arrAngle[2] = { 0.0f, 0.0f };
    for ( uint32 caseIndex = 0; caseIndex < 2; ++caseIndex )
    {
        sw::unique_ptr<sw::IPhysicsScene3D> pScene = makeScene();
        SW_ASSERT_NOT_NULL( pScene.get() );
        // 원점의 Z 축 힌지에 1 m 옆으로 뻗은 막대 — 중력이 아래로 돌린다.
        const sw::PhysicsBodyHandle bar =
            addBody( *pScene, makeBox( sw::float3{ 0.5f, 0.05f, 0.05f } ), sw::float3{ 0.5f, 0.0f, 0.0f }, sw::PhysicsBodyType::Dynamic );
        sw::PhysicsJointDesc3D joint;
        joint._bodyA                       = bar;
        joint._type                        = sw::PhysicsJointType::Hinge;
        joint._anchor                      = sw::float3{};
        joint._axis                        = sw::float3{ 0.0f, 0.0f, 1.0f };
        joint._bLimitsEnabled              = caseIndex == 0;
        joint._minLimit                    = -0.3f;
        joint._maxLimit                    = 0.3f;
        const sw::PhysicsJointHandle hinge = pScene->createJoint( joint );
        SW_ASSERT_TRUE( pScene->isJointValid( hinge ) );
        stepFor( *pScene, 180 );
        arrAngle[caseIndex] = pScene->getJointPosition( hinge );
        // 막대 끝이 원점에서 1 m 안에 머문다(관절이 붙들고 있다).
        SW_EXPECT_NEAR_EQUAL( 0.5f, getPosition( *pScene, bar ).getLength(), 0.02f );
    }
    // 부호는 바디 순서에 달렸다 — 크기를 본다.
    SW_EXPECT_NEAR_EQUAL( 0.3f, sw::MathUtil::abs( arrAngle[0] ), 0.03f );
    SW_EXPECT_TRUE_MSG( sw::MathUtil::abs( arrAngle[1] ) > 1.0f, "without limits the bar swings far below" );
}

/**
 * @brief [PhysicsScene3DTest] 캐릭터는 턱 높이(0.3) 아래의 계단(0.2)을 오르고, 벽에 막혀 선다. 턱 높이보다 높은 계단(0.6)은 오르지 못한다
 */
SW_TEST_CASE( PhysicsScene3DTest, CharacterClimbsStepAndStopsAtWall )
{
    const float32 arrStep[2] = { 0.2f, 0.6f };
    for ( uint32 caseIndex = 0; caseIndex < 2; ++caseIndex )
    {
        sw::unique_ptr<sw::IPhysicsScene3D> pScene = makeScene();
        SW_ASSERT_NOT_NULL( pScene.get() );
        addFloor( *pScene );
        const float32 stepHeight = arrStep[caseIndex];
        // x = 1 부터 시작하는 단, 그 위 x = 5 에 벽.
        addBody( *pScene, makeBox( sw::float3{ 4.0f, stepHeight * 0.5f, 3.0f } ), sw::float3{ 5.0f, stepHeight * 0.5f, 0.0f }, sw::PhysicsBodyType::Static );
        addBody( *pScene, makeBox( sw::float3{ 0.5f, 2.0f, 3.0f } ), sw::float3{ 5.5f, 2.0f, 0.0f }, sw::PhysicsBodyType::Static );

        sw::PhysicsCharacterDesc3D desc;
        desc._radius                               = 0.3f;
        desc._halfHeight                           = 0.6f;
        desc._stepHeight                           = 0.3f;
        const sw::PhysicsCharacterHandle character = pScene->createCharacter( desc );
        SW_ASSERT_TRUE( character.isValid() );
        sw::PhysicsCharacterState3D state;
        float32                     verticalSpeed = 0.0f;
        for ( uint32 stepIndex = 0; stepIndex < 300; ++stepIndex )
        {
            verticalSpeed = state._bGrounded ? 0.0f : verticalSpeed - 9.81f * kStep3D;
            state         = pScene->moveCharacter( character, sw::float3{ 2.0f, verticalSpeed, 0.0f }, kStep3D );
        }
        if ( caseIndex == 0 )
        {
            SW_EXPECT_NEAR_EQUAL( 0.2f, state._position._y, 0.05f );
            SW_EXPECT_TRUE_MSG( state._position._x > 4.5f && state._position._x < 4.75f, "stops against the wall (wall face 5 - radius 0.3)" );
            SW_EXPECT_TRUE( state._bGrounded );
        }
        else
        {
            SW_EXPECT_NEAR_EQUAL( 0.0f, state._position._y, 0.05f );
            SW_EXPECT_TRUE_MSG( state._position._x < 0.75f, "a 0.6 m step is a wall for a 0.3 m step height" );
        }
    }
}

/**
 * @brief [PhysicsScene3DTest] 같은 입력이면 같은 자리다 — 두 씬에 같은 순서로 같은 바디를 넣고 같은 스텝을 돌리면 비트까지 같다
 */
SW_TEST_CASE( PhysicsScene3DTest, StepIsDeterministic )
{
    sw::vector<sw::float3> arrListPosition[2];
    for ( uint32 runIndex = 0; runIndex < 2; ++runIndex )
    {
        sw::unique_ptr<sw::IPhysicsScene3D> pScene = makeScene();
        SW_ASSERT_NOT_NULL( pScene.get() );
        addFloor( *pScene );
        sw::vector<sw::PhysicsBodyHandle> listBody;
        for ( uint32 bodyIndex = 0; bodyIndex < 24; ++bodyIndex )
        {
            const float32 x = static_cast<float32>( bodyIndex % 4 ) * 0.7f - 1.0f;
            const float32 y = 1.0f + static_cast<float32>( bodyIndex / 4 ) * 1.1f;
            const float32 z = static_cast<float32>( bodyIndex % 3 ) * 0.3f;
            listBody.push_back( addBody( *pScene, ( bodyIndex % 2 ) == 0 ? makeBox( sw::float3{ 0.4f, 0.4f, 0.4f } ) : makeSphere( 0.4f ), sw::float3{ x, y, z },
                                         sw::PhysicsBodyType::Dynamic ) );
        }
        stepFor( *pScene, 180 );
        for ( const sw::PhysicsBodyHandle& body : listBody )
            arrListPosition[runIndex].push_back( getPosition( *pScene, body ) );
    }
    SW_ASSERT_EQUAL( arrListPosition[0].size(), arrListPosition[1].size() );
    for ( size_t bodyIndex = 0; bodyIndex < arrListPosition[0].size(); ++bodyIndex )
    {
        SW_EXPECT_TRUE( arrListPosition[0][bodyIndex]._x == arrListPosition[1][bodyIndex]._x );
        SW_EXPECT_TRUE( arrListPosition[0][bodyIndex]._y == arrListPosition[1][bodyIndex]._y );
        SW_EXPECT_TRUE( arrListPosition[0][bodyIndex]._z == arrListPosition[1][bodyIndex]._z );
    }
}

/**
 * @brief [PhysicsScene3DTest] 미리 지은 컴파운드 셰이프로 바디를 한 번에 만들고 한 번에 지운다 — 수 · 핸들 세대 · 접촉 끝
 */
SW_TEST_CASE( PhysicsScene3DTest, BulkCompoundBodiesCreateAndDestroy )
{
    sw::unique_ptr<sw::IPhysicsScene3D> pScene = makeScene();
    SW_ASSERT_NOT_NULL( pScene.get() );
    addFloor( *pScene );
    sw::vector<sw::PhysicsShapeDesc3D> listPart;
    listPart.push_back( makeBox( sw::float3{ 0.2f, 0.2f, 0.2f } ) );
    sw::PhysicsShapeDesc3D knob = makeSphere( 0.15f );
    knob._localPosition         = sw::float3{ 0.0f, 0.3f, 0.0f };
    listPart.push_back( knob );
    const sw::PhysicsShapeHandle shape = pScene->createShape( sw::span<const sw::PhysicsShapeDesc3D>{ listPart.data(), listPart.size() }, sw::hashed_string{} );
    SW_ASSERT_TRUE( shape.isValid() );

    sw::vector<sw::PhysicsBodyDesc3D> listDesc( 200 );
    for ( size_t bodyIndex = 0; bodyIndex < listDesc.size(); ++bodyIndex )
    {
        listDesc[bodyIndex]._sharedShape = shape;
        listDesc[bodyIndex]._position    = sw::float3{ static_cast<float32>( bodyIndex % 20 ) * 0.6f - 6.0f, 0.3f, static_cast<float32>( bodyIndex / 20 ) * 0.6f - 3.0f };
    }
    sw::vector<sw::PhysicsBodyHandle> listBody;
    pScene->createBodies( sw::span<const sw::PhysicsBodyDesc3D>{ listDesc.data(), listDesc.size() }, listBody );
    SW_ASSERT_EQUAL( listDesc.size(), listBody.size() );
    SW_EXPECT_EQUAL( 201u, pScene->getBodyCount() );
    stepFor( *pScene, 30 );

    pScene->destroyBodies( sw::span<const sw::PhysicsBodyHandle>{ listBody.data(), listBody.size() } );
    SW_EXPECT_EQUAL( 1u, pScene->getBodyCount() );
    SW_EXPECT_FALSE( pScene->isBodyValid( listBody.front() ) );
    // 지운 바디의 접촉은 다음 스텝에 끝난다.
    pScene->step( kStep3D );
    uint32 endCount = 0;
    for ( const sw::PhysicsContactEvent3D& event : pScene->getContactEvents() )
    {
        if ( event._phase == sw::PhysicsContactPhase::End )
            ++endCount;
    }
    SW_EXPECT_TRUE( endCount > 0 );
    // 슬롯을 다시 써도 옛 핸들은 무효다(세대).
    const sw::PhysicsBodyHandle reused = addBody( *pScene, makeSphere( 0.2f ), sw::float3{ 0.0f, 3.0f, 0.0f }, sw::PhysicsBodyType::Dynamic );
    SW_EXPECT_TRUE( pScene->isBodyValid( reused ) );
    SW_EXPECT_FALSE( pScene->isBodyValid( listBody.back() ) );
    SW_EXPECT_TRUE( reused.getSlot().index() == listBody.back().getSlot().index() );
}

SW_TEST_CASE( PhysicsScene3DTest, StartingAngularVelocityIsClampedToBodyLimit )
{
    // 상한을 넘는 시작 각속도는 만들 때 줄인다 — 갈라진 파괴 덩어리가 상한에 붙어 돌던 부모의 운동을 이을 때 생긴다(Jolt 는 단언하고 값을 그대로 둔다).
    sw::unique_ptr<sw::IPhysicsScene3D> pScene = makeScene();
    SW_ASSERT_NOT_NULL( pScene.get() );
    sw::PhysicsBodyDesc3D desc;
    desc._listShape.push_back( makeSphere( 0.1f ) );
    desc._position                   = sw::float3{ 0.0f, 3.0f, 0.0f };
    desc._type                       = sw::PhysicsBodyType::Dynamic;
    desc._angularVelocity            = sw::float3{ 1000.0f, 0.0f, 0.0f };
    const sw::PhysicsBodyHandle body = pScene->createBody( desc );
    SW_ASSERT_TRUE( body.isValid() );
    const float32 speed = pScene->getAngularVelocity( body ).getLength();
    SW_EXPECT_TRUE( speed > 1.0f );                               // 방향은 남긴다
    SW_EXPECT_TRUE( speed <= 0.25f * sw::MathUtil::kPi * 60.0f ); // Jolt 기본 상한(초당 15 바퀴)
}

/**
 * @brief [PhysicsScene3DTest] 지어 둔 셰이프를 묶은 컴파운드 — 자식의 로컬 자리가 그대로 들고, 자식을 지워도 산다. 지운 자식으로는 만들지 않는다
 */
SW_TEST_CASE( PhysicsScene3DTest, CompoundOfBuiltShapesKeepsChildOffsets )
{
    sw::unique_ptr<sw::IPhysicsScene3D> pScene = makeScene();
    SW_ASSERT_NOT_NULL( pScene.get() );
    const sw::PhysicsShapeDesc3D lower       = makeBox( sw::float3{ 0.5f, 0.5f, 0.5f } );
    sw::PhysicsShapeDesc3D       upper       = makeBox( sw::float3{ 0.5f, 0.5f, 0.5f } );
    upper._localPosition                     = sw::float3{ 0.0f, 1.0f, 0.0f };
    const sw::PhysicsShapeHandle lowerShape  = pScene->createShape( sw::span<const sw::PhysicsShapeDesc3D>{ &lower, 1 }, sw::hashed_string{} );
    const sw::PhysicsShapeHandle upperShape  = pScene->createShape( sw::span<const sw::PhysicsShapeDesc3D>{ &upper, 1 }, sw::hashed_string{} );
    const sw::PhysicsShapeHandle arrChild[2] = { lowerShape, upperShape };
    const sw::PhysicsShapeHandle compound    = pScene->createCompoundShape( sw::span<const sw::PhysicsShapeHandle>{ arrChild, 2 } );
    SW_ASSERT_TRUE( compound.isValid() );
    pScene->destroyShape( lowerShape );
    pScene->destroyShape( upperShape );

    sw::PhysicsBodyDesc3D desc;
    desc._sharedShape = compound;
    desc._type        = sw::PhysicsBodyType::Static;
    SW_ASSERT_TRUE( pScene->createBody( desc ).isValid() );
    sw::PhysicsQueryFilter filter;
    sw::PhysicsCastHit3D   hit;
    SW_ASSERT_TRUE( pScene->raycast( sw::float3{ 0.0f, 10.0f, 0.0f }, sw::float3{ 0.0f, -1.0f, 0.0f }, 100.0f, filter, hit ) );
    SW_EXPECT_NEAR_EQUAL( 8.5f, hit._distance, 1e-3f ); // 위 자식의 윗면(y = 1.5)
    SW_ASSERT_TRUE( pScene->raycast( sw::float3{ 0.0f, -10.0f, 0.0f }, sw::float3{ 0.0f, 1.0f, 0.0f }, 100.0f, filter, hit ) );
    SW_EXPECT_NEAR_EQUAL( 9.5f, hit._distance, 1e-3f ); // 아래 자식의 밑면(y = -0.5)

    SW_TEST_DEFENSIVE_SCOPE( "a compound of a destroyed shape is rejected" );
    const sw::PhysicsShapeHandle arrStale[1] = { lowerShape };
    SW_EXPECT_FALSE( pScene->createCompoundShape( sw::span<const sw::PhysicsShapeHandle>{ arrStale, 1 } ).isValid() );
}
