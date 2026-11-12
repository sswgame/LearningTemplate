// PrefabSpawnTemplateTest — 캐시가 로드에서 지은 스폰 원형(이진 본문)으로 스폰한 결과가 상태 글을 읽은 것과 같고, 모르는 타입 · 그 참조도 남는다(감사 A-15).
// 원형은 씬 밖 매니저(`GameObjectManager::TransientTag` — 훅 없음)가 짓고 워커 로드에서도 같으며, 쿠킹본의 펼친 중첩 노드도 원형으로 스폰한다.
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Asset/AssetFormat.h"
#include "Engine/Content/ContentSource.h"
#include "Engine/Content/Pack/PackManager.h"
#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Object/Component/ComponentStableKey.h"
#include "Engine/Object/Component/MissingComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
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

        /** @brief 루트 위치가 @p pPosition 인 프리팹 글입니다. @p nested 를 주면 루트에 붙는 중첩 인스턴스 원소를 더합니다. */
        static string makePrefab( const utf8* pName, const utf8* pPosition, const string& nested = {} )
        {
            return string( "<Prefab formatVersion=\"0\" name=\"" ) + pName + "\"><GameObject _schemaVersion=\"0\" _name=\"" + pName +
                   "\" _bActive=\"true\"><_listComponent><SceneComponent _componentName=\"Root\" _bActive=\"true\" _localPosition=\"" + pPosition +
                   "\" /></_listComponent></GameObject>" + nested + "</Prefab>";
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

    /** @brief 콘텐츠 정책을 바꿨다가 소멸자에서 되돌리는 가드입니다(정책은 프로세스 전역 팩 매니저의 값이다 — `MountedDiskDomainTest` 와 같은 모양). */
    struct ScopedCookedContent
    {
        PackManager&  _packs;
        ContentSource _previous;
        explicit ScopedCookedContent( PackManager& packs )
            : _packs{ packs }
            , _previous{ packs.getContentSource() }
        {
            _packs.setContentSource( ContentSource::Cooked );
        }
        ~ScopedCookedContent() { _packs.setContentSource( _previous ); }
        ScopedCookedContent( const ScopedCookedContent& )            = delete;
        ScopedCookedContent& operator=( const ScopedCookedContent& ) = delete;
    };
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

/** @brief [PrefabSpawnTemplateTest] 씬 밖 매니저(`TransientTag`)는 컴포넌트 훅을 부르지 않는다 — 같은 컴포넌트가 보통 매니저에서는 등록된다 */
SW_TEST_CASE( PrefabSpawnTemplateTest, TransientManagerCallsNoHooks )
{
    GameObjectManager transient( GameObjectManager::TransientTag{} );
    GameObject*       pTransientObject = transient.createGameObject( hashed_string( "Scratch" ) );
    SW_REQUIRE_NOT_NULL( pTransientObject );
    const SceneComponent* pTransientRoot = pTransientObject->addComponent<SceneComponent>();
    SW_REQUIRE_NOT_NULL( pTransientRoot );
    SW_EXPECT_FALSE( pTransientRoot->isRegistered() );

    GameObjectManager live;
    GameObject*       pLiveObject = live.createGameObject( hashed_string( "Live" ) );
    SW_REQUIRE_NOT_NULL( pLiveObject );
    const SceneComponent* pLiveRoot = pLiveObject->addComponent<SceneComponent>();
    SW_REQUIRE_NOT_NULL( pLiveRoot );
    SW_EXPECT_TRUE( pLiveRoot->isRegistered() );
}

/**
 * @brief [PrefabSpawnTemplateTest] 워커 로드(`request` — 비동기 시작)가 지은 원형으로 스폰해도 상태 글을 읽은 것과 같다
 * @details 원형은 씬 밖 매니저에서 짓는다 — 워커에서 컴포넌트 훅을 부르면 `GameObjectManager::registerComponent` 의 단언이 진다(감사 A-15).
 */
SW_TEST_CASE( PrefabSpawnTemplateTest, TemplateBuiltOnWorkerLoadMatchesTextState )
{
    const test::ScopedScratchDomain domain;
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "crate.prefab.xml" ), Util::kPrefab ) );
    const string prefabID = test::ScopedScratchDomain::makeID( "crate.prefab.xml" ).str();

    PrefabCache                         cache;
    const shared_ptr<const PrefabAsset> pAsset = cache.request( prefabID ).wait();
    SW_REQUIRE_NOT_NULL( pAsset.get() );
    GameObjectManager spawnManager;
    GameObject*       pSpawned = cache.spawn( &spawnManager, prefabID );
    SW_REQUIRE_NOT_NULL( pSpawned );

    GameObjectManager textManager;
    GameObject*       pText = textManager.createGameObject( hashed_string( "Crate" ) );
    SW_REQUIRE_NOT_NULL( pText );
    SW_REQUIRE_TRUE( pAsset->applyStateTo( pText ) );
    SW_EXPECT_EQUAL( Util::saveState( pText ), Util::saveState( pSpawned ) );
}

/**
 * @brief [PrefabSpawnTemplateTest] 쿠킹본(펼친 중첩 노드 — `_resolvedStateXML`)을 쿠킹 정책에서 스폰하면 노드는 로드 때 지은 원형으로 서고, 원본 글로 스폰한 부품과 같은 상태다
 * @details 쿠커와 같은 길로 쿠킹본을 쓴다(`flattenNestedInstances` → `saveToBinaryFile`). 쿠킹 정책은 원본(바퀴 프리팹)을 읽지 않는다.
 */
SW_TEST_CASE( PrefabSpawnTemplateTest, CookedFlattenedNodeSpawnsFromTemplate )
{
    const test::ScopedScratchDomain domain;
    const string                    wheelID = test::ScopedScratchDomain::makeID( "wheel.prefab.xml" ).str();
    const string                    cartID  = test::ScopedScratchDomain::makeID( "cart.prefab.xml" ).str();
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "wheel.prefab.xml" ), Util::makePrefab( "Wheel", "4,5,6" ) ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "cart.prefab.xml" ),
                                              Util::makePrefab( "Cart", "1,2,3", "<PrefabInstance name=\"Wheel\" prefab=\"" + wheelID + "\" />" ) ) );

    // 쿠커의 길: 펼쳐서 이진으로 쓴다(`PrefabCache::cookAllPrefabs`).
    PrefabAsset source;
    SW_REQUIRE_TRUE( source.loadFromDiskFile( domain.makeDiskPath( "cart.prefab.xml" ) ) );
    PrefabCache cooker;
    SW_REQUIRE_TRUE( cooker.flattenNestedInstances( source ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, source.getNestedInstances().size() );
    SW_REQUIRE_TRUE( source.getNestedInstances()[0].isResolved() );
    const optional<AbsolutePath> cookedFile = FileUtil::requireAbsolutePath( domain.makeDiskPath( AssetCookPath::toCookedPath( "cart.prefab.xml" ) ) );
    SW_REQUIRE_TRUE( cookedFile.has_value() );
    SW_REQUIRE_TRUE( source.saveToBinaryFile( *cookedFile ) );

    // 기준: 원본 글로 스폰한 바퀴 부품(소스 정책).
    PrefabCache       sourceCache;
    GameObjectManager sourceManager;
    SW_REQUIRE_NOT_NULL( sourceCache.spawn( &sourceManager, cartID ) );
    const GameObject* pSourceWheel = sourceManager.findGameObjectByName( hashed_string( "Wheel" ) );
    SW_REQUIRE_NOT_NULL( pSourceWheel );

    PackManager* pPacks = engine::findSubsystem<PackManager>(); // 테스트 호스트는 에셋 매니저가 팩 매니저를 건다
    SW_REQUIRE_NOT_NULL( pPacks );
    const ScopedCookedContent           cookedContent{ *pPacks };
    PrefabCache                         cookedCache;
    const shared_ptr<const PrefabAsset> pCooked = cookedCache.loadPrefab( cartID );
    SW_REQUIRE_NOT_NULL( pCooked.get() );
    SW_REQUIRE_EQUAL( size_t{ 1 }, pCooked->getNestedInstances().size() );
    SW_EXPECT_TRUE( pCooked->getNestedInstances()[0].isResolved() );
    GameObjectManager cookedManager;
    SW_REQUIRE_NOT_NULL( cookedCache.spawn( &cookedManager, cartID ) );
    const GameObject* pCookedWheel = cookedManager.findGameObjectByName( hashed_string( "Wheel" ) );
    SW_REQUIRE_NOT_NULL( pCookedWheel );
    const string cookedState = Util::saveState( pCookedWheel );
    SW_EXPECT_TRUE( cookedState.find( "_localPosition=\"4,5,6\"" ) != string::npos );
    SW_EXPECT_EQUAL( Util::saveState( pSourceWheel ), cookedState );
}
