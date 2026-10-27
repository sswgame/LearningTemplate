// 파괴 쇼케이스 씬(game/empty/maps/destructionshowcase.scene.xml) — 쿠킹한 .fracture(벽 200 조각 · 상자 · 드럼통)를 쓰는 오브젝트가 씬에서 읽히고,
// 도화선 드럼통이 1.5 초에 터져 옆 드럼통을 사슬로 터뜨리며 벽 · 상자를 그 자리에서 부순다.
#include "pch.h"

#include "Engine/Destruction/FractureAsset.h"
#include "Engine/Destruction/FractureComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"

#include "GameFramework/Base/Gameplay/Gimmick/Genre/ShooterGimmicks.h"
#include "GameFramework/Kits/Feature/Network/NetDestruction/DestructionNetSessionComponent.h"

#include "TestFramework/Harness/TestTick.h"
#include "TestFramework/TestFramework.h"

/**
 * @brief [DestructionShowcaseTest] 쇼케이스 씬 — 시작 1 초는 모두 온전, 3 초 뒤 두 드럼통이 터지고 벽(200 조각) · 상자가 조각으로 바뀌어 있다
 */
SW_TEST_CASE( DestructionShowcaseTest, FuseBarrelChainBreaksWallAndCrates )
{
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );
    sw::SceneDocument doc;
    SW_ASSERT_TRUE( doc.loadXML( sw::ResourceID{ "game/empty/maps/destructionshowcase.scene.xml" } ) );
    sw::Scene scene{ "DestructionShowcase" };
    SW_ASSERT_TRUE( scene.instantiate( doc ) );
    sw::GameObjectManager& manager = *scene.getObjectManager();
    sw::GameObject*        pWall   = manager.findGameObjectByName( "BrickWall" );
    sw::GameObject*        pFuse   = manager.findGameObjectByName( "BarrelFuse" );
    sw::GameObject*        pChain  = manager.findGameObjectByName( "BarrelChain" );
    SW_ASSERT_TRUE( pWall != nullptr && pFuse != nullptr && pChain != nullptr );
    const sw::FractureComponent* pWallFracture = pWall->getComponent<sw::FractureComponent>();
    SW_ASSERT_NOT_NULL( pWallFracture );

    manager.beginPlay();
    test::updateFrames( manager, 60 );
    SW_ASSERT_TRUE( pWallFracture->hasFractureData() );
    SW_EXPECT_EQUAL( 200u, pWallFracture->findAsset()->getPieceCount() );
    SW_EXPECT_FALSE( pWallFracture->isFractured() );
    SW_EXPECT_FALSE( pFuse->getComponent<sw::ExplosiveBarrelComponent>()->hasExploded() );

    test::updateFrames( manager, 180 );
    SW_EXPECT_TRUE( pFuse->getComponent<sw::ExplosiveBarrelComponent>()->hasExploded() );
    SW_EXPECT_TRUE( pChain->getComponent<sw::ExplosiveBarrelComponent>()->hasExploded() );
    SW_EXPECT_TRUE( pWallFracture->isFractured() );
    SW_EXPECT_TRUE( pFuse->getComponent<sw::FractureComponent>()->isFractured() );
    uint32 brokenCrateCount = 0;
    for ( const utf8* pName : { "CrateLeft", "CrateRight", "CrateStacked" } )
    {
        sw::GameObject* pCrate = manager.findGameObjectByName( sw::hashed_string( pName ) );
        SW_ASSERT_NOT_NULL( pCrate );
        brokenCrateCount += pCrate->getComponent<sw::FractureComponent>()->isFractured() ? 1u : 0u;
    }
    SW_EXPECT_TRUE( brokenCrateCount >= 1 );
    manager.endPlay();
}

/**
 * @brief [DestructionShowcaseTest] 네트워크용 씬(destructionnet.scene.xml)은 쇼케이스와 같은 파괴 오브젝트에 세션 컴포넌트 하나를 더한다 — 세션 없이(혼자) 돌면 쇼케이스와 같이 부서진다
 * @details 씬 글은 쇼케이스를 그대로 옮기고 오브젝트 하나만 더해 손으로 썼다(에디터 저장과 같은 형식). 서버 · 클라이언트 실행은 컴포넌트 머리말의 명령이다.
 */
SW_TEST_CASE( DestructionShowcaseTest, NetworkSceneAddsTheSessionComponentToTheShowcase )
{
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );
    sw::SceneDocument doc;
    SW_ASSERT_TRUE( doc.loadXML( sw::ResourceID{ "game/empty/maps/destructionnet.scene.xml" } ) );
    sw::Scene scene{ "DestructionNet" };
    SW_ASSERT_TRUE( scene.instantiate( doc ) );
    sw::GameObjectManager& manager  = *scene.getObjectManager();
    sw::GameObject*        pSession = manager.findGameObjectByName( "DestructionNetSession" );
    SW_ASSERT_NOT_NULL( pSession );
    SW_EXPECT_NOT_NULL( pSession->getComponent<sw::DestructionNetSessionComponent>() );
    sw::GameObject* pFuse = manager.findGameObjectByName( "BarrelFuse" );
    SW_ASSERT_NOT_NULL( pFuse );

    manager.beginPlay();
    test::updateFrames( manager, 240 );
    SW_EXPECT_TRUE( pFuse->getComponent<sw::ExplosiveBarrelComponent>()->hasExploded() );
    manager.endPlay();
}
