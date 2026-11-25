// WorldOriginTest — 월드 원점 이동(설계 "런타임 하위 레벨 · Packed Level · 원점 이동"): 원점을 옮기면 루트만 옮고 붙은 것은 로컬 그대로이며, 떠 둔 상태 · 세이브 엔티티는
// 적힌 원점을 들어 다시 지을 때 지금 원점에 맞게 서고, 하위 레벨은 절대 자리에 놓인다.
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Foundation/DeterministicRun.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Subsystem/SceneSpatialGrid.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDataLayerSubsystem.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Scene/SceneSubLevelSubsystem.h"

using namespace sw;

namespace
{
    struct WorldOriginTestUtil
    {
        static SceneDocument::SceneObjectNode makeNode( uint64 fileID, string_view name, string_view position, string_view attach = {} )
        {
            SceneDocument::SceneObjectNode node{};
            node._fileID = fileID;
            node._name   = string( name );
            StringBuilder<constant::buffer::kMax512> xml;
            xml.appendFormat( "<GameObject _schemaVersion=\"0\" _name=\"%#\" _bActive=\"true\"><_listComponent><SceneComponent _localID=\"1\" _localPosition=\"%#\" %# /></_listComponent></GameObject>", name, position, attach );
            node._embeddedXML = string( xml.view() );
            return node;
        }

        /** @brief 루트 Ground(1000, 0, 500)와 거기 붙은 Lid(로컬 0, 2, 0), 레이어 Winter 의 Snow(5, 0, 0) — Winter 는 처음에 켜져 있다. */
        static SceneDocument makeDocument()
        {
            SceneDocument doc{};
            doc._name   = "Origin";
            doc._bValid = true;
            doc._listSceneObjectNode.push_back( makeNode( 1, "Ground", "1000,0,500" ) );
            doc._listSceneObjectNode.push_back( makeNode( 2, "Lid", "0,2,0", "_attachOwner=\"Ground\" _attachOwnerID=\"1\" _attachComponent=\"#0000000000000001\"" ) );
            SceneDocument::SceneObjectNode snow = makeNode( 3, "Snow", "5,0,0" );
            snow._listDataLayer.push_back( "Winter" );
            doc._listSceneObjectNode.push_back( std::move( snow ) );
            SceneDataLayerDef winter{};
            winter._name             = "Winter";
            winter._bInitiallyActive = true;
            doc._sceneSettings._listDataLayer.push_back( winter );
            return doc;
        }

        static const SceneComponent* findScene( GameObjectManager& objects, const utf8* pName )
        {
            objects.processDeferredDestruction();
            const GameObject* pObject = objects.findGameObjectByName( hashed_string( pName ) );
            return pObject != nullptr ? pObject->getPrimarySceneComponent() : nullptr;
        }
    };
    using Util = WorldOriginTestUtil;
} // namespace

/** @brief [WorldOriginTest] 원점을 옮기면 루트는 (옛 원점 − 새 원점)만큼 옮고, 붙은 것은 로컬 그대로 따라오며, Y 원점은 그대로다 */
SW_TEST_CASE( WorldOriginTest, ShiftMovesRootsAndKeepsChildrenLocal )
{
    Scene scene{ "Origin" };
    SW_REQUIRE_TRUE( scene.instantiate( Util::makeDocument() ) );
    GameObjectManager& objects = *scene.getObjectManager();
    SW_REQUIRE_TRUE( scene.shiftWorldOrigin( double3{ 1000.0, 50.0, 500.0 } ) );
    SW_EXPECT_EQUAL( 1000.0, scene.getWorldOrigin()._x );
    SW_EXPECT_EQUAL( 500.0, scene.getWorldOrigin()._z );
    SW_EXPECT_EQUAL( 0.0, scene.getWorldOrigin()._y ); // 높이는 옮기지 않는다

    const SceneComponent* pGround = Util::findScene( objects, "Ground" );
    const SceneComponent* pLid    = Util::findScene( objects, "Lid" );
    SW_REQUIRE_TRUE( pGround != nullptr && pLid != nullptr );
    SW_EXPECT_EQUAL( 0.0f, pGround->getWorldPosition()._x );
    SW_EXPECT_EQUAL( 0.0f, pGround->getWorldPosition()._z );
    SW_EXPECT_EQUAL( 2.0f, pLid->getLocalPosition()._y ); // 붙은 것은 로컬 그대로
    SW_EXPECT_EQUAL( 0.0f, pLid->getWorldPosition()._x );
    SW_EXPECT_EQUAL( -995.0f, Util::findScene( objects, "Snow" )->getWorldPosition()._x );

    SW_REQUIRE_TRUE( scene.shiftWorldOrigin( double3{} ) ); // 되돌리면 처음 자리다
    SW_EXPECT_EQUAL( 1000.0f, Util::findScene( objects, "Ground" )->getWorldPosition()._x );
}

/** @brief [WorldOriginTest] 원점 0 기준 자리는 원점을 옮겨도 그대로다 — 루트는 절대, 붙은 것은 부모 기준이고, 기억한 자리를 되돌려 쓰면 제자리다 */
SW_TEST_CASE( WorldOriginTest, ZeroOriginPositionSurvivesTheShift )
{
    Scene scene{ "Origin" };
    SW_REQUIRE_TRUE( scene.instantiate( Util::makeDocument() ) );
    GameObjectManager&    objects = *scene.getObjectManager();
    const SceneComponent* pLid    = Util::findScene( objects, "Lid" );
    GameObject*           pGround = objects.findGameObjectByName( hashed_string( "Ground" ) );
    SW_REQUIRE_TRUE( pGround != nullptr && pLid != nullptr );
    SceneComponent* pRoot = pGround->getPrimarySceneComponent();
    const float3    rest  = pRoot->getZeroOriginPosition();
    SW_EXPECT_EQUAL( 1000.0f, rest._x );

    SW_REQUIRE_TRUE( scene.shiftWorldOrigin( double3{ 900.0, 0.0, 500.0 } ) );
    SW_EXPECT_EQUAL( 100.0f, pRoot->getLocalPosition()._x ); // 씬 자리는 옮았다
    SW_EXPECT_EQUAL( 1000.0f, pRoot->getZeroOriginPosition()._x );
    SW_EXPECT_EQUAL( 2.0f, pLid->getZeroOriginPosition()._y ); // 부모 기준은 원점과 상관없다

    pRoot->setZeroOriginPosition( rest + float3{ 1.0f, 0.0f, 0.0f } ); // 문 열기 — 기억한 자리 + 여는 거리
    SW_EXPECT_EQUAL( 101.0f, pRoot->getLocalPosition()._x );
    pRoot->setZeroOriginPosition( rest );
    SW_EXPECT_EQUAL( 100.0f, pRoot->getLocalPosition()._x );
}

/** @brief [WorldOriginTest] 떠 둔 상태는 뜬 때의 원점을 든다 — 원점을 옮긴 뒤 다시 지으면 절대 자리가 같다(레이어를 끄고 옮기고 켠다, 반대도) */
SW_TEST_CASE( WorldOriginTest, HeldStateRebuildsAtItsAbsolutePosition )
{
    Scene scene{ "Origin" };
    SW_REQUIRE_TRUE( scene.instantiate( Util::makeDocument() ) );
    GameObjectManager&       objects = *scene.getObjectManager();
    SceneDataLayerSubsystem& layers  = objects.getSubsystem<SceneDataLayerSubsystem>();

    SW_REQUIRE_TRUE( layers.setLayerActive( "Winter", false ) );
    layers.update( 0.0f ); // 원점 0 에서 떠 둔다
    SW_REQUIRE_TRUE( scene.shiftWorldOrigin( double3{ 100.0, 0.0, 0.0 } ) );
    SW_REQUIRE_TRUE( layers.setLayerActive( "Winter", true ) );
    layers.update( 0.0f );
    const SceneComponent* pSnow = Util::findScene( objects, "Snow" );
    SW_REQUIRE_NOT_NULL( pSnow );
    SW_EXPECT_EQUAL( -95.0f, pSnow->getWorldPosition()._x ); // 절대 5

    SW_REQUIRE_TRUE( layers.setLayerActive( "Winter", false ) );
    layers.update( 0.0f ); // 원점 100 에서 떠 둔다
    SW_REQUIRE_TRUE( scene.shiftWorldOrigin( double3{} ) );
    SW_REQUIRE_TRUE( layers.setLayerActive( "Winter", true ) );
    layers.update( 0.0f );
    pSnow = Util::findScene( objects, "Snow" );
    SW_REQUIRE_NOT_NULL( pSnow );
    SW_EXPECT_EQUAL( 5.0f, pSnow->getWorldPosition()._x );
}

/** @brief [WorldOriginTest] 세이브 엔티티 목록은 런타임 칸(읽을 목적 · 쿠킹 상태 범위 · 월드 원점)을 엔티티마다 싣고 그대로 읽는다 */
SW_TEST_CASE( WorldOriginTest, SavedNodesCarryRuntimeState )
{
    vector<SceneDocument::SceneObjectNode> listNode;
    listNode.push_back( Util::makeNode( 7, "Crate", "1,2,3" ) );
    listNode[0]._statePurpose       = SerializePurpose::Snapshot;
    listNode[0]._cookedStateIDScope = 0x1234u;
    listNode[0]._worldOrigin        = double3{ 2048.0, 0.0, -4096.0 };
    vector<uint8> bytes;
    SceneDocument::writeSavedNodes( listNode, bytes );
    vector<SceneDocument::SceneObjectNode> listRead;
    SW_REQUIRE_TRUE( SceneDocument::readSavedNodes( bytes.data(), bytes.size(), listRead, "test" ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listRead.size() );
    SW_EXPECT_TRUE( listRead[0]._statePurpose == SerializePurpose::Snapshot );
    SW_EXPECT_EQUAL( uint64{ 0x1234u }, listRead[0]._cookedStateIDScope );
    SW_EXPECT_EQUAL( 2048.0, listRead[0]._worldOrigin._x );
    SW_EXPECT_EQUAL( -4096.0, listRead[0]._worldOrigin._z );
}

/** @brief [WorldOriginTest] 하위 레벨의 자리는 절대 좌표다 — 원점을 옮긴 월드에 더해도 같은 절대 자리에 선다 */
SW_TEST_CASE( WorldOriginTest, SubLevelPlacementIsAbsolute )
{
    const test::ScopedScratchDomain domain;
    const string                    inner = "<Scene formatVersion=\"3\" name=\"Shop\"><SceneSettings _bStreaming=\"false\" _unloadMargin=\"0\" _maxCellLoadsPerFrame=\"0\" _addToWorldBudgetMicroseconds=\"0\">"
                                            "<_cellGrid _cellSize=\"0\" _loadingRadius=\"0\" /></SceneSettings><entities><entity id=\"1\" name=\"Box\"><GameObject _schemaVersion=\"0\" _name=\"Box\" "
                                            "_bActive=\"true\"><_listComponent><SceneComponent _localPosition=\"0,1,0\" /></_listComponent></GameObject></entity></entities></Scene>";
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "shop.scene.xml" ), inner ) );
    Scene scene{ "Origin" };
    SW_REQUIRE_TRUE( scene.instantiate( Util::makeDocument() ) );
    GameObjectManager& objects = *scene.getObjectManager();
    SW_REQUIRE_TRUE( scene.shiftWorldOrigin( double3{ 1000.0, 0.0, 0.0 } ) );

    SceneSubLevelSubsystem& subLevels = objects.getSubsystem<SceneSubLevelSubsystem>();
    SW_REQUIRE_TRUE( subLevels.addSubLevel( scene, "Shop", test::ScopedScratchDomain::makeID( "shop.scene.xml" ).str(),
                                            SceneSubLevelSubsystem::Placement{
                                                float3{ 1010.0f, 0.0f, 0.0f },
                                                float3{}
    } ) );
    const DeterministicRunPolicy previous = engine::getSubsystem<DeterministicRun>().getPolicy();
    DeterministicRunPolicy       finish   = previous;
    finish._bFinishAssetJobsEachFrame     = SW_TRUE; // 갱신이 걸린 문서 읽기를 기다린다
    engine::getSubsystem<DeterministicRun>().setPolicy( finish );
    subLevels.update( 0.0f );
    engine::getSubsystem<DeterministicRun>().setPolicy( previous );
    objects.mergePendingAdds();
    const SceneComponent* pBox = Util::findScene( objects, "Shop/Box" );
    SW_REQUIRE_NOT_NULL( pBox );
    SW_EXPECT_EQUAL( 10.0f, pBox->getWorldPosition()._x ); // 절대 1010 − 원점 1000
}

/**
 * @brief [WorldOriginTest] 원점을 옮기면 씬 공간 색인도 같은 프레임에 새 자리를 본다 — 지역 볼륨이 다음 `SpatialIndex` 단계 전에 옛 자리 격자로 골라 대상을 내보냈다 다시 들이지 않는다
 * @details `Scene::shiftWorldOrigin` → `ISceneSubsystem::onWorldOriginShifted` → `SceneSpatialGrid` 의 길을 잇는다(격자 자체는 `SceneSpatialGridTest.OriginShiftRefreshesTheFrontBuffer`).
 */
SW_TEST_CASE( WorldOriginTest, ShiftRefreshesTheSceneSpatialGrid )
{
    static const SpatialGridLayerDesc kLayer{ "WorldOriginTest", SpatialGridPlane::XZ, 1 };
    Scene                             scene{ "Origin" };
    SW_REQUIRE_TRUE( scene.instantiate( Util::makeDocument() ) );
    GameObjectManager& objects = *scene.getObjectManager();
    GameObject*        pObject = objects.findGameObjectByName( hashed_string( "Ground" ) );
    SW_REQUIRE_NOT_NULL( pObject );
    SceneComponent* pGround = pObject->getPrimarySceneComponent();
    SW_REQUIRE_NOT_NULL( pGround );
    SceneSpatialGrid& grid  = objects.getSubsystem<SceneSpatialGrid>();
    SlotHandle        entry = grid.add( kLayer, *pGround, *pGround, 0.0f );
    grid.addLayerReader( kLayer );
    grid.requireCellSize( kLayer, 4.0f );
    vector<Component*> listKey;
    grid.query( kLayer, 999.0f, 499.0f, 1001.0f, 501.0f, listKey );
    SW_REQUIRE_EQUAL( static_cast<size_t>( 1 ), listKey.size() );

    SW_REQUIRE_TRUE( scene.shiftWorldOrigin( double3{ 1000.0, 0.0, 500.0 } ) );
    grid.query( kLayer, -1.0f, -1.0f, 1.0f, 1.0f, listKey ); // Ground 는 이제 (0, 0, 0)
    SW_EXPECT_EQUAL( static_cast<size_t>( 1 ), listKey.size() );
    grid.query( kLayer, 999.0f, 499.0f, 1001.0f, 501.0f, listKey );
    SW_EXPECT_TRUE( listKey.empty() );
    grid.removeLayerReader( kLayer );
    grid.remove( entry );
}

/** @brief [WorldOriginTest] 아직 합치지 않은(이번 프레임에 만든) 오브젝트도 원점과 함께 옮겨진다 — 프레임 끝 스폰이 옛 원점 자리에 남지 않는다 */
SW_TEST_CASE( WorldOriginTest, ShiftMovesObjectsCreatedThisFrame )
{
    Scene scene{ "Origin" };
    SW_REQUIRE_TRUE( scene.instantiate( Util::makeDocument() ) );
    GameObject* pSpawned = scene.getObjectManager()->createGameObject( hashed_string( "Spawned" ) );
    SW_REQUIRE_NOT_NULL( pSpawned );
    SceneComponent* pRoot = pSpawned->addComponent<SceneComponent>();
    SW_REQUIRE_NOT_NULL( pRoot );
    pRoot->setWorldPosition( float3{ 1010.0f, 0.0f, 500.0f } );
    SW_REQUIRE_TRUE( scene.shiftWorldOrigin( double3{ 1000.0, 0.0, 500.0 } ) );
    SW_EXPECT_EQUAL( 10.0f, pRoot->getWorldPosition()._x );
    SW_EXPECT_EQUAL( 0.0f, pRoot->getWorldPosition()._z );
}
