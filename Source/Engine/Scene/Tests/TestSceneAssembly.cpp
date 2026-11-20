// SceneAssemblyTest — 씬 인스턴스 · 씬 상속 · 데이터 레이어: 펼쳐 읽기, id 가 겹치지 않음, 차이로 저장, 레이어 켜고 끄기(끌 때 상태 보존), 레이어 세이브 몫, 쿠킹본 왕복.
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/Component/Tests/StateHashTestComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Service/NetObjectTable.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDataLayerSubsystem.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Serialization/XML/XMLDocument.h"

using namespace sw;

namespace
{
    struct SceneAssemblyTestUtil
    {
        static constexpr const utf8* kSettings = "<SceneSettings _bStreaming=\"false\" _unloadMargin=\"0\" _maxCellLoadsPerFrame=\"0\" _addToWorldBudgetMicroseconds=\"0\"><_cellGrid _cellSize=\"0\" _loadingRadius=\"0\" />";

        /** @brief 씬 컴포넌트 하나를 가진 엔티티입니다. */
        static string makeEntity( const utf8* pID, const utf8* pName, const utf8* pPosition, const utf8* pExtraAttributes = "", const utf8* pExtraChildren = "", const utf8* pAttach = "" )
        {
            string text = string( "<entity id=\"" ) + pID + "\" name=\"" + pName + "\" " + pExtraAttributes + ">" + pExtraChildren;
            text += string( "<GameObject _schemaVersion=\"0\" _name=\"" ) + pName + "\" _bActive=\"true\"><_listComponent><SceneComponent _localID=\"1\" _localPosition=\"" + pPosition + "\" " + pAttach + " /></_listComponent></GameObject></entity>";
            return text;
        }

        static string makeScene( const utf8* pName, const string& entities, const utf8* pLayers = "", const utf8* pRootExtra = "" )
        {
            return string( "<Scene formatVersion=\"2\" name=\"" ) + pName + "\" " + pRootExtra + ">" + kSettings + pLayers + "</SceneSettings><entities>" + entities + "</entities></Scene>";
        }

        /** @brief 부모(1) 와 부모에 붙은 자식(2) 을 가진 내부 씬입니다. */
        static string makeInnerScene()
        {
            return makeScene( "Inner", makeEntity( "1", "Box", "0,1,0" ) + makeEntity( "2", "Lid", "0,2,0", "", "", "_attachOwner=\"Box\" _attachOwnerID=\"1\" _attachComponent=\"#0000000000000001\"" ) );
        }

        static void write( const test::ScopedScratchDomain& domain, const utf8* pFile, const string& text )
        {
            SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( pFile ), text ) );
        }

        static string instanceAttribute( const utf8* pFile ) { return "scene=\"" + test::ScopedScratchDomain::makeID( pFile ).str() + "\""; }

        static const SceneDocument::SceneObjectNode* findNode( const SceneDocument& doc, string_view name )
        {
            for ( const SceneDocument::SceneObjectNode& node : doc._listSceneObjectNode )
            {
                if ( node._name == name )
                    return &node;
            }
            return nullptr;
        }

        static AbsolutePath makeAbsolute( const string& diskPath )
        {
            const optional<AbsolutePath> absPath = FileUtil::requireAbsolutePath( diskPath );
            SW_EXPECT_TRUE( absPath.has_value() );
            return *absPath;
        }
    };
    using Util = SceneAssemblyTestUtil;
} // namespace

/** @brief [SceneAssemblyTest] 같은 씬을 두 번 넣으면 파일 id · 이름이 인스턴스마다 다르고, 멤버 사이 부착은 자기 인스턴스 안에서 이어지며, 앵커 아래에 놓인다 */
SW_TEST_CASE( SceneAssemblyTest, InstancesDoNotShareIdsAndLinkInsideThemselves )
{
    const test::ScopedScratchDomain domain;
    Util::write( domain, "inner.scene.xml", Util::makeInnerScene() );
    const string attribute = Util::instanceAttribute( "inner.scene.xml" );
    Util::write( domain, "outer.scene.xml", Util::makeScene( "Outer", Util::makeEntity( "1", "Left", "-5,0,0", attribute.c_str() ) + Util::makeEntity( "2", "Right", "5,0,0", attribute.c_str() ) ) );

    SceneDocument doc{};
    SW_REQUIRE_TRUE( doc.loadXMLFromDiskFile( domain.makeDiskPath( "outer.scene.xml" ) ) );
    SW_REQUIRE_EQUAL( size_t{ 6 }, doc._listSceneObjectNode.size() ); // 앵커 둘 + 멤버 둘씩
    const SceneDocument::SceneObjectNode* pLeftBox  = Util::findNode( doc, "Left/Box" );
    const SceneDocument::SceneObjectNode* pRightBox = Util::findNode( doc, "Right/Box" );
    SW_REQUIRE_TRUE( pLeftBox != nullptr && pRightBox != nullptr );
    SW_EXPECT_NOT_EQUAL( pLeftBox->_fileID, pRightBox->_fileID );
    SW_EXPECT_EQUAL( string( "1/1" ), pLeftBox->_instancePartPath );
    SW_EXPECT_EQUAL( uint64{ 1 }, pLeftBox->_instanceAnchorFileID );

    Scene scene{ "Outer" };
    SW_REQUIRE_TRUE( scene.instantiate( doc ) );
    GameObjectManager& objects = *scene.getObjectManager();
    GameObject*        pLeft   = objects.findGameObjectByName( hashed_string( "Left" ) );
    GameObject*        pBox    = objects.findGameObjectByName( hashed_string( "Left/Box" ) );
    GameObject*        pLid    = objects.findGameObjectByName( hashed_string( "Left/Lid" ) );
    GameObject*        pRLid   = objects.findGameObjectByName( hashed_string( "Right/Lid" ) );
    SW_REQUIRE_TRUE( pLeft != nullptr && pBox != nullptr && pLid != nullptr && pRLid != nullptr );
    SW_EXPECT_TRUE( pBox->getParent() == pLeft );
    SW_EXPECT_TRUE( pLid->getParent() == pBox ); // 내부 id 1 이 바깥 엔티티 1(Left)이나 오른쪽 Box 로 잘못 가지 않는다
    SW_EXPECT_TRUE( pRLid->getParent() != pBox );

    // 네트워크 씬 번호는 이름의 해시다 — 인스턴스마다 달라야 한다(`NetObjectTable`).
    SW_EXPECT_NOT_EQUAL( NetObjectTable::computeSceneID( *pLid ), NetObjectTable::computeSceneID( *pRLid ) );
}

/** @brief [SceneAssemblyTest] 앵커의 `<Part path>` 가 멤버의 값을 덮어쓰고, 저장은 앵커와 차이만 적으며 다시 읽으면 같다 */
SW_TEST_CASE( SceneAssemblyTest, PartOverridesApplyAndSaveAsDifference )
{
    const test::ScopedScratchDomain domain;
    Util::write( domain, "inner.scene.xml", Util::makeInnerScene() );
    const string attribute = Util::instanceAttribute( "inner.scene.xml" );
    Util::write( domain, "outer.scene.xml",
                 Util::makeScene( "Outer", Util::makeEntity( "1", "House", "0,0,0", attribute.c_str(),
                                                             "<PrefabOverrides><Part path=\"1\"><PrefabOverrides><Override key=\"#0000000000000001\"><SceneComponent _localPosition=\"7,7,7\" /></Override></PrefabOverrides></Part></PrefabOverrides>" ) ) );

    SceneDocument doc{};
    SW_REQUIRE_TRUE( doc.loadXMLFromDiskFile( domain.makeDiskPath( "outer.scene.xml" ) ) );
    Scene scene{ "Outer" };
    SW_REQUIRE_TRUE( scene.instantiate( doc ) );
    const GameObject* pBox = scene.getObjectManager()->findGameObjectByName( hashed_string( "House/Box" ) );
    SW_REQUIRE_TRUE( pBox != nullptr && pBox->getPrimarySceneComponent() != nullptr );
    SW_EXPECT_NEAR( 7.0, pBox->getPrimarySceneComponent()->getWorldPosition()._x, 1e-4 );

    SceneDocument written{};
    SW_REQUIRE_TRUE( scene.serializeToDocument( written ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, written._listSceneObjectNode.size() ); // 멤버는 엔티티로 적지 않는다
    const string savedPath = domain.makeDiskPath( "saved.scene.xml" );
    SW_REQUIRE_TRUE( written.saveToFile( Util::makeAbsolute( savedPath ) ) );
    string savedText;
    SW_REQUIRE_TRUE( FileUtil::readTextFile( savedPath, savedText ) );
    SW_EXPECT_TRUE( savedText.find( "<Part path=\"1\"" ) != string::npos );
    SW_EXPECT_TRUE( savedText.find( "path=\"2\"" ) == string::npos ); // 고치지 않은 Lid 는 적지 않는다(부착 · 이름은 차이가 아니다)

    SceneDocument reread{};
    SW_REQUIRE_TRUE( reread.loadXMLFromDiskFile( savedPath ) );
    Scene sceneAgain{ "Saved" };
    SW_REQUIRE_TRUE( sceneAgain.instantiate( reread ) );
    const GameObject* pBoxAgain = sceneAgain.getObjectManager()->findGameObjectByName( hashed_string( "House/Box" ) );
    SW_REQUIRE_TRUE( pBoxAgain != nullptr );
    SW_EXPECT_NEAR( 7.0, pBoxAgain->getPrimarySceneComponent()->getWorldPosition()._x, 1e-4 );
}

/** @brief [SceneAssemblyTest] 씬이 자기를 포함하면 오류다(곧바로든 다른 씬을 거치든) */
SW_TEST_CASE( SceneAssemblyTest, SelfInclusionIsAnError )
{
    const test::ScopedScratchDomain domain;
    const string                    attributeA = Util::instanceAttribute( "a.scene.xml" );
    const string                    attributeB = Util::instanceAttribute( "b.scene.xml" );
    Util::write( domain, "a.scene.xml", Util::makeScene( "A", Util::makeEntity( "1", "ToB", "0,0,0", attributeB.c_str() ) ) );
    Util::write( domain, "b.scene.xml", Util::makeScene( "B", Util::makeEntity( "1", "ToA", "0,0,0", attributeA.c_str() ) ) );
    SW_EXPECT_LOG( "SceneDocument.InstanceCycle" );
    SW_EXPECT_LOG( "SceneDocument.InstanceNotLoaded" );
    SceneDocument doc{};
    SW_EXPECT_TRUE( doc.loadXML( test::ScopedScratchDomain::makeID( "a.scene.xml" ) ) == false );
}

/** @brief [SceneAssemblyTest] 앵커에 상태가 없으면(자리가 없으면) 오류다 */
SW_TEST_CASE( SceneAssemblyTest, AnchorWithoutStateIsAnError )
{
    const test::ScopedScratchDomain domain;
    Util::write( domain, "inner.scene.xml", Util::makeInnerScene() );
    const string text = "<entity id=\"1\" name=\"Bare\" " + Util::instanceAttribute( "inner.scene.xml" ) + " />";
    Util::write( domain, "outer.scene.xml", Util::makeScene( "Outer", text ) );
    SW_EXPECT_LOG( "SceneDocument.InstanceAnchorShape" );
    SceneDocument doc{};
    SW_EXPECT_TRUE( doc.loadXMLFromDiskFile( domain.makeDiskPath( "outer.scene.xml" ) ) == false );
}

/** @brief [SceneAssemblyTest] 씬 상속: 부모 위에 차이를 얹고, 저장은 차이로 남으며 다시 읽으면 같다 */
SW_TEST_CASE( SceneAssemblyTest, BaseSceneMergesAndSavesAsDelta )
{
    const test::ScopedScratchDomain domain;
    Util::write( domain, "base.scene.xml", Util::makeScene( "Base", Util::makeEntity( "1", "Keep", "1,0,0" ) + Util::makeEntity( "2", "Gone", "2,0,0" ) + Util::makeEntity( "3", "Move", "3,0,0" ) ) );
    const string baseAttribute = "_base=\"" + test::ScopedScratchDomain::makeID( "base.scene.xml" ).str() + "\"";
    Util::write( domain, "child.scene.xml",
                 string( "<Scene " ) + baseAttribute + " name=\"Child\"><entities><entity id=\"2\" _op=\"Remove\" />" + Util::makeEntity( "3", "Move", "9,0,0" ) + Util::makeEntity( "4", "New", "4,0,0" ) + "</entities></Scene>" );

    SceneDocument doc{};
    SW_REQUIRE_TRUE( doc.loadXML( test::ScopedScratchDomain::makeID( "child.scene.xml" ) ) );
    SW_EXPECT_EQUAL( string( "Child" ), doc._name );
    SW_EXPECT_EQUAL( test::ScopedScratchDomain::makeID( "base.scene.xml" ).str(), doc._baseScenePath );
    SW_EXPECT_TRUE( Util::findNode( doc, "Keep" ) != nullptr );
    SW_EXPECT_TRUE( Util::findNode( doc, "Gone" ) == nullptr );
    SW_EXPECT_TRUE( Util::findNode( doc, "New" ) != nullptr );

    Scene scene{ "Child" };
    SW_REQUIRE_TRUE( scene.instantiate( doc ) );
    SceneDocument written{};
    SW_REQUIRE_TRUE( scene.serializeToDocument( written ) );
    SW_EXPECT_EQUAL( doc._baseScenePath, written._baseScenePath );

    SceneDocument parentSource{};
    SW_REQUIRE_TRUE( parentSource.loadXML( test::ScopedScratchDomain::makeID( "base.scene.xml" ) ) );
    Scene parentScene{ "Base" };
    SW_REQUIRE_TRUE( parentScene.instantiate( parentSource ) );
    SceneDocument parent{};
    SW_REQUIRE_TRUE( parentScene.serializeToDocument( parent ) );
    const string savedPath = domain.makeDiskPath( "saved.scene.xml" );
    SW_REQUIRE_TRUE( written.saveXMLDelta( Util::makeAbsolute( savedPath ), parent ) );

    string savedText;
    SW_REQUIRE_TRUE( FileUtil::readTextFile( savedPath, savedText ) );
    SW_EXPECT_TRUE( savedText.find( "_base=" ) != string::npos );
    SW_EXPECT_TRUE( savedText.find( "name=\"Keep\"" ) == string::npos ); // 부모와 같은 엔티티는 적지 않는다

    SceneDocument reread{};
    SW_REQUIRE_TRUE( reread.loadXMLFromDiskFile( savedPath ) );
    SW_EXPECT_TRUE( Util::findNode( reread, "Keep" ) != nullptr );
    SW_EXPECT_TRUE( Util::findNode( reread, "Gone" ) == nullptr );
    SW_EXPECT_TRUE( Util::findNode( reread, "New" ) != nullptr );
}

/** @brief [SceneAssemblyTest] 끈 레이어의 엔티티는 오르지 않고 저장에 그대로 남으며, 켜면 오르고 끄면 내린다 */
SW_TEST_CASE( SceneAssemblyTest, DataLayerLoadsOnlyWhenActive )
{
    const test::ScopedScratchDomain domain;
    Util::write( domain, "layer.scene.xml",
                 Util::makeScene( "Layered", Util::makeEntity( "1", "Always", "0,0,0" ) + Util::makeEntity( "2", "Snow", "1,0,0", "layers=\"Winter\"" ) + Util::makeEntity( "3", "NightSnow", "2,0,0", "layers=\"Winter,Night\"" ),
                                  "<_listDataLayer><SceneDataLayerDef _name=\"Winter\" _bInitiallyActive=\"false\" /><SceneDataLayerDef _name=\"Night\" _bInitiallyActive=\"true\" /></_listDataLayer>" ) );
    SceneDocument doc{};
    SW_REQUIRE_TRUE( doc.loadXMLFromDiskFile( domain.makeDiskPath( "layer.scene.xml" ) ) );

    Scene scene{ "Layered" };
    SW_REQUIRE_TRUE( scene.instantiate( doc ) );
    GameObjectManager&       objects = *scene.getObjectManager();
    SceneDataLayerSubsystem& layers  = objects.getSubsystem<SceneDataLayerSubsystem>();
    SW_EXPECT_TRUE( objects.findGameObjectByName( hashed_string( "Always" ) ) != nullptr );
    SW_EXPECT_TRUE( objects.findGameObjectByName( hashed_string( "Snow" ) ) == nullptr );

    SceneDocument written{};
    SW_REQUIRE_TRUE( scene.serializeToDocument( written ) );
    SW_EXPECT_EQUAL( size_t{ 3 }, written._listSceneObjectNode.size() ); // 끈 레이어의 엔티티를 잃지 않는다

    SW_REQUIRE_TRUE( layers.setLayerActive( "Winter", true ) );
    layers.update( 0.0f );
    SW_EXPECT_TRUE( objects.findGameObjectByName( hashed_string( "Snow" ) ) != nullptr );
    SW_EXPECT_TRUE( objects.findGameObjectByName( hashed_string( "NightSnow" ) ) != nullptr ); // 둘 다 켜졌다

    SW_REQUIRE_TRUE( layers.setLayerActive( "Night", false ) );
    layers.update( 0.0f );
    const GameObject* pNightSnow = objects.findGameObjectByName( hashed_string( "NightSnow" ) );
    SW_EXPECT_TRUE( pNightSnow == nullptr || pNightSnow->isPendingDestroy() );
    const GameObject* pSnow = objects.findGameObjectByName( hashed_string( "Snow" ) );
    SW_EXPECT_TRUE( pSnow != nullptr && pSnow->isPendingDestroy() == false ); // Winter 는 켜져 있다
}

/** @brief [SceneAssemblyTest] 선언하지 않은 레이어는 오류다 — 엔티티에 적어도, 켜려 해도 */
SW_TEST_CASE( SceneAssemblyTest, UndeclaredLayerIsAnError )
{
    const test::ScopedScratchDomain domain;
    Util::write( domain, "bad.scene.xml", Util::makeScene( "Bad", Util::makeEntity( "1", "Snow", "0,0,0", "layers=\"Winter\"" ) ) );
    SW_EXPECT_LOG( "SceneDocument.DataLayerNotDeclared" );
    SceneDocument doc{};
    SW_EXPECT_TRUE( doc.loadXMLFromDiskFile( domain.makeDiskPath( "bad.scene.xml" ) ) == false );

    Util::write( domain, "good.scene.xml", Util::makeScene( "Good", Util::makeEntity( "1", "Plain", "0,0,0" ) ) );
    SW_REQUIRE_TRUE( doc.loadXMLFromDiskFile( domain.makeDiskPath( "good.scene.xml" ) ) );
    Scene scene{ "Good" };
    SW_REQUIRE_TRUE( scene.instantiate( doc ) );
    SW_EXPECT_LOG( "SceneDataLayers.LayerNotDeclared" );
    SW_EXPECT_TRUE( scene.getObjectManager()->getSubsystem<SceneDataLayerSubsystem>().setLayerActive( "Winter", true ) == false );
}

/** @brief [SceneAssemblyTest] 조립 칸이 쿠킹본을 왕복한다(저작 칸은 비우고 펼친 결과만 싣는다) */
SW_TEST_CASE( SceneAssemblyTest, BinaryKeepsExpandedResult )
{
    const test::ScopedScratchDomain domain;
    Util::write( domain, "inner.scene.xml", Util::makeInnerScene() );
    const string attribute = Util::instanceAttribute( "inner.scene.xml" ) + " layers=\"Winter\"";
    Util::write( domain, "outer.scene.xml", Util::makeScene( "Outer", Util::makeEntity( "1", "House", "0,0,0", attribute.c_str() ), "<_listDataLayer><SceneDataLayerDef _name=\"Winter\" _bInitiallyActive=\"true\" /></_listDataLayer>" ) );
    SceneDocument doc{};
    SW_REQUIRE_TRUE( doc.loadXMLFromDiskFile( domain.makeDiskPath( "outer.scene.xml" ) ) );
    doc.stripAuthoringInstanceData();
    SW_REQUIRE_TRUE( doc.saveBinary( Util::makeAbsolute( domain.makeDiskPath( "outer.scene.bin" ) ) ) );

    SceneDocument cooked{};
    SW_REQUIRE_TRUE( cooked.loadBinary( test::ScopedScratchDomain::makeID( "outer.scene.bin" ) ) );
    SW_REQUIRE_EQUAL( size_t{ 3 }, cooked._listSceneObjectNode.size() );
    const SceneDocument::SceneObjectNode* pLid = Util::findNode( cooked, "House/Lid" );
    SW_REQUIRE_TRUE( pLid != nullptr );
    SW_EXPECT_EQUAL( uint64{ 1 }, pLid->_instanceAnchorFileID );
    SW_EXPECT_NOT_EQUAL( uint64{ 0 }, pLid->_idScope );
    SW_EXPECT_EQUAL( size_t{ 1 }, pLid->_listDataLayer.size() ); // 앵커의 레이어를 물려받았다
    SW_EXPECT_TRUE( pLid->_instancePartPath.empty() );

    Scene scene{ "Cooked" };
    SW_REQUIRE_TRUE( scene.instantiate( cooked ) );
    GameObject* pHouseLid = scene.getObjectManager()->findGameObjectByName( hashed_string( "House/Lid" ) );
    SW_REQUIRE_TRUE( pHouseLid != nullptr );
    SW_EXPECT_TRUE( pHouseLid->getParent() == scene.getObjectManager()->findGameObjectByName( hashed_string( "House/Box" ) ) );
}

/** @brief [SceneAssemblyTest] 에셋 리다이렉트를 조립 기록에 반영한다 — 씬 상속의 부모 · 씬 인스턴스 앵커의 씬 · 엔티티의 프리팹이 새 id 로, 맵에 없는 경로는 그대로다(감사 P0-4) */
SW_TEST_CASE( SceneAssemblyTest, RemapAssetPathsFollowsTheMove )
{
    Scene                   scene{ "RemapProbe" };
    Scene::AssemblySnapshot snapshot = scene.captureAssembly();
    snapshot._baseScenePath          = "scratch/remap/base.scene.xml";
    snapshot._mapSceneInstancePath.insert_or_assign( uint64{ 7 }, string( "scratch/remap/inner.scene.xml" ) );
    snapshot._mapSceneInstancePath.insert_or_assign( uint64{ 8 }, string( "scratch/remap/kept.scene.xml" ) );
    scene.restoreAssembly( std::move( snapshot ) );
    scene.setEntityPrefabPath( 9, "scratch/remap/crate.prefab.xml" );

    unordered_map<string, string> mapOldToNew;
    mapOldToNew.insert_or_assign( string( "scratch/remap/base.scene.xml" ), string( "scratch/moved/base.scene.xml" ) );
    mapOldToNew.insert_or_assign( string( "scratch/remap/inner.scene.xml" ), string( "scratch/moved/inner.scene.xml" ) );
    mapOldToNew.insert_or_assign( string( "scratch/remap/crate.prefab.xml" ), string( "scratch/moved/crate.prefab.xml" ) );
    SW_EXPECT_EQUAL( 3u, scene.remapAssetPaths( mapOldToNew ) );
    SW_EXPECT_EQUAL( string( "scratch/moved/base.scene.xml" ), scene.getBaseScenePath() );
    SW_EXPECT_TRUE( scene.getSceneInstancePath( 7 ) == "scratch/moved/inner.scene.xml" );
    SW_EXPECT_TRUE( scene.getSceneInstancePath( 8 ) == "scratch/remap/kept.scene.xml" );
    SW_EXPECT_TRUE( scene.getEntityPrefabPath( 9 ) == "scratch/moved/crate.prefab.xml" );
}

/**
 * @brief [SceneAssemblyTest] 레이어를 껐다 켜면 묶음 오브젝트는 끌 때의 상태(자리 · 런타임 칸)로 다시 선다 — 스트리밍 셀과 같이 내릴 때 떠 둔다. 저장은 문서 그대로다
 */
SW_TEST_CASE( SceneAssemblyTest, DataLayerKeepsStateWhenTurnedOffAndOn )
{
    const test::ScopedScratchDomain domain;
    Util::write( domain, "layerstate.scene.xml",
                 Util::makeScene( "LayerState", Util::makeEntity( "1", "Always", "0,0,0" ) + Util::makeEntity( "2", "Snow", "1,0,0", "layers=\"Winter\"" ),
                                  "<_listDataLayer><SceneDataLayerDef _name=\"Winter\" _bInitiallyActive=\"true\" /></_listDataLayer>" ) );
    SceneDocument doc{};
    SW_REQUIRE_TRUE( doc.loadXMLFromDiskFile( domain.makeDiskPath( "layerstate.scene.xml" ) ) );
    Scene scene{ "LayerState" };
    SW_REQUIRE_TRUE( scene.instantiate( doc ) );
    GameObjectManager&       objects = *scene.getObjectManager();
    SceneDataLayerSubsystem& layers  = objects.getSubsystem<SceneDataLayerSubsystem>();

    GameObject* pSnow = objects.findGameObjectByName( hashed_string( "Snow" ) );
    SW_REQUIRE_TRUE( pSnow != nullptr && pSnow->getPrimarySceneComponent() != nullptr );
    SW_EXPECT_TRUE( layers.isGroupObject( pSnow->getObjectID() ) );
    pSnow->getPrimarySceneComponent()->setLocalPosition( float3{ 5.0f, 0.0f, 0.0f } );
    StateHashTestComponent* pState = pSnow->addComponent<StateHashTestComponent>();
    SW_REQUIRE_NOT_NULL( pState );
    pState->_steps = 3;

    SW_REQUIRE_TRUE( layers.setLayerActive( "Winter", false ) );
    layers.update( 0.0f );
    objects.processDeferredDestruction();
    SW_EXPECT_TRUE( objects.findGameObjectByName( hashed_string( "Snow" ) ) == nullptr );
    SceneDocument written{};
    SW_REQUIRE_TRUE( scene.serializeToDocument( written ) );
    const SceneDocument::SceneObjectNode* pWritten = Util::findNode( written, "Snow" );
    SW_REQUIRE_TRUE( pWritten != nullptr );
    SW_EXPECT_TRUE( pWritten->_statePurpose == SerializePurpose::Authoring ); // 끈 묶음은 문서 그대로 다시 쓴다 — 떠 둔 스냅숏을 저작 파일에 쓰지 않는다

    SW_REQUIRE_TRUE( layers.setLayerActive( "Winter", true ) );
    layers.update( 0.0f );
    const GameObject* pBack = objects.findGameObjectByName( hashed_string( "Snow" ) );
    SW_REQUIRE_TRUE( pBack != nullptr && pBack->getPrimarySceneComponent() != nullptr );
    SW_EXPECT_EQUAL( 5.0f, pBack->getPrimarySceneComponent()->getLocalPosition()._x );
    const StateHashTestComponent* pBackState = pBack->getComponent<StateHashTestComponent>();
    SW_REQUIRE_NOT_NULL( pBackState );
    SW_EXPECT_EQUAL( 3, pBackState->_steps );
}

/**
 * @brief [SceneAssemblyTest] 데이터 레이어 세이브 몫은 켜진 레이어와 묶음 상태를 싣고, 비운 매니저(복원) 위에서 묶음이 그 상태로 다시 선다 — 비워도 켜진 레이어 · 묶음은 남는다
 */
SW_TEST_CASE( SceneAssemblyTest, DataLayerSaveStateRestoresOverAClearedManager )
{
    const test::ScopedScratchDomain domain;
    Util::write( domain, "layersave.scene.xml",
                 Util::makeScene( "LayerSave", Util::makeEntity( "1", "Always", "0,0,0" ) + Util::makeEntity( "2", "Snow", "1,0,0", "layers=\"Winter\"" ),
                                  "<_listDataLayer><SceneDataLayerDef _name=\"Winter\" _bInitiallyActive=\"true\" /></_listDataLayer>" ) );
    SceneDocument doc{};
    SW_REQUIRE_TRUE( doc.loadXMLFromDiskFile( domain.makeDiskPath( "layersave.scene.xml" ) ) );
    Scene scene{ "LayerSave" };
    SW_REQUIRE_TRUE( scene.instantiate( doc ) );
    GameObjectManager&       objects = *scene.getObjectManager();
    SceneDataLayerSubsystem& layers  = objects.getSubsystem<SceneDataLayerSubsystem>();

    GameObject* pSnow = objects.findGameObjectByName( hashed_string( "Snow" ) );
    SW_REQUIRE_TRUE( pSnow != nullptr && pSnow->getPrimarySceneComponent() != nullptr );
    pSnow->getPrimarySceneComponent()->setLocalPosition( float3{ 5.0f, 0.0f, 0.0f } );

    vector<uint8> bytes;
    layers.writeSaveState( bytes );
    SceneDataLayerSubsystem::SaveState state;
    SW_REQUIRE_TRUE( SceneDataLayerSubsystem::readSaveState( bytes.data(), bytes.size(), state ) );
    pSnow->getPrimarySceneComponent()->setLocalPosition( float3{ 9.0f, 0.0f, 0.0f } ); // 저장 뒤에 바꾼다 — 복원이 덮는다

    objects.clear();
    scene.rebindRestoredFileIDs( {} );
    SW_EXPECT_TRUE( layers.isLayerActive( "Winter" ) ); // 비워도 켜진 레이어는 남는다
    layers.applySaveState( std::move( state ) );
    layers.update( 0.0f );
    vector<GameObject*> listSnow;
    objects.findGameObjectsByBaseName( "Snow", listSnow );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listSnow.size() );
    SW_REQUIRE_TRUE( listSnow[0]->getPrimarySceneComponent() != nullptr );
    SW_EXPECT_EQUAL( 5.0f, listSnow[0]->getPrimarySceneComponent()->getLocalPosition()._x );
    SW_EXPECT_TRUE( layers.isGroupObject( listSnow[0]->getObjectID() ) );
}
