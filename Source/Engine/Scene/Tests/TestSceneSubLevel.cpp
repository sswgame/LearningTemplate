// SceneSubLevelTest — 런타임 하위 레벨(설계 "런타임 하위 레벨 · Packed Level · 원점 이동"): 앵커 아래에 펼쳐 짓고, 같은 씬을 두 번 더해도 id 가 겹치지 않으며,
// 셀에 들지 않고, 세이브 몫이 비운 매니저 위에서 그 상태로 다시 서고, 빼면 모두 내린다.
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Scene/SceneSubLevelSubsystem.h"

using namespace sw;

namespace
{
    struct SceneSubLevelTestUtil
    {
        static constexpr const utf8* kSettings = "<SceneSettings _bStreaming=\"false\" _unloadMargin=\"0\" _maxCellLoadsPerFrame=\"0\" _addToWorldBudgetMicroseconds=\"0\"><_cellGrid _cellSize=\"0\" _loadingRadius=\"0\" /></SceneSettings>";

        static string makeEntity( const utf8* pID, const utf8* pName, const utf8* pPosition, const utf8* pAttach = "" )
        {
            return string( "<entity id=\"" ) + pID + "\" name=\"" + pName + "\"><GameObject _schemaVersion=\"0\" _name=\"" + pName +
                   "\" _bActive=\"true\"><_listComponent><SceneComponent _localPosition=\"" + pPosition + "\" " + pAttach + " /></_listComponent></GameObject></entity>";
        }

        static string makeScene( const utf8* pName, const string& entities )
        {
            return string( "<Scene formatVersion=\"3\" name=\"" ) + pName + "\">" + kSettings + "<entities>" + entities + "</entities></Scene>";
        }

        /** @brief 하위 레벨로 넣을 씬 — Box(파일 id 1)와 Box 에 붙은 Lid(2). 바깥 씬의 엔티티 1 과 id 가 같다. */
        static string makeShopScene()
        {
            return makeScene( "Shop", makeEntity( "1", "Box", "0,1,0" ) + makeEntity( "2", "Lid", "0,2,0", "_attachOwner=\"Box\" _attachOwnerID=\"1\" _attachComponent=\"SceneComponent#0\"" ) );
        }

        /** @brief 바깥 씬 — 엔티티 1 하나(Ground). */
        static SceneDocument makeHostDocument()
        {
            SceneDocument doc{};
            doc._name   = "Hub";
            doc._bValid = true;
            SceneDocument::SceneObjectNode node{};
            node._fileID      = 1;
            node._name        = "Ground";
            node._embeddedXML = "<GameObject _schemaVersion=\"0\" _name=\"Ground\" _bActive=\"true\"><_listComponent><SceneComponent _localPosition=\"0,0,0\" /></_listComponent></GameObject>";
            doc._listSceneObjectNode.push_back( std::move( node ) );
            return doc;
        }

        static string writeShop( const test::ScopedScratchDomain& domain )
        {
            SW_EXPECT_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "shop.scene.xml" ), makeShopScene() ) );
            return test::ScopedScratchDomain::makeID( "shop.scene.xml" ).str();
        }

        static uint32 countNamed( GameObjectManager& objects, const utf8* pName )
        {
            objects.processDeferredDestruction();
            vector<GameObject*> listObject;
            objects.findGameObjectsByBaseName( pName, listObject );
            uint32 count = 0;
            for ( const GameObject* pObject : listObject )
            {
                count += pObject->isPendingDestroy() ? 0u : 1u;
            }
            return count;
        }

        static float3 worldPosition( GameObjectManager& objects, const utf8* pName )
        {
            const GameObject*     pObject = objects.findGameObjectByName( hashed_string( pName ) );
            const SceneComponent* pScene  = pObject != nullptr ? pObject->getPrimarySceneComponent() : nullptr;
            SW_EXPECT_TRUE( pScene != nullptr );
            return pScene != nullptr ? pScene->getWorldPosition() : float3{};
        }
    };
    using Util = SceneSubLevelTestUtil;
} // namespace

/** @brief [SceneSubLevelTest] 더한 씬은 다음 갱신에 앵커 아래로 펼쳐 서고(자리 · 부착 · 늘 올라 있음), 빼면 앵커와 멤버가 모두 내려간다 */
SW_TEST_CASE( SceneSubLevelTest, AddBuildsUnderAnchorAndRemoveUnloads )
{
    const test::ScopedScratchDomain domain;
    const string                    shopPath = Util::writeShop( domain );
    Scene                           scene{ "Hub" };
    SW_REQUIRE_TRUE( scene.instantiate( Util::makeHostDocument() ) );
    GameObjectManager&      objects   = *scene.getObjectManager();
    SceneSubLevelSubsystem& subLevels = objects.getSubsystem<SceneSubLevelSubsystem>();

    SW_REQUIRE_TRUE( subLevels.addSubLevel( scene, "Shop", shopPath, SceneSubLevelSubsystem::Placement{
                                                                         float3{ 100.0f, 0.0f, 0.0f },
                                                                         float3{}
    } ) );
    SW_EXPECT_EQUAL( 0u, Util::countNamed( objects, "Shop/Box" ) ); // 받기만 한다 — 짓는 것은 갱신이다
    subLevels.update( 0.0f );
    objects.mergePendingAdds();
    SW_REQUIRE_EQUAL( 1u, subLevels.getLoadedSubLevelCount() );

    GameObject* pAnchor = objects.findGameObjectByName( hashed_string( "Shop" ) );
    GameObject* pBox    = objects.findGameObjectByName( hashed_string( "Shop/Box" ) );
    GameObject* pLid    = objects.findGameObjectByName( hashed_string( "Shop/Lid" ) );
    SW_REQUIRE_TRUE( pAnchor != nullptr && pBox != nullptr && pLid != nullptr );
    SW_EXPECT_TRUE( pBox->getParent() == pAnchor );
    SW_EXPECT_TRUE( pLid->getParent() == pBox ); // 내부 id 1 이 바깥 씬의 Ground(1)로 가지 않는다
    SW_EXPECT_TRUE( pAnchor->isAlwaysLoaded() ); // 스트리밍 셀이 삼키지 않는다
    SW_EXPECT_EQUAL( 100.0f, Util::worldPosition( objects, "Shop/Box" )._x );
    SW_EXPECT_EQUAL( 1.0f, Util::worldPosition( objects, "Shop/Box" )._y );
    SW_EXPECT_TRUE( subLevels.isSubLevelObject( pLid->getObjectID() ) );
    SW_EXPECT_FALSE( subLevels.isSubLevelObject( objects.findGameObjectByName( hashed_string( "Ground" ) )->getObjectID() ) );
    // 앵커의 넓은 id 는 다음 파일 id 를 끌어올리지 않는다.
    SW_EXPECT_EQUAL( SceneSubLevelSubsystem::computeAnchorFileID( "Shop" ), scene.findFileID( pAnchor->getObjectID() ) );
    GameObject* pSpawned = objects.createGameObject( hashed_string( "Spawned" ) );
    SW_REQUIRE_NOT_NULL( pSpawned );
    SW_EXPECT_EQUAL( uint64{ 2 }, scene.getOrAssignFileID( pSpawned->getObjectID() ) );

    SW_REQUIRE_TRUE( subLevels.removeSubLevel( "Shop" ) );
    subLevels.update( 0.0f );
    SW_EXPECT_EQUAL( 0u, subLevels.getSubLevelCount() );
    SW_EXPECT_EQUAL( 0u, Util::countNamed( objects, "Shop" ) );
    SW_EXPECT_EQUAL( 0u, Util::countNamed( objects, "Shop/Box" ) );
    SW_EXPECT_EQUAL( 0u, Util::countNamed( objects, "Shop/Lid" ) );
    SW_EXPECT_EQUAL( 1u, Util::countNamed( objects, "Ground" ) );
}

/** @brief [SceneSubLevelTest] 같은 씬을 다른 이름으로 두 번 더하면 파일 id 가 겹치지 않고 각자 자기 앵커 안에서 잇는다 — 같은 이름 · 없는 씬은 받지 않는다 */
SW_TEST_CASE( SceneSubLevelTest, SameSceneTwiceDoesNotShareIds )
{
    const test::ScopedScratchDomain domain;
    const string                    shopPath = Util::writeShop( domain );
    Scene                           scene{ "Hub" };
    SW_REQUIRE_TRUE( scene.instantiate( Util::makeHostDocument() ) );
    GameObjectManager&      objects   = *scene.getObjectManager();
    SceneSubLevelSubsystem& subLevels = objects.getSubsystem<SceneSubLevelSubsystem>();

    SW_REQUIRE_TRUE( subLevels.addSubLevel( scene, "East", shopPath, SceneSubLevelSubsystem::Placement{
                                                                         float3{ 10.0f, 0.0f, 0.0f },
                                                                         float3{}
    } ) );
    SW_REQUIRE_TRUE( subLevels.addSubLevel( scene, "West", shopPath, SceneSubLevelSubsystem::Placement{
                                                                         float3{ -10.0f, 0.0f, 0.0f },
                                                                         float3{}
    } ) );
    SW_EXPECT_FALSE( subLevels.addSubLevel( scene, "East", shopPath, SceneSubLevelSubsystem::Placement{} ) );
    SW_EXPECT_FALSE( subLevels.addSubLevel( scene, "Nowhere", "missing/nothing.scene.xml", SceneSubLevelSubsystem::Placement{} ) );
    subLevels.update( 0.0f );
    objects.mergePendingAdds();
    SW_REQUIRE_EQUAL( 2u, subLevels.getLoadedSubLevelCount() );

    const GameObject* pEastLid = objects.findGameObjectByName( hashed_string( "East/Lid" ) );
    const GameObject* pWestLid = objects.findGameObjectByName( hashed_string( "West/Lid" ) );
    const GameObject* pEastBox = objects.findGameObjectByName( hashed_string( "East/Box" ) );
    SW_REQUIRE_TRUE( pEastLid != nullptr && pWestLid != nullptr && pEastBox != nullptr );
    SW_EXPECT_TRUE( pEastLid->getParent() == pEastBox );
    SW_EXPECT_NOT_EQUAL( scene.findFileID( pEastLid->getObjectID() ), scene.findFileID( pWestLid->getObjectID() ) );
    SW_EXPECT_EQUAL( -10.0f, Util::worldPosition( objects, "West/Lid" )._x );
}

/** @brief [SceneSubLevelTest] 문서의 멤버 옮기기는 파일 안 씬 인스턴스 펼치기와 같은 규칙이다 — 파일 id · 글 범위 · 쿠킹 상태 범위가 앵커 범위로 옮고, 앵커 없는 멤버는 이 앵커에 붙는다 */
SW_TEST_CASE( SceneSubLevelTest, MemberNodesUseTheAnchorScope )
{
    SceneDocument                  doc{};
    SceneDocument::SceneObjectNode node{};
    node._fileID = 3;
    node._name   = "Crate";
    doc._listSceneObjectNode.push_back( node );
    node._fileID               = 4;
    node._instanceAnchorFileID = 3; // 안쪽 인스턴스의 멤버
    doc._listSceneObjectNode.push_back( node );

    const uint64                           anchorFileID = SceneSubLevelSubsystem::computeAnchorFileID( "Yard" );
    const uint64                           scope        = SceneDocument::computeInstanceIDScope( anchorFileID );
    vector<SceneDocument::SceneObjectNode> listNode;
    SceneSubLevelSubsystem::makeMemberNodes( doc, "Yard", anchorFileID, listNode );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listNode.size() );
    SW_EXPECT_EQUAL( uint64{ 3 } ^ scope, listNode[0]._fileID );
    SW_EXPECT_EQUAL( anchorFileID, listNode[0]._instanceAnchorFileID );
    SW_EXPECT_EQUAL( scope, listNode[0]._idScope );
    SW_EXPECT_EQUAL( scope, listNode[0]._cookedStateIDScope );
    SW_EXPECT_EQUAL( uint64{ 3 } ^ scope, listNode[1]._instanceAnchorFileID );
    SW_EXPECT_EQUAL( string( "Yard/Crate" ), listNode[0]._name );
    SW_EXPECT_EQUAL( anchorFileID, SceneSubLevelSubsystem::computeAnchorFileID( "Yard" ) ); // 프로세스가 달라도 같은 이름이면 같다
    SW_EXPECT_NOT_EQUAL( anchorFileID, SceneSubLevelSubsystem::computeAnchorFileID( "yard" ) );
}

/**
 * @brief [SceneSubLevelTest] 세이브 몫은 다 지은 하위 레벨의 지금 상태(앵커 포함)를 싣고, 비운 매니저(복원) 위에서 그 상태로 다시 선다 — 저장 뒤에 바꾼 것은 덮이고 문서를 다시 읽지 않는다
 */
SW_TEST_CASE( SceneSubLevelTest, SaveStateRestoresOverAClearedManager )
{
    const test::ScopedScratchDomain domain;
    const string                    shopPath = Util::writeShop( domain );
    Scene                           scene{ "Hub" };
    SW_REQUIRE_TRUE( scene.instantiate( Util::makeHostDocument() ) );
    GameObjectManager&      objects   = *scene.getObjectManager();
    SceneSubLevelSubsystem& subLevels = objects.getSubsystem<SceneSubLevelSubsystem>();
    SW_REQUIRE_TRUE( subLevels.addSubLevel( scene, "Shop", shopPath, SceneSubLevelSubsystem::Placement{
                                                                         float3{ 100.0f, 0.0f, 0.0f },
                                                                         float3{}
    } ) );
    subLevels.update( 0.0f );
    objects.mergePendingAdds();

    GameObject* pBox = objects.findGameObjectByName( hashed_string( "Shop/Box" ) );
    SW_REQUIRE_TRUE( pBox != nullptr && pBox->getPrimarySceneComponent() != nullptr );
    pBox->getPrimarySceneComponent()->setLocalPosition( float3{ 5.0f, 1.0f, 0.0f } );

    vector<uint8> bytes;
    subLevels.writeSaveState( bytes );
    SceneSubLevelSubsystem::SaveState state;
    SW_REQUIRE_TRUE( SceneSubLevelSubsystem::readSaveState( bytes.data(), bytes.size(), state ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, state._listSubLevel.size() );
    SW_EXPECT_EQUAL( string( "Shop" ), state._listSubLevel[0]._name );
    pBox->getPrimarySceneComponent()->setLocalPosition( float3{ 9.0f, 1.0f, 0.0f } ); // 저장 뒤에 바꾼다 — 복원이 덮는다

    objects.clear();
    scene.rebindRestoredFileIDs( {} );
    SW_EXPECT_EQUAL( 0u, subLevels.getSubLevelCount() );                                             // 비우면 버린다 — 세이브가 다시 세운다
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "shop.scene.xml" ), "broken" ) ); // 복원은 문서를 다시 읽지 않는다
    subLevels.applySaveState( scene, std::move( state ) );
    subLevels.update( 0.0f );
    objects.mergePendingAdds();
    SW_REQUIRE_EQUAL( 1u, subLevels.getLoadedSubLevelCount() );
    SW_EXPECT_EQUAL( 1u, Util::countNamed( objects, "Shop" ) );
    SW_EXPECT_EQUAL( 1u, Util::countNamed( objects, "Shop/Box" ) );
    SW_EXPECT_EQUAL( 105.0f, Util::worldPosition( objects, "Shop/Box" )._x );
    const GameObject* pAnchor = objects.findGameObjectByName( hashed_string( "Shop" ) );
    const GameObject* pLid    = objects.findGameObjectByName( hashed_string( "Shop/Lid" ) );
    SW_REQUIRE_TRUE( pAnchor != nullptr && pLid != nullptr );
    SW_EXPECT_TRUE( pAnchor->isAlwaysLoaded() );
    SW_EXPECT_TRUE( subLevels.isSubLevelObject( pLid->getObjectID() ) );
}
