// PrefabSpawnTemplateTest — 캐시가 로드에서 지은 스폰 원형(이진 본문)으로 스폰한 결과가 상태 글을 읽은 것과 같고, 모르는 타입 · 그 참조도 남는다(감사 A-15).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Object/Component/ComponentStableKey.h"
#include "Engine/Object/Component/MissingComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"
#include "Engine/Object/GameObject/Serialization/Tests/HandleTestComponents.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Object/Prefab/PrefabCache.h"

using namespace sw;

namespace
{
    struct PrefabSpawnTemplateTestUtil
    {
        static constexpr const utf8* kPrefab = "<Prefab formatVersion=\"0\" name=\"Crate\"><GameObject _schemaVersion=\"0\" _name=\"Crate\" _bActive=\"true\"><_listComponent>"
                                               "<SceneComponent _componentName=\"Root\" _bActive=\"true\" _localScale=\"2,2,2\" _localPosition=\"1,2,3\" />"
                                               "</_listComponent></GameObject></Prefab>";

        /** @brief 비교용 글입니다 — 같은 저작 문맥으로 씁니다. */
        static string saveState( const GameObject* pObject )
        {
            ObjectSaveOptions options{ SerializePurpose::Authoring };
            options._bOmitExternalParent = SW_TRUE;
            return ObjectStateSerializer::saveToXMLString( pObject, options );
        }

        /** @brief 오브젝트 상태 글(`<GameObject …>`)을 프리팹 파일 글로 감쌉니다. */
        static string wrapAsPrefab( const string& stateXML, const utf8* pName )
        {
            const size_t bodyStart = stateXML.find( "<GameObject" );
            SW_EXPECT_TRUE( bodyStart != string::npos );
            return string( "<Prefab formatVersion=\"0\" name=\"" ) + pName + "\">" + stateXML.substr( bodyStart ) + "</Prefab>";
        }
    };
    using Util = PrefabSpawnTemplateTestUtil;
} // namespace

/** @brief [PrefabSpawnTemplateTest] 원형으로 스폰한 오브젝트는 같은 상태 글을 읽은 오브젝트와 같은 글로 저장되고, 두 번째 스폰도 같다 */
SW_TEST_CASE( PrefabSpawnTemplateTest, SpawnMatchesTextState )
{
    const test::ScopedScratchDomain domain;
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "crate.prefab.xml" ), Util::kPrefab ) );
    const string prefabID = test::ScopedScratchDomain::makeID( "crate.prefab.xml" ).str();

    PrefabCache                         cache;
    const shared_ptr<const PrefabAsset> pAsset = cache.loadPrefab( prefabID );
    SW_REQUIRE_NOT_NULL( pAsset.get() );

    // 매니저마다 하나씩 스폰한다 — 같은 매니저면 두 번째가 번호 붙은 이름(`Crate_2`)을 받아 글이 이름에서 갈린다.
    GameObjectManager firstManager;
    GameObjectManager secondManager;
    GameObject*       pFirst  = cache.spawn( &firstManager, prefabID );
    GameObject*       pSecond = cache.spawn( &secondManager, prefabID );
    SW_REQUIRE_NOT_NULL( pFirst );
    SW_REQUIRE_NOT_NULL( pSecond );

    GameObjectManager textManager;
    GameObject*       pText = textManager.createGameObject( hashed_string( "Crate" ) );
    SW_REQUIRE_NOT_NULL( pText );
    SW_REQUIRE_TRUE( pAsset->applyStateTo( pText ) );

    const string textState = Util::saveState( pText );
    SW_EXPECT_TRUE( textState.find( "_localPosition=\"1,2,3\"" ) != string::npos );
    SW_EXPECT_EQUAL( textState, Util::saveState( pFirst ) );
    SW_EXPECT_EQUAL( textState, Util::saveState( pSecond ) );
}

/** @brief [PrefabSpawnTemplateTest] 캐시 밖에서 읽은 자산은 원형이 없어 원형 스폰이 오류로 멈춘다(글로 되돌아가 읽지 않는다) */
SW_TEST_CASE( PrefabSpawnTemplateTest, AssetOutsideCacheHasNoTemplate )
{
    SW_EXPECT_LOG( "PrefabAsset.SpawnTemplateMissing" );
    const test::ScopedScratchDomain domain;
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "crate.prefab.xml" ), Util::kPrefab ) );
    PrefabAsset asset;
    SW_REQUIRE_TRUE( asset.loadFromXMLFile( test::ScopedScratchDomain::makeID( "crate.prefab.xml" ) ) );

    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Crate" ) );
    SW_REQUIRE_NOT_NULL( pObject );
    SW_EXPECT_FALSE( asset.applySpawnTemplateTo( pObject ) );
}

/**
 * @brief [PrefabSpawnTemplateTest] 모르는 타입의 컴포넌트와 그것을 가리키는 핸들이 원형을 지나도 남는다 — 스폰한 인스턴스를 다시 글로 쓰면 원래 원소 · 참조 글이 그대로다(A-1)
 */
SW_TEST_CASE( PrefabSpawnTemplateTest, MissingComponentAndHandleSurviveTemplate )
{
    SW_EXPECT_LOG( "ObjectStateSerializer.ComponentUnknownTypeKept" );
    SW_EXPECT_LOG( "Object.HandleKeptForMissingComponent" );
    const test::ScopedScratchDomain domain;

    // 대상과 그것을 가리키는 핸들을 가진 오브젝트를 글로 쓰고, 대상 타입 이름만 바꿔 "모듈이 안 뜬 타입" 으로 만든다.
    GameObjectManager authorManager;
    GameObject*       pAuthor = authorManager.createGameObject( hashed_string( "Holder" ) );
    SW_REQUIRE_NOT_NULL( pAuthor );
    HandleTargetTestComponent* pTarget = pAuthor->addComponent<HandleTargetTestComponent>();
    HandleHolderTestComponent* pHolder = pAuthor->addComponent<HandleHolderTestComponent>();
    SW_REQUIRE_NOT_NULL( pTarget );
    SW_REQUIRE_NOT_NULL( pHolder );
    pHolder->_target      = TComponentHandle<HandleTargetTestComponent>{ pTarget };
    const string saved    = Util::saveState( pAuthor );
    const string key      = "0:" + ComponentStableKey::makeKey( pTarget );
    const string typeName = HandleTargetTestComponent::StaticType()->_name.c_str();
    const string goneName = typeName + "Gone";
    const string gone     = StringUtil::replace( StringUtil::replace( saved, "<" + typeName, "<" + goneName ), "</" + typeName + ">", "</" + goneName + ">" );
    SW_REQUIRE_TRUE( gone.find( key ) != string::npos );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "holder.prefab.xml" ), Util::wrapAsPrefab( gone, "Holder" ) ) );

    PrefabCache       cache;
    GameObjectManager spawnManager;
    GameObject*       pSpawned = cache.spawn( &spawnManager, test::ScopedScratchDomain::makeID( "holder.prefab.xml" ).str() );
    SW_REQUIRE_NOT_NULL( pSpawned );
    SW_EXPECT_NOT_NULL( pSpawned->getComponent<MissingComponent>() );
    const HandleHolderTestComponent* pSpawnedHolder = pSpawned->getComponent<HandleHolderTestComponent>();
    SW_REQUIRE_NOT_NULL( pSpawnedHolder );
    SW_EXPECT_TRUE( pSpawnedHolder->_target.isValid() );                        // 비우지 않았다
    SW_EXPECT_NULL( spawnManager.resolveComponent( pSpawnedHolder->_target ) ); // 다만 찾으면 아무것도 아니다

    const string resaved = Util::saveState( pSpawned );
    SW_EXPECT_TRUE( resaved.find( key ) != string::npos );
    SW_EXPECT_TRUE( resaved.find( "<" + goneName ) != string::npos );
}
