// 내비메시 쿠킹 — 표면이 놓인 쇼케이스 씬을 쿠킹한 타일과 런타임이 처음 쓸 때 베이크한 타일이 바이트까지 같고, 런타임은 맞는 쿠킹본을 끼우고 낡은 것은 버린다.
#include "pch.h"

#include "Engine/Navigation/INavMesh.h"
#include "Engine/Navigation/NavMeshAsset.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/SceneNavigation.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Scene/SceneNavigationCooker.h"

#include "TestFramework/TestFramework.h"

namespace
{
    struct NavMeshCookTestInternal
    {
        static constexpr const utf8* kScenePath = "game/empty/maps/destructionshowcase.scene.xml";

        [[nodiscard]] static bool instantiate( sw::Scene& scene )
        {
            sw::SceneDocument document;
            return document.loadXml( kScenePath ) && scene.instantiate( document );
        }
    };
} // namespace

/**
 * @brief [NavMeshCookTest] 쇼케이스 씬 — 쿠킹한 타일과 런타임 베이크의 타일이 바이트까지 같고, 쿠킹본을 주면 런타임은 베이크하지 않고 끼우며, 씬이 바뀌면 낡은 쿠킹본을 버리고 베이크한다
 */
SW_TEST_CASE( NavMeshCookTest, CookedTilesMatchTheRuntimeBakeAndStaleOnesAreRejected )
{
    using Internal = NavMeshCookTestInternal;
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    sw::NavMeshAsset cooked;
    SW_ASSERT_TRUE( sw::SceneNavigationCooker::cookScene( Internal::kScenePath, cooked ) );
    SW_ASSERT_EQUAL( size_t{ 1 }, cooked.getEntries().size() );
    const sw::NavMeshAssetEntry& entry = cooked.getEntries().front();
    SW_EXPECT_FALSE( entry._listTile.empty() );

    // 런타임 베이크(쿠킹본 없음) — 같은 타일.
    sw::Scene runtimeScene{ "NavRuntime" };
    SW_ASSERT_TRUE( Internal::instantiate( runtimeScene ) );
    sw::SceneNavigation& runtimeNavigation = runtimeScene.getObjectManager()->getSceneNavigation();
    // 쿠킹 팩(Shipping 의 Cooked)에는 이 씬의 `.navmesh` 가 있어 런타임이 그것을 끼운다 — 런타임 베이크를 보려면 찾을 경로를 비운다.
    runtimeNavigation.setSourcePath( {} );
    const sw::INavMesh* pRuntimeMesh = runtimeNavigation.ensureNavMesh( entry._agentType );
    SW_ASSERT_NOT_NULL( pRuntimeMesh );
    SW_EXPECT_FALSE( runtimeNavigation.isLoadedFromCooked( entry._agentType ) );
    sw::vector<sw::NavTileData> listRuntimeTile;
    pRuntimeMesh->collectTiles( listRuntimeTile );
    SW_ASSERT_EQUAL( entry._listTile.size(), listRuntimeTile.size() );
    for ( size_t tileIndex = 0; tileIndex < listRuntimeTile.size(); ++tileIndex )
        SW_EXPECT_TRUE( entry._listTile[tileIndex]._bytes == listRuntimeTile[tileIndex]._bytes );

    // 쿠킹본을 준 씬은 끼운다.
    const sw::shared_ptr<const sw::NavMeshAsset> pCooked = sw::make_shared<sw::NavMeshAsset>( cooked );
    sw::Scene                                    cookedScene{ "NavCooked" };
    SW_ASSERT_TRUE( Internal::instantiate( cookedScene ) );
    sw::SceneNavigation& cookedNavigation = cookedScene.getObjectManager()->getSceneNavigation();
    cookedNavigation.setCookedAsset( pCooked );
    const sw::INavMesh* pCookedMesh = cookedNavigation.ensureNavMesh( entry._agentType );
    SW_ASSERT_NOT_NULL( pCookedMesh );
    SW_EXPECT_TRUE( cookedNavigation.isLoadedFromCooked( entry._agentType ) );
    SW_EXPECT_EQUAL( pRuntimeMesh->getPolygonCount(), pCookedMesh->getPolygonCount() );

    // 바닥을 옮긴 씬은 입력 해시가 달라 쿠킹본을 버리고 베이크한다.
    sw::Scene staleScene{ "NavStale" };
    SW_ASSERT_TRUE( Internal::instantiate( staleScene ) );
    sw::GameObject* pGround = staleScene.getObjectManager()->findGameObjectByName( sw::hashed_string( "BrickWall" ) );
    SW_ASSERT_NOT_NULL( pGround );
    sw::SceneComponent* pGroundRoot = pGround->getPrimarySceneComponent();
    SW_ASSERT_NOT_NULL( pGroundRoot );
    pGroundRoot->setLocalPosition( pGroundRoot->getLocalPosition() + sw::float3{ 0.5f, 0.0f, 0.0f } );
    staleScene.getObjectManager()->flushSceneTransforms();
    sw::SceneNavigation& staleNavigation = staleScene.getObjectManager()->getSceneNavigation();
    staleNavigation.setCookedAsset( pCooked );
    {
        SW_TEST_DEFENSIVE_SCOPE( "a cooked navmesh whose scene moved is stale" );
        SW_ASSERT_NOT_NULL( staleNavigation.ensureNavMesh( entry._agentType ) );
    }
    SW_EXPECT_FALSE( staleNavigation.isLoadedFromCooked( entry._agentType ) );
}
