#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Destruction/Fracture2DComponent.h"
#include "Engine/Destruction/FractureAsset.h"
#include "Engine/Destruction/MeshFracture.h"
#include "Engine/Destruction/PolygonFracture.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/Physics/RigidBody2DComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionCast.h"

#include "TestFramework/TestFramework.h"

// Fracture2DTest — 2D 다각형 보로노이(넓이 보존 · 변 길이 연결 · 결정적, 오목 모양)와, 같은 구조 · 피해 · 런타임 위의 Box2D 조각.

namespace
{
    struct TestFracture2DInternal
    {
        static float32 sumArea( const sw::FractureAsset& asset )
        {
            float32 total = 0.0f;
            for ( uint32 piece = 0; piece < asset.getPieceCount(); ++piece )
                total += asset._graph._listNode[piece]._volume;
            return total;
        }

        /** @brief 뿌리 정적 2D 강체(4 × 3 상자) + 2D 파괴 컴포넌트(바닥 앵커, 조각 20, 레벨 [4])입니다. */
        static sw::Fracture2DComponent* spawnWall2D( sw::GameObjectManager& manager, const utf8* pName, const sw::string& profilePath )
        {
            sw::GameObject*           pWall = manager.createGameObject( sw::hashed_string( pName ) );
            sw::RigidBody2DComponent* pBody = pWall->addComponent<sw::RigidBody2DComponent>();
            sw::PhysicsShapeDesc2D    box;
            box._halfExtents   = sw::float2{ 2.0f, 1.5f };
            box._localPosition = sw::float2{ 0.0f, 1.5f };
            pBody->setShape( box );
            pBody->setBodyType( sw::PhysicsBodyType::Static );
            sw::Fracture2DComponent* pFracture = pWall->addComponent<sw::Fracture2DComponent>();
            pFracture->setSize( sw::float2{ 4.0f, 3.0f } );
            pFracture->setPieceCount( 20 );
            pFracture->setFractureSeed( 9 );
            pFracture->setAnchorMode( sw::FractureAnchorMode::Bottom );
            pFracture->setProfilePath( profilePath );
            return pFracture;
        }

        static sw::string writeProfile( const utf8* pName )
        {
            const sw::string path = test::makeTempPath( pName );
            (void)sw::FileUtil::writeTextFile( path, R"(<DestructionProfile density="800" physicsMaterial="Wood">
  <Strain thresholds="10 15 10"/>
  <Links strength="40" supportStrength="30000"/>
  <Debris lifetime="3" maxBodies="64" smallVolume="0.01" fadeTime="0.25" sleepRemoveTime="0.5" keepCollisionVolume="0.5" hullShrink="0.005"/>
</DestructionProfile>)" );
            return path;
        }

        static void tickFor( sw::GameObjectManager& manager, uint32 frameCount )
        {
            for ( uint32 frame = 0; frame < frameCount; ++frame )
                manager.tick( 1.0f / 60.0f );
        }
    };
} // namespace

/**
 * @brief [Fracture2DTest] 상자 · 오목 L 다각형을 쪼개면 넓이 합이 모양 넓이이고, 이웃은 맞닿은 변 길이로 이어지며, 같은 씨앗이면 바이트까지 같다
 */
SW_TEST_CASE( Fracture2DTest, PolygonFractureKeepsAreaAndLinksNeighbours )
{
    const sw::vector<sw::float2> listBox = {
        sw::float2{-2.0f, 0.0f},
        sw::float2{ 2.0f, 0.0f},
        sw::float2{ 2.0f, 3.0f},
        sw::float2{-2.0f, 3.0f}
    };
    sw::FractureSettings settings;
    settings._pieceCount     = 16;
    settings._seed           = 4;
    settings._listLevelCount = { 4 };
    sw::FractureAsset asset;
    sw::string        error;
    SW_ASSERT_TRUE_MSG( sw::PolygonFractureUtil::fracture( listBox, settings, asset, error ), error.c_str() );
    SW_EXPECT_EQUAL( 16u, asset.getPieceCount() );
    SW_EXPECT_NEAR_EQUAL( 12.0f, TestFracture2DInternal::sumArea( asset ), 1e-3f );
    SW_EXPECT_TRUE( asset._graph._listLink.size() >= 16 );
    for ( const sw::FractureLink& link : asset._graph._listLink )
        SW_EXPECT_TRUE( link._area > 0.0f && link._area < 4.0f );
    SW_EXPECT_EQUAL( 3u, asset._graph.getDepthCount() );
    // 조각 그림은 앞뒤 두 면이고, 껍질은 조각 다각형 꼭짓점(Z = 0)이다.
    for ( uint32 piece = 0; piece < asset.getPieceCount(); ++piece )
    {
        SW_EXPECT_TRUE( asset.getPieceHull( piece ).size() >= 3 );
        for ( const sw::float3& point : asset.getPieceHull( piece ) )
            SW_EXPECT_NEAR_EQUAL( 0.0f, point._z, 0.0f );
    }
    sw::FractureAsset again;
    SW_ASSERT_TRUE( sw::PolygonFractureUtil::fracture( listBox, settings, again, error ) );
    sw::vector<uint8> bytes;
    sw::vector<uint8> againBytes;
    asset.makeBytes( bytes );
    again.makeBytes( againBytes );
    SW_EXPECT_TRUE( bytes == againBytes );

    // 오목 L(넓이 3)과 시계 방향 입력.
    const sw::vector<sw::float2> listL = {
        sw::float2{0.0f, 2.0f},
        sw::float2{1.0f, 2.0f},
        sw::float2{1.0f, 1.0f},
        sw::float2{2.0f, 1.0f},
        sw::float2{2.0f, 0.0f},
        sw::float2{0.0f, 0.0f}
    };
    settings._listLevelCount.clear();
    settings._pieceCount = 10;
    sw::FractureAsset concave;
    SW_ASSERT_TRUE_MSG( sw::PolygonFractureUtil::fracture( listL, settings, concave, error ), error.c_str() );
    SW_EXPECT_NEAR_EQUAL( 3.0f, TestFracture2DInternal::sumArea( concave ), 1e-3f );
}

/**
 * @brief [Fracture2DTest] 2D 벽은 시작할 때 쪼개 온전한 평평한 메시를 스스로 더하고, 폭발에 Box2D 조각(붙은 조각은 정적, 떨어진 것은 동적)으로 부서져 날아가며,
 *        같은 사건이면 다른 매니저에서도 같은 상태다
 */
SW_TEST_CASE( Fracture2DTest, WallBreaksIntoBox2DPiecesDeterministically )
{
    using Internal                       = TestFracture2DInternal;
    const sw::string         profilePath = Internal::writeProfile( "wall2d.destruction.xml" );
    sw::GameObjectManager    server;
    sw::GameObjectManager    client;
    sw::Fracture2DComponent* pServer = Internal::spawnWall2D( server, "Wall2D", profilePath );
    sw::Fracture2DComponent* pClient = Internal::spawnWall2D( client, "Wall2D", profilePath );
    pClient->setAuthority( false );
    server.beginPlay();
    client.beginPlay();
    Internal::tickFor( server, 3 );
    Internal::tickFor( client, 3 );
    SW_ASSERT_TRUE( pServer->hasFractureData() );
    SW_EXPECT_NOT_NULL( pServer->getOwner()->getComponent<sw::MeshComponent>() );
    SW_EXPECT_FALSE( pServer->isFractured() );

    pServer->applyRadialDamageAtWorld( sw::float3{ 0.5f, 2.5f, 0.0f }, 1.4f, 200.0f, 3000.0f );
    Internal::tickFor( server, 2 );
    SW_ASSERT_TRUE( pServer->isFractured() );
    SW_EXPECT_TRUE( pServer->getStaticBodyCount() > 0 );
    SW_EXPECT_TRUE( pServer->getDynamicBodyCount() > 0 );
    SW_EXPECT_FALSE( pServer->getOwner()->getComponent<sw::RigidBody2DComponent>()->isSelfActive() );
    Internal::tickFor( server, 60 );
    float32                  maxMove = 0.0f;
    const sw::FractureAsset* pAsset  = pServer->findAsset();
    for ( uint32 leaf = 0; leaf < pAsset->getPieceCount(); ++leaf )
    {
        const sw::float3& pose = pServer->getPiecePoses()[leaf]._translation;
        maxMove                = sw::MathUtil::max( maxMove, sw::float3::getDistance( pAsset->_graph._listNode[leaf]._centroid, pose ) );
        SW_EXPECT_NEAR_EQUAL( 0.0f, pose._z, 1e-4f ); // 2D 조각은 XY 평면을 떠나지 않는다
    }
    SW_EXPECT_TRUE( maxMove > 0.3f );

    for ( const sw::DestructionDamageEvent& event : pServer->getEventLog()._listEvent )
        pClient->applyDamage( event );
    Internal::tickFor( client, 2 );
    SW_EXPECT_EQUAL( pServer->getState().computeStateHash(), pClient->getState().computeStateHash() );
    server.endPlay();
    client.endPlay();
}
