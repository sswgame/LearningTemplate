// 파괴 쇼케이스 씬(game/empty/maps/destructionshowcase.scene.xml) — 쿠킹한 .fracture(벽 200 조각 · 상자 · 드럼통)를 쓰는 오브젝트가 씬에서 읽히고,
// 도화선 드럼통이 1.5 초에 터져 옆 드럼통을 사슬로 터뜨리며 벽 · 상자를 그 자리에서 부순다.
#include "pch.h"

#include "Engine/Destruction/FractureAsset.h"
#include "Engine/Destruction/FractureComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"

#include "GameFramework/Base/Gameplay/Gimmick/Genre/ShooterGimmicks.h"

#include "TestFramework/TestFramework.h"
#include "TestFramework/TestTick.h"

/**
 * @brief [DestructionShowcaseTest] 쇼케이스 씬 — 시작 1 초는 모두 온전, 3 초 뒤 두 드럼통이 터지고 벽(200 조각) · 상자가 조각으로 바뀌어 있다
 */
SW_TEST_CASE( DestructionShowcaseTest, FuseBarrelChainBreaksWallAndCrates )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    sw::SceneDocument doc;
    SW_ASSERT_TRUE( doc.loadXML( "game/empty/maps/destructionshowcase.scene.xml" ) );
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
    test::tickFrames( manager, 60 );
    SW_ASSERT_TRUE( pWallFracture->hasFractureData() );
    SW_EXPECT_EQUAL( 200u, pWallFracture->findAsset()->getPieceCount() );
    SW_EXPECT_FALSE( pWallFracture->isFractured() );
    SW_EXPECT_FALSE( pFuse->getComponent<sw::ExplosiveBarrelComponent>()->hasExploded() );

    test::tickFrames( manager, 180 );
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
