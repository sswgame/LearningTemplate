#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/Skeleton.h"
#include "Engine/Destruction/FractureAsset.h"
#include "Engine/Destruction/FractureComponent.h"
#include "Engine/Destruction/MeshFracture.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/Component/Physics/RigidBodyComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ScenePhysics.h"

#include "EngineTest/DestructionTestUtil.h"

#include "TestFramework/TestFramework.h"

// FractureComponentTest — 온전할 때는 메시 하나 · 강체 하나, 첫 파괴에 조각(정적 · 동적 바디 + 스킨드 메시 둘)으로 바뀌고, 떨어진 것은 떨어지고,
// 쉬면 바디를 빼고 정적 그림으로 구워지며, 같은 사건이면 다른 매니저에서도 같은 상태다.

namespace
{
    struct TestFractureComponentInternal
    {
        static constexpr float32 kFrame = 1.0f / 60.0f;

        /** @brief 4 × 3 × 0.3 벽(바닥 가운데가 원점)을 벽돌 8 × 6 · 레벨 [3, 12] 로 쪼개 임시 `.fracture` 로 씁니다. 경로를 돌려줍니다. */
        static sw::string writeWallFracture( const utf8* pName )
        {
            const sw::vector<sw::RHIVertex> listBox = test::DestructionTestUtil::makeBox( sw::float3{ 2.0f, 1.5f, 0.15f }, sw::float3{ 0.0f, 1.5f, 0.0f } );
            sw::FractureSettings            settings;
            settings._pattern          = sw::FracturePattern::Slices;
            settings._arrSliceCount[0] = 8;
            settings._arrSliceCount[1] = 6;
            settings._arrSliceCount[2] = 1;
            settings._sliceJitter      = 0.0f;
            settings._listLevelCount   = { 3, 12 };
            sw::FractureAsset asset;
            sw::string        error;
            if ( sw::MeshFractureUtil::fracture( listBox, settings, asset, error ) == false )
                return {};
            const sw::string path = test::makeTempPath( pName );
            return asset.saveToFile( path ) ? path : sw::string{};
        }

        /** @brief 시험용 표 — 문턱이 낮고 파편이 빨리 쉰다. */
        static sw::string writeProfile( const utf8* pName )
        {
            const sw::string path = test::makeTempPath( pName );
            (void)sw::FileUtil::writeTextFile( path, R"(<DestructionProfile density="1500" physicsMaterial="Stone">
  <Strain thresholds="20 30 25"/>
  <Links strength="100" supportStrength="30000"/>
  <Impact impulseToStrain="0.5" minImpulse="20" radius="0.4"/>
  <Debris lifetime="1.5" maxBodies="64" smallVolume="0.03" fadeTime="0.25" sleepRemoveTime="0.3" keepCollisionVolume="0.5" hullShrink="0.01"/>
</DestructionProfile>)" );
            return path;
        }

        static void spawnFloor( sw::GameObjectManager& manager )
        {
            sw::GameObject*         pFloor = manager.createGameObject( sw::hashed_string( "Floor" ) );
            sw::RigidBodyComponent* pBody  = pFloor->addComponent<sw::RigidBodyComponent>();
            sw::PhysicsShapeDesc3D  box;
            box._halfExtents = sw::float3{ 30.0f, 0.5f, 30.0f };
            pBody->setShape( box );
            pBody->setBodyType( sw::PhysicsBodyType::Static );
            pBody->setLocalPosition( sw::float3{ 0.0f, -0.5f, 0.0f } );
        }

        /** @brief 벽 오브젝트 — 뿌리 강체(정적 상자, 오브젝트 자세를 맡는다) · 그리는 메시 · 파괴 컴포넌트(바닥 앵커). */
        static sw::FractureComponent* spawnWall( sw::GameObjectManager& manager, const sw::string& fracturePath, const sw::string& profilePath,
                                                 const sw::float3& position = sw::float3{} )
        {
            sw::GameObject*         pWall = manager.createGameObject( sw::hashed_string( "Wall" ) );
            sw::RigidBodyComponent* pBody = pWall->addComponent<sw::RigidBodyComponent>();
            sw::PhysicsShapeDesc3D  box;
            box._halfExtents   = sw::float3{ 2.0f, 1.5f, 0.15f };
            box._localPosition = sw::float3{ 0.0f, 1.5f, 0.0f };
            pBody->setShape( box );
            pBody->setBodyType( sw::PhysicsBodyType::Static );
            pBody->setLocalPosition( position );
            sw::MeshComponent*       pMesh = pWall->addComponent<sw::MeshComponent>();
            sw::shared_ptr<sw::Mesh> mesh  = sw::Mesh::create();
            mesh->setVertices( test::DestructionTestUtil::makeBox( sw::float3{ 2.0f, 1.5f, 0.15f }, sw::float3{ 0.0f, 1.5f, 0.0f } ) );
            pMesh->setMesh( mesh );
            sw::FractureComponent* pFracture = pWall->addComponent<sw::FractureComponent>();
            pFracture->setFracturePath( fracturePath );
            pFracture->setProfilePath( profilePath );
            pFracture->setAnchorMode( sw::FractureAnchorMode::Bottom );
            return pFracture;
        }

        static void tickFor( sw::GameObjectManager& manager, uint32 frameCount )
        {
            for ( uint32 frame = 0; frame < frameCount; ++frame )
                manager.tick( kFrame );
        }

        static uint32 countSkinnedUnits( const sw::GameObject& object )
        {
            uint32 count = 0;
            for ( sw::Component* pComp : object.getComponents() )
                count += sw::castTo<sw::SkeletalMeshComponent>( pComp ) != nullptr ? 1u : 0u;
            return count;
        }
    };
} // namespace

/**
 * @brief [FractureComponentTest] 온전한 벽은 바디 · 그림을 들지 않고, 폭발에 조각으로 바뀌어(메시 숨김 · 강체 끔 · 스킨드 메시 둘 · 붙은 조각은 정적 바디)
 *        떨어진 덩어리는 폭발에 날아가고 붙은 조각은 제자리이고, 시간이 지나면 잠든 바디를 빼고 정적 그림으로 구워진다
 */
SW_TEST_CASE( FractureComponentTest, WallBreaksIntoPiecesThatFlySettleAndBake )
{
    using Internal                = TestFractureComponentInternal;
    const sw::string fracturePath = Internal::writeWallFracture( "wall_runtime.fracture" );
    const sw::string profilePath  = Internal::writeProfile( "wall_runtime.destruction.xml" );
    SW_ASSERT_FALSE( fracturePath.empty() );
    sw::GameObjectManager manager;
    Internal::spawnFloor( manager );
    sw::FractureComponent* pFracture = Internal::spawnWall( manager, fracturePath, profilePath );
    SW_ASSERT_NOT_NULL( pFracture );
    sw::GameObject* pWall = pFracture->getOwner();
    manager.beginPlay();
    Internal::tickFor( manager, 5 );
    SW_ASSERT_TRUE( pFracture->hasFractureData() );
    SW_EXPECT_FALSE( pFracture->isFractured() );
    SW_EXPECT_EQUAL( 0u, pFracture->getStaticBodyCount() );
    SW_EXPECT_EQUAL( 0u, Internal::countSkinnedUnits( *pWall ) );

    // 위쪽 가운데 폭발 — 위의 벽돌이 떨어져 나간다.
    pFracture->applyRadialDamageAtWorld( sw::float3{ 0.0f, 2.4f, 0.4f }, 1.2f, 400.0f, 300.0f );
    Internal::tickFor( manager, 2 );
    SW_ASSERT_TRUE( pFracture->isFractured() );
    SW_EXPECT_FALSE( pWall->getComponent<sw::MeshComponent>()->isVisible() && Internal::countSkinnedUnits( *pWall ) == 0 );
    SW_EXPECT_FALSE( pWall->getComponent<sw::RigidBodyComponent>()->isSelfActive() );
    SW_EXPECT_EQUAL( 2u, Internal::countSkinnedUnits( *pWall ) );
    SW_EXPECT_TRUE( pFracture->getStaticBodyCount() > 20 );
    SW_EXPECT_TRUE( pFracture->getDynamicBodyCount() > 0 );
    SW_EXPECT_TRUE( pFracture->getEventLog()._listEvent.empty() == false );

    // 스킨드 유닛은 시작(다음 틱의 렌더 에셋 해석)을 지나도 조각 스켈레톤을 지킨다.
    for ( sw::Component* pComp : pWall->getComponents() )
    {
        const sw::SkeletalMeshComponent* pUnit = sw::castTo<sw::SkeletalMeshComponent>( pComp );
        if ( pUnit != nullptr )
            SW_EXPECT_EQUAL( 48u, pUnit->getSkeleton().getBoneCount() );
    }

    // 폭발에 밀린 조각은 날아간다 — 가장 멀리 간 조각이 처음 자리에서 0.5 m 넘게. 붙은 조각(그룹이 앵커)은 제자리다.
    Internal::tickFor( manager, 60 );
    float32                  maxMove = 0.0f;
    const sw::FractureAsset* pAsset  = pFracture->findAsset();
    for ( uint32 leaf = 0; leaf < 48; ++leaf )
    {
        const float32               move   = sw::float3::getDistance( pAsset->_graph._listNode[leaf]._centroid, pFracture->getPiecePoses()[leaf]._translation );
        const sw::DestructionGroup* pGroup = pFracture->getState().findGroup( pFracture->getState().getGroupOfLeaf( leaf ) );
        SW_ASSERT_NOT_NULL( pGroup );
        if ( pGroup->_bAnchored == SW_TRUE )
            SW_EXPECT_NEAR_EQUAL( 0.0f, move, 1e-4f );
        maxMove = sw::MathUtil::max( maxMove, move );
    }
    SW_EXPECT_TRUE( maxMove > 0.5f );

    // 쉬면 작은 파편은 바디를 빼거나 사라지고, 움직임이 멈추면 정적 그림으로 굽는다.
    Internal::tickFor( manager, 600 );
    SW_EXPECT_EQUAL( 0u, pFracture->getDynamicBodyCount() );
    SW_EXPECT_TRUE( pFracture->isBaked() );
    manager.endPlay();
    manager.tick( Internal::kFrame );
    SW_EXPECT_EQUAL( 0u, pFracture->getStaticBodyCount() );
}

/**
 * @brief [FractureComponentTest] 같은 사건 기록을 다른 매니저의 같은 벽에 적용하면 구조 상태 해시가 같다(물리 자세는 보내지 않는다). 광선 피해는 맞은 벽에 사건을 남긴다
 */
SW_TEST_CASE( FractureComponentTest, ReplayedEventLogGivesTheSameStateAndRaycastHits )
{
    using Internal                     = TestFractureComponentInternal;
    const sw::string      fracturePath = Internal::writeWallFracture( "wall_replay.fracture" );
    const sw::string      profilePath  = Internal::writeProfile( "wall_replay.destruction.xml" );
    sw::GameObjectManager server;
    sw::GameObjectManager client;
    Internal::spawnFloor( server );
    Internal::spawnFloor( client );
    sw::FractureComponent* pServerWall = Internal::spawnWall( server, fracturePath, profilePath );
    sw::FractureComponent* pClientWall = Internal::spawnWall( client, fracturePath, profilePath );
    pClientWall->setAuthority( false );
    server.beginPlay();
    client.beginPlay();
    Internal::tickFor( server, 3 );
    Internal::tickFor( client, 3 );

    sw::float3 hitPoint{};
    SW_ASSERT_TRUE( sw::FractureComponentBase::applyRaycastDamage( server, sw::float3{ 0.5f, 1.2f, -5.0f }, sw::float3{ 0.0f, 0.0f, 1.0f }, 20.0f, 500.0f, 100.0f, &hitPoint ) );
    SW_EXPECT_NEAR_EQUAL( -0.15f, hitPoint._z, 0.05f );
    pServerWall->applyRadialDamageAtWorld( sw::float3{ -1.0f, 2.5f, 0.0f }, 1.0f, 300.0f, 200.0f );
    Internal::tickFor( server, 120 );
    const sw::DestructionEventLog log = pServerWall->getEventLog();
    SW_ASSERT_TRUE( log._listEvent.size() >= 2 );
    SW_EXPECT_TRUE( pServerWall->isFractured() );

    for ( const sw::DestructionDamageEvent& event : log._listEvent )
        pClientWall->applyDamage( event );
    Internal::tickFor( client, 120 );
    SW_EXPECT_EQUAL( pServerWall->getState().computeStateHash(), pClientWall->getState().computeStateHash() );
    SW_EXPECT_TRUE( pClientWall->isFractured() );
    server.endPlay();
    client.endPlay();
}

/**
 * @brief [FractureComponentTest] 앵커 없는 상자는 높은 데서 떨어져 바닥에 세게 부딪히면(시작 충격량) 스스로 깨진다. 살살 내려놓은 상자는 온전하다
 */
SW_TEST_CASE( FractureComponentTest, HardImpactBreaksAFreeObject )
{
    using Internal                     = TestFractureComponentInternal;
    const sw::string      fracturePath = Internal::writeWallFracture( "crate_impact.fracture" );
    const sw::string      profilePath  = Internal::writeProfile( "crate_impact.destruction.xml" );
    sw::GameObjectManager manager;
    Internal::spawnFloor( manager );
    sw::FractureComponent* pHigh = Internal::spawnWall( manager, fracturePath, profilePath, sw::float3{ -6.0f, 12.0f, 0.0f } );
    sw::FractureComponent* pLow  = Internal::spawnWall( manager, fracturePath, profilePath, sw::float3{ 6.0f, 0.02f, 0.0f } );
    for ( sw::FractureComponent* pFracture : { pHigh, pLow } )
    {
        pFracture->setAnchorMode( sw::FractureAnchorMode::None );
        pFracture->getOwner()->getComponent<sw::RigidBodyComponent>()->setBodyType( sw::PhysicsBodyType::Dynamic );
    }
    manager.beginPlay();
    Internal::tickFor( manager, 180 );
    SW_EXPECT_TRUE( pHigh->isFractured() );
    SW_EXPECT_TRUE( pHigh->getEventLog()._listEvent.empty() == false );
    SW_EXPECT_FALSE( pLow->isFractured() );
    manager.endPlay();
}
