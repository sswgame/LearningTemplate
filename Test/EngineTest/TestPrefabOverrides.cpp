/**
 * @file TestPrefabOverrides.cpp
 * @brief 씬의 프리팹 인스턴스는 덮어쓴 것만 저장한다 — 프리팹 수정이 놓인 인스턴스에 퍼지고, 옛 문서(전체 상태)도 그대로 읽힌다.
 */
#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Object/Prefab/PrefabOverrides.h"
#include "Engine/Resource/AssetFormat.h"
#include "Engine/Resource/ResourceManager.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneCooker.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Format/Archive.h"

#include "TestFramework/TestFramework.h"

namespace sw
{
    namespace
    {
        struct PrefabOverridesTestInternal
        {
            /** @brief 상자 프리팹: 루트(위치 @p position · 스케일 @p scale), 루트에 붙은 "Socket", 메시 하나. */
            static PrefabAsset makeCratePrefab( const float3& position, const float3& scale )
            {
                GameObjectManager authoring;
                GameObject*       pSource = authoring.createGameObject( hashed_string( "Crate" ) );
                SceneComponent*   pRoot   = pSource->addComponent<SceneComponent>();
                SceneComponent*   pSocket = pSource->addComponent<SceneComponent>();
                pRoot->setLocalPosition( position );
                pRoot->setLocalScale( scale );
                pSocket->setComponentName( hashed_string( "Socket" ) );
                (void)pSocket->attachToComponent( pRoot ); // 붙지 않으면 아래 비교가 드러낸다
                (void)pSource->addComponent<MeshComponent>();
                PrefabAsset asset;
                asset.setFromGameObject( pSource );
                return asset;
            }

            /** @brief 프리팹을 저작 파일과 그 쿠킹본(Shipping 은 쿠킹본만 읽는다)으로 쓰고, 캐시에서 버려 다음 로드가 다시 읽게 합니다. */
            static bool writePrefab( const PrefabAsset& asset, const string& path )
            {
                if ( asset.saveToXmlFile( path ) == false || asset.saveToBinaryFile( AssetCookPath::toCookedPath( path ) ) == false )
                    return false;
                engine::getResourceManager().getPrefabManager().reload( path );
                return true;
            }

            /** @brief 문서에서 이름이 @p pName 인 엔티티입니다. */
            static const SceneDocument::EntityNode* findEntity( const SceneDocument& doc, const utf8* pName )
            {
                for ( const SceneDocument::EntityNode& entity : doc._listEntityNode )
                {
                    if ( entity._name == pName )
                        return &entity;
                }
                return nullptr;
            }
        };
    } // namespace
} // namespace sw

/**
 * @brief [PrefabOverridesTest] 덮어쓴 것은 다른 칸 · 지운 컴포넌트 · 더한 컴포넌트뿐이고, 원형에 얹으면 인스턴스의 상태가 그대로 나온다
 */
SW_TEST_CASE( PrefabOverridesTest, OverridesHoldOnlyWhatDiffersAndRebuildTheInstance )
{
    const sw::PrefabAsset prefab = sw::PrefabOverridesTestInternal::makeCratePrefab( sw::float3( 1.0f, 2.0f, 3.0f ), sw::float3( 2.0f, 2.0f, 2.0f ) );
    sw::string            baseState;
    SW_ASSERT_TRUE( sw::PrefabOverrides::makeBaseState( prefab, baseState ) );

    sw::GameObjectManager world;
    sw::GameObject*       pInstance = world.createGameObject( sw::hashed_string( "CrateA" ) );
    SW_ASSERT_TRUE( prefab.applyStateTo( pInstance ) );
    pInstance->setName( sw::hashed_string( "CrateA" ) );

    // 고치지 않은 인스턴스는 덮어쓴 것이 없다(이름은 엔티티가 든다).
    sw::string overrides;
    SW_ASSERT_TRUE( sw::PrefabOverrides::computeOverrides( sw::ObjectStateSerializer::saveToXmlString( pInstance ), baseState, overrides ) );
    SW_EXPECT_TRUE_MSG( overrides.empty(), overrides.c_str() );

    // 루트를 옮기고, 메시를 지우고, 이름표 단 컴포넌트를 더한다.
    pInstance->getPrimarySceneComponent()->setLocalPosition( sw::float3( 5.0f, 5.0f, 5.0f ) );
    SW_ASSERT_TRUE( pInstance->removeComponent( pInstance->getComponent<sw::MeshComponent>() ) );
    sw::SceneComponent* pExtra = pInstance->addComponent<sw::SceneComponent>();
    SW_ASSERT_NOT_NULL( pExtra );
    pExtra->setComponentName( sw::hashed_string( "Extra" ) );
    world.flushSceneTransforms();

    const sw::string instanceState = sw::ObjectStateSerializer::saveToXmlString( pInstance );
    SW_ASSERT_TRUE( sw::PrefabOverrides::computeOverrides( instanceState, baseState, overrides ) );
    // 루트의 덮어쓴 것은 위치 한 칸이다 — 같은 값(회전 · 스케일 · 부착)은 적지 않는다.
    SW_EXPECT_TRUE_MSG( overrides.find( "<Override key=\"SceneComponent#0\">\n\t\t<SceneComponent _localPosition=\"5,5,5\" />" ) != sw::string::npos,
                        overrides.c_str() );
    SW_EXPECT_TRUE_MSG( overrides.find( "Socket" ) == sw::string::npos, overrides.c_str() ); // 고치지 않은 컴포넌트도
    SW_EXPECT_TRUE_MSG( overrides.find( "<Remove key=\"MeshComponent#0\"" ) != sw::string::npos, overrides.c_str() );
    SW_EXPECT_TRUE_MSG( overrides.find( "<Add>" ) != sw::string::npos && overrides.find( "Extra" ) != sw::string::npos, overrides.c_str() );

    sw::string rebuiltState;
    SW_ASSERT_TRUE( sw::PrefabOverrides::makeInstanceState( baseState, overrides, "CrateA", rebuiltState ) );
    sw::GameObjectManager rebuiltWorld;
    sw::GameObject*       pRebuilt = rebuiltWorld.createGameObject( sw::hashed_string( "Rebuilt" ) );
    SW_ASSERT_TRUE( sw::ObjectStateSerializer::loadFromXmlString( pRebuilt, rebuiltState ) );
    SW_EXPECT_STREQ( instanceState.c_str(), sw::ObjectStateSerializer::saveToXmlString( pRebuilt ).c_str() );
}

/**
 * @brief [PrefabOverridesTest] 프리팹을 고치면 씬에 놓인 인스턴스에 퍼지고, 인스턴스가 덮어쓴 값은 남는다 — XML 씬과 쿠킹한 SCN1 이 같다
 * @details 씬이 인스턴스의 전체 상태를 저장하면 프리팹 수정은 이미 놓인 인스턴스에 닿지 않는다 — 스케일을 고친 프리팹을 다시 열어도 옛 스케일이다.
 *          언리얼 · 유니티처럼 덮어쓴 것만 저장하면 다시 열 때 프리팹 위에 얹으므로 퍼진다.
 */
SW_TEST_CASE( PrefabOverridesTest, PrefabEditReachesPlacedInstancesButNotTheirOverrides )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    const sw::string prefabPath = test::makeTempPath( "crate.prefab.xml" );
    SW_ASSERT_TRUE( sw::PrefabOverridesTestInternal::writePrefab(
        sw::PrefabOverridesTestInternal::makeCratePrefab( sw::float3( 1.0f, 2.0f, 3.0f ), sw::float3( 2.0f, 2.0f, 2.0f ) ), prefabPath ) );

    sw::SceneManager manager;
    SW_ASSERT_TRUE( manager.initialize() );
    sw::SceneDocument authored;
    for ( const utf8* pName : { "CrateA", "CrateB" } )
    {
        sw::SceneDocument::EntityNode entity;
        entity._name   = pName;
        entity._prefab = prefabPath;
        authored._listEntityNode.push_back( entity );
    }
    sw::Scene* pScene = manager.createScene( "PrefabEditWorld" );
    SW_ASSERT_TRUE( pScene->instantiate( authored ) );
    sw::GameObject* pPlacedA = pScene->getObjectManager()->findGameObjectByName( sw::hashed_string( "CrateA" ) );
    SW_ASSERT_NOT_NULL( pPlacedA );
    pPlacedA->getPrimarySceneComponent()->setLocalPosition( sw::float3( 5.0f, 5.0f, 5.0f ) ); // A 만 위치를 덮어쓴다

    sw::SceneDocument saved;
    SW_ASSERT_TRUE( pScene->serializeToDocument( saved ) );
    const sw::SceneDocument::EntityNode* pSavedA = sw::PrefabOverridesTestInternal::findEntity( saved, "CrateA" );
    const sw::SceneDocument::EntityNode* pSavedB = sw::PrefabOverridesTestInternal::findEntity( saved, "CrateB" );
    SW_ASSERT_TRUE( pSavedA != nullptr && pSavedB != nullptr );
    SW_EXPECT_TRUE( pSavedA->_embeddedXml.empty() && pSavedB->_embeddedXml.empty() ); // 전체 상태를 싣지 않는다
    SW_EXPECT_TRUE_MSG( pSavedA->_prefabOverrideXml.find( "_localPosition" ) != sw::string::npos, pSavedA->_prefabOverrideXml.c_str() );
    SW_EXPECT_TRUE_MSG( pSavedB->_prefabOverrideXml.empty(), pSavedB->_prefabOverrideXml.c_str() );
    const sw::string scenePath = test::makeTempPath( "prefab_edit.scene.xml" );
    SW_ASSERT_TRUE( saved.saveXml( scenePath ) );

    // 프리팹을 고친다 — 위치와 스케일 둘 다.
    SW_ASSERT_TRUE( sw::PrefabOverridesTestInternal::writePrefab(
        sw::PrefabOverridesTestInternal::makeCratePrefab( sw::float3( 9.0f, 9.0f, 9.0f ), sw::float3( 3.0f, 3.0f, 3.0f ) ), prefabPath ) );

    /** @brief 다시 지은 씬의 두 상자를 봅니다: A 는 덮어쓴 위치 + 새 스케일, B 는 새 위치 + 새 스케일. */
    struct Check
    {
        static void run( const sw::Scene* pBuilt, const utf8* pWhere )
        {
            sw::GameObject* pA = pBuilt->getObjectManager()->findGameObjectByName( sw::hashed_string( "CrateA" ) );
            sw::GameObject* pB = pBuilt->getObjectManager()->findGameObjectByName( sw::hashed_string( "CrateB" ) );
            SW_ASSERT_TRUE_MSG( pA != nullptr && pB != nullptr && pA->getPrimarySceneComponent() != nullptr && pB->getPrimarySceneComponent() != nullptr,
                                pWhere );
            SW_EXPECT_TRUE_MSG( pA->getPrimarySceneComponent()->getLocalPosition() == sw::float3( 5.0f, 5.0f, 5.0f ), pWhere );
            SW_EXPECT_TRUE_MSG( pA->getPrimarySceneComponent()->getLocalScale() == sw::float3( 3.0f, 3.0f, 3.0f ), pWhere );
            SW_EXPECT_TRUE_MSG( pB->getPrimarySceneComponent()->getLocalPosition() == sw::float3( 9.0f, 9.0f, 9.0f ), pWhere );
            SW_EXPECT_TRUE_MSG( pB->getPrimarySceneComponent()->getLocalScale() == sw::float3( 3.0f, 3.0f, 3.0f ), pWhere );
            SW_EXPECT_TRUE_MSG( pA->getComponents().size() == 3 && pA->getComponent<sw::MeshComponent>() != nullptr, pWhere );
        }
    };

    sw::SceneDocument reopened;
    SW_ASSERT_TRUE( reopened.loadXml( scenePath ) );
    sw::Scene* pReopened = manager.createScene( "PrefabEditWorldReopened" );
    SW_ASSERT_TRUE( pReopened->instantiate( reopened ) );
    Check::run( pReopened, "xml" );

    // 쿠킹한 씬도 같은 뜻이다 — 프리팹 엔티티는 덮어쓴 것을 SCN1 에 싣고, 로드가 쿠킹한 프리팹 위에 얹는다.
    (void)sw::SceneCooker::cookEntityState( reopened ); // 프리팹 엔티티는 굽지 않는다(덮어쓴 것 그대로 실린다)
    const sw::string binPath = test::makeTempPath( "prefab_edit.scene.bin" );
    SW_ASSERT_TRUE( reopened.saveBinary( binPath ) );
    sw::SceneDocument cooked;
    SW_ASSERT_TRUE( cooked.loadBinary( binPath ) );
    const sw::SceneDocument::EntityNode* pCookedA = sw::PrefabOverridesTestInternal::findEntity( cooked, "CrateA" );
    SW_ASSERT_NOT_NULL( pCookedA );
    SW_EXPECT_TRUE( pCookedA->_embeddedStateBytes.empty() && pCookedA->_prefabOverrideXml.empty() == false );
    sw::Scene* pCooked = manager.createScene( "PrefabEditWorldCooked" );
    SW_ASSERT_TRUE( pCooked->instantiate( cooked ) );
    Check::run( pCooked, "binary" );

    manager.shutdown();
}

/**
 * @brief [PrefabOverridesTest] 프리팹 엔티티가 전체 상태(`<GameObject>`)를 실으면 그 상태 그대로 읽히고, 다음 저장이 덮어쓴 것만 쓴다
 * @details 프리팹을 읽지 못한 채 저장하면 전체 상태를 적는다(`Scene::serializeToDocument`). 그 상태가 기준이므로 덮어쓴 값 · 지운 컴포넌트가
 *          그대로 나온다. 프리팹이 있을 때 저장하면 덮어쓴 것만 쓰고, 그것을 다시 읽어도 같은 오브젝트다.
 */
SW_TEST_CASE( PrefabOverridesTest, FullStatePrefabEntityIsReadAndResavedAsOverrides )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    const sw::string prefabPath = test::makeTempPath( "legacy_crate.prefab.xml" );
    SW_ASSERT_TRUE( sw::PrefabOverridesTestInternal::writePrefab(
        sw::PrefabOverridesTestInternal::makeCratePrefab( sw::float3( 1.0f, 2.0f, 3.0f ), sw::float3( 2.0f, 2.0f, 2.0f ) ), prefabPath ) );

    // 인스턴스는 위치를 덮어쓰고 메시를 지운 전체 상태다.
    const sw::string legacyScene = sw::string( "<Scene formatVersion=\"1\" name=\"Legacy\">\n"
                                               "\t<entities>\n"
                                               "\t\t<entity id=\"1\" name=\"Old\" prefab=\"" ) +
                                   prefabPath +
                                   "\">\n"
                                   "\t\t\t<GameObject _schemaVersion=\"0\" _name=\"Old\" _bActive=\"true\">\n"
                                   "\t\t\t\t<_listComponent>\n"
                                   "\t\t\t\t\t<SceneComponent _localPosition=\"7,7,7\" _localRotation=\"0,0,0\" _localScale=\"2,2,2\" />\n"
                                   "\t\t\t\t\t<SceneComponent _localPosition=\"0,0,0\" _localRotation=\"0,0,0\" _localScale=\"1,1,1\" "
                                   "_attachComponent=\"SceneComponent#0\" />\n"
                                   "\t\t\t\t</_listComponent>\n"
                                   "\t\t\t</GameObject>\n"
                                   "\t\t</entity>\n"
                                   "\t</entities>\n"
                                   "</Scene>\n";
    const sw::string legacyPath = test::makeTempPath( "legacy.scene.xml" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( legacyPath, legacyScene ) );

    sw::SceneManager manager;
    SW_ASSERT_TRUE( manager.initialize() );
    sw::SceneDocument legacy;
    SW_ASSERT_TRUE( legacy.loadXml( legacyPath ) );
    sw::Scene* pOpened = manager.createScene( "LegacyWorld" );
    SW_ASSERT_TRUE( pOpened->instantiate( legacy ) );
    sw::GameObject* pOld = pOpened->getObjectManager()->findGameObjectByName( sw::hashed_string( "Old" ) );
    SW_ASSERT_NOT_NULL( pOld );
    SW_EXPECT_TRUE( pOld->getPrimarySceneComponent()->getLocalPosition() == sw::float3( 7.0f, 7.0f, 7.0f ) );
    SW_EXPECT_TRUE( pOld->getComponent<sw::MeshComponent>() == nullptr ); // 그 상태가 기준 — 지운 메시는 없다
    const sw::string openedState = sw::ObjectStateSerializer::saveToXmlString( pOld );

    sw::SceneDocument resaved;
    SW_ASSERT_TRUE( pOpened->serializeToDocument( resaved ) );
    const sw::SceneDocument::EntityNode* pResaved = sw::PrefabOverridesTestInternal::findEntity( resaved, "Old" );
    SW_ASSERT_NOT_NULL( pResaved );
    SW_EXPECT_TRUE( pResaved->_embeddedXml.empty() );
    SW_EXPECT_TRUE_MSG( pResaved->_prefabOverrideXml.find( "<Remove key=\"MeshComponent#0\"" ) != sw::string::npos, pResaved->_prefabOverrideXml.c_str() );
    const sw::string resavedPath = test::makeTempPath( "legacy_resaved.scene.xml" );
    SW_ASSERT_TRUE( resaved.saveXml( resavedPath ) );
    sw::string resavedText;
    SW_ASSERT_TRUE( sw::FileUtil::readTextFile( resavedPath, resavedText ) );
    SW_EXPECT_TRUE_MSG( resavedText.find( "formatVersion=\"1\"" ) != sw::string::npos, resavedText.c_str() );

    sw::SceneDocument reread;
    SW_ASSERT_TRUE( reread.loadXml( resavedPath ) );
    sw::Scene* pReopened = manager.createScene( "LegacyWorldReopened" );
    SW_ASSERT_TRUE( pReopened->instantiate( reread ) );
    sw::GameObject* pReopenedOld = pReopened->getObjectManager()->findGameObjectByName( sw::hashed_string( "Old" ) );
    SW_ASSERT_NOT_NULL( pReopenedOld );
    SW_EXPECT_STREQ( openedState.c_str(), sw::ObjectStateSerializer::saveToXmlString( pReopenedOld ).c_str() );

    manager.shutdown();
}

/**
 * @brief [PrefabOverridesTest] 지금 판이 아닌 쿠킹 씬(SCN1 v2)은 읽지 않고 다시 구우라고 알린다 — 바이트를 v2 의 배치대로 손으로 쓴다
 * @details 쿠킹본은 쿠커가 매번 다시 굽는 산출물이다. 옛 배치를 읽어 주는 갈래를 두지 않는다 — 판이 다르면 배치를 짐작하지 않고 거절한다.
 */
SW_TEST_CASE( PrefabOverridesTest, CookedSceneOfAnotherVersionIsRefused )
{
    sw::Archive arch;
    arch << static_cast<uint32>( 0x53434E31u ); // 'SCN1'
    arch << static_cast<uint32>( 2 );           // v2: 이름 · 프리팹 · GUID · XML · 바이너리 상태 · 파일 id(덮어쓴 것 칸이 없다)
    arch << sw::string( "OldCooked" );
    arch << static_cast<uint32>( 1 );
    arch << sw::string( "Plain" ) << sw::string() << sw::string()
         << sw::string( "<GameObject _schemaVersion=\"0\" _name=\"Plain\" _bActive=\"false\"><_listComponent/></GameObject>" );
    arch << sw::vector<uint8>{};
    arch << static_cast<uint64>( 4 );
    const sw::string path = test::makeTempPath( "v2.scene.bin" );
    SW_ASSERT_TRUE( arch.saveFile( path ) );

    test::ScopedLogCollector logs;
    {
        test::ScopedDefensiveTestLog expected( "a cooked scene of another binary version" );
        sw::SceneDocument            doc;
        SW_EXPECT_FALSE( doc.loadBinary( path ) );
        SW_EXPECT_TRUE( doc._listEntityNode.empty() );
    }
    SW_EXPECT_TRUE_MSG( logs.countContaining( "Unsupported binary version 2" ) == 1, logs.joined().c_str() );
}
