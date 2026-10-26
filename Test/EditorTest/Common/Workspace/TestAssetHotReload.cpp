#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Editor/Common/Workspace/AssetHotReload.h"
#include "Editor/Common/Workspace/EditorAssetType.h"

#include "Engine/Animation/Sprite/SpriteClipAsset.h"
#include "Engine/Graphics/Shader/Binding/GPUSpriteInstanceData.h"
#include "Engine/Object/Component/2D/SpriteComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Resource/Cache/IAssetCache.h"
#include "Engine/Resource/ResourcePaths.h"

#include "TestFramework/TestFramework.h"

// AssetHotReloadTest — 에디터 핫 리로드의 씬 알림(누가 그 에셋을 쓰는지 리플렉션으로 찾는다).

namespace
{
    /** @brief 이름을 에디터가 모르는 모듈 캐시 흉내 — 한 경로만 들고 있다. */
    class HeldPathProbeCache final : public sw::IAssetCache
    {
    public:
        HeldPathProbeCache()
            : sw::IAssetCache{ sw::CacheTracking::uncounted( "test probe without lookups" ) }
        {
        }

        const utf8* getAssetKindName() const override { return "HeldPathProbe"; }
        bool        isCached( sw::string_view relativePath ) const override { return relativePath == "data/held.interactions.xml"; }
        void        reload( sw::string_view, sw::IRHIDevice* ) override { ++_reloadCount; }
        size_t      getCachedCount() const override { return 1u; }
        void        clear() override {}

        uint32 _reloadCount{ 0 };
    };
} // namespace

/**
 * @brief [AssetHotReloadTest] 모듈이 올린 데이터 표(상호작용 · 원소 규칙)는 이름 대신 그 경로를 든 캐시가 다시 읽는다
 * @details 에디터는 GameFramework 를 몰라 캐시 이름을 표에 적을 수 없다. 표 줄은 "그 경로를 든 캐시 모두" 를 뜻하고, 감시 목록에도 든다.
 */
SW_TEST_CASE( AssetHotReloadTest, ModuleDataTableReloadsInTheCacheHoldingItsPath )
{
    const sw::editor::AssetReloadRoute route = sw::editor::EditorAssetTypeRegistry::findReloadRoute( "data/held.interactions.xml" );
    SW_ASSERT_NOT_NULL( route._pCacheKindName );
    SW_EXPECT_TRUE( sw::string_view( route._pCacheKindName ).empty() );

    sw::AssetManager       resources;
    HeldPathProbeCache     probe;
    const sw::Subscription registration = resources.registerAssetCache( &probe );
    SW_EXPECT_EQUAL( 1u, sw::editor::AssetHotReload::reloadInCachesHolding( resources, "data/held.interactions.xml", nullptr ) );
    SW_EXPECT_EQUAL( 0u, sw::editor::AssetHotReload::reloadInCachesHolding( resources, "data/other.interactions.xml", nullptr ) );
    SW_EXPECT_EQUAL( 1u, probe._reloadCount );
}

/**
 * @brief [AssetHotReloadTest] 오디오 데이터(믹서 그래프 · 이벤트 라이브러리)는 에셋 핫 리로드가 그 경로를 든 엔진 캐시(`AudioData`)로 다시 읽는다
 * @details 에디터에 오디오 전용 감시기가 따로 없다. 오디오 서비스가 쓰지 않는 파일(게임이 로드하지 않은 라이브러리)은 아무 캐시도 들지 않아 다시 읽지 않는다.
 */
SW_TEST_CASE( AssetHotReloadTest, AudioDataReloadsThroughTheEngineAudioCache )
{
    const sw::string_view arrAudioPath[] = { "engine/audio/default.audiomixer.xml", "game/shooter3d/audio/shooter3d.audioevents.xml", "game/x/audio/X.AudioEvents.XML" };
    for ( const sw::string_view path : arrAudioPath )
    {
        const sw::editor::AssetReloadRoute route = sw::editor::EditorAssetTypeRegistry::findReloadRoute( path );
        SW_ASSERT_NOT_NULL( route._pCacheKindName );
        SW_EXPECT_TRUE( sw::string_view( route._pCacheKindName ).empty() );
        SW_EXPECT_NULL( route._pfnImportSource );
    }
    SW_EXPECT_NULL( sw::editor::EditorAssetTypeRegistry::findReloadRoute( "game/x/audio/theme.music.xml" )._pCacheKindName );

    sw::vector<sw::string> listSuffix{};
    sw::editor::EditorAssetTypeRegistry::appendReloadableSuffixes( listSuffix );
    bool bWatchesMixer  = false;
    bool bWatchesEvents = false;
    for ( const sw::string& suffix : listSuffix )
    {
        bWatchesMixer  = bWatchesMixer || suffix == ".audiomixer.xml";
        bWatchesEvents = bWatchesEvents || suffix == ".audioevents.xml";
    }
    SW_EXPECT_TRUE( bWatchesMixer && bWatchesEvents );

    sw::AssetManager resources;
    SW_EXPECT_NOT_NULL( resources.findAssetCache( "AudioData" ) );
    SW_EXPECT_EQUAL( 0u, sw::editor::AssetHotReload::reloadInCachesHolding( resources, "game/none/audio/none.audioevents.xml", nullptr ) );
}

/**
 * @brief [AssetHotReloadTest] 다시 읽은 에셋은 그 경로를 에셋 경로 프로퍼티로 든 컴포넌트에만 `onPropertyChanged` 로 알려진다
 * @details 컴포넌트에는 리로드 전용 코드가 없다 — 에디터가 `PROPERTY( AssetPath )` 값을 보고 인스펙터 편집과 같은 알림을 보낸다.
 *          경로 구분자가 달라도 같은 에셋이다.
 */
SW_TEST_CASE( AssetHotReloadTest, ReloadedAssetReachesComponentsThroughTheirAssetPathProperty )
{
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );
    const sw::string    clipPath = test::makeTempPath( "hot_reload_users.sprite.xml" );
    sw::SpriteClipAsset clip;
    clip._listFrame.resize( 2 );
    clip._listFrame[1]._uvRect = sw::float4{ 0.0f, 0.0f, 0.5f, 0.5f };
    SW_ASSERT_TRUE( clip.saveToFile( clipPath ) );

    sw::GameObjectManager manager;
    sw::GameObject*       pObject = manager.createGameObject( sw::hashed_string( "Sprite" ) );
    SW_ASSERT_NOT_NULL( pObject );
    sw::SpriteComponent* pSprite = pObject->addComponent<sw::SpriteComponent>();
    SW_ASSERT_NOT_NULL( pSprite );
    pSprite->setClipPath( ::test::makeTempResourceID( clipPath ) );
    pSprite->setClipFrame( 1 );
    SW_EXPECT_NEAR_EQUAL( 0.0f, pSprite->getSpriteInstanceData().getUvRect()._x, 1e-4f );

    clip._listFrame[1]._uvRect = sw::float4{ 0.5f, 0.5f, 0.5f, 0.5f };
    SW_ASSERT_TRUE( clip.saveToFile( clipPath ) );
    sw::AssetManager resources;
    sw::IAssetCache* pCache = resources.findAssetCache( "SpriteClip" );
    SW_ASSERT_NOT_NULL( pCache );
    pCache->reload( ::test::makeTempResourceID( clipPath ), nullptr );

    // 다른 경로는 아무에게도 알리지 않는다.
    SW_EXPECT_EQUAL( 0u, sw::editor::AssetHotReload::notifyAssetUsers( manager, "other/unrelated.sprite.xml" ) );
    SW_EXPECT_NEAR_EQUAL( 0.0f, pSprite->getSpriteInstanceData().getUvRect()._x, 1e-4f );

    // 구분자가 달라도 같은 경로다 — 클립 프로퍼티 하나에 알린다.
    sw::string spelledDifferently = ::test::makeTempResourceID( clipPath );
    for ( utf8& ch : spelledDifferently )
    {
        if ( ch == '/' )
            ch = '\\';
    }
    SW_EXPECT_EQUAL( 1u, sw::editor::AssetHotReload::notifyAssetUsers( manager, spelledDifferently ) );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pSprite->getSpriteInstanceData().getUvRect()._x, 1e-4f );
}

/**
 * @brief [AssetHotReloadTest] 변경 이벤트는 바뀐 파일의 리소스 id 로 모이고(중복 없이 소문자), 폴더가 생기거나 사라지면 · 한도를 넘으면 개별 파일을 모르는 것이 되어 전체를 다시 읽게 한다
 * @details 폴더의 내용 변경(Modified)은 안의 파일 사건이 따로 오므로 버린다. 울타리 표지 파일은 번호만 올리고 바뀐 파일로 세지 않는다. 꺼내면 기억이 비워진다.
 *          변이: 폴더 추가를 파일처럼 기록하면 안의 파일이 목록에 없는 채 증분 갱신이 통과해 낡은 색인이 남는다.
 */
SW_TEST_CASE( AssetHotReloadTest, TreeChangesAreTrackedPerFileAndFoldersForceAFullRescan )
{
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );
    const sw::string&          root = sw::ResourcePaths::getRootFolderPath();
    sw::editor::AssetHotReload reload;
    const auto                 makeEvent = [&root]( sw::FileWatcherAction action, const utf8* pDirectory, const utf8* pFileName )
    { return sw::FileChangeEvent{ action, sw::FileUtil::joinPath( root, pDirectory ), pFileName }; };

    reload.onResourceTreeChanged( makeEvent( sw::FileWatcherAction::Modified, "game/empty/data", "Items.xml" ) );
    reload.onResourceTreeChanged( makeEvent( sw::FileWatcherAction::RenamedNewName, "game/empty/data", "items.xml" ) ); // 저장은 새 이름으로 온다 — 같은 파일이다
    reload.onResourceTreeChanged( makeEvent( sw::FileWatcherAction::Modified, "game/empty", "data" ) );                 // 폴더의 내용 변경은 버린다
    SW_EXPECT_EQUAL( uint64{ 3 }, reload.getContentChangeSerial() );
    sw::vector<sw::string> listPath;
    SW_ASSERT_TRUE( reload.takeChangedPaths( listPath ) );
    SW_ASSERT_EQUAL( size_t{ 1 }, listPath.size() );
    SW_EXPECT_STREQ( "game/empty/data/items.xml", listPath[0].c_str() );
    SW_ASSERT_TRUE( reload.takeChangedPaths( listPath ) ); // 꺼내면 비워진다
    SW_EXPECT_TRUE( listPath.empty() );

    // 울타리 표지는 번호만 올린다.
    reload.onResourceTreeChanged( makeEvent( sw::FileWatcherAction::Added, "scratch", "contentfence.tmp" ) );
    SW_EXPECT_EQUAL( uint64{ 4 }, reload.getContentChangeSerial() );
    SW_ASSERT_TRUE( reload.takeChangedPaths( listPath ) );
    SW_EXPECT_TRUE( listPath.empty() );

    // 실제 감시자가 주는 모양: 디렉터리는 감시 루트이고 파일 이름은 그 아래 상대 경로다(`IFileWatcher::pushChange`). 이름만 견주던 때는 이 표지가 바뀐 파일로 샜다.
    reload.onResourceTreeChanged( sw::FileChangeEvent{ sw::FileWatcherAction::RenamedNewName, root, "scratch\\contentfence.tmp" } );
    SW_EXPECT_EQUAL( uint64{ 5 }, reload.getContentChangeSerial() );
    SW_ASSERT_TRUE( reload.takeChangedPaths( listPath ) );
    SW_EXPECT_TRUE( listPath.empty() );

    // 폴더가 생기면 안의 파일을 모른다.
    reload.onResourceTreeChanged( makeEvent( sw::FileWatcherAction::Modified, "game/empty/data", "items.xml" ) );
    reload.onResourceTreeChanged( makeEvent( sw::FileWatcherAction::Added, "game/empty", "newfolder" ) );
    SW_EXPECT_FALSE( reload.takeChangedPaths( listPath ) );
    SW_EXPECT_TRUE( listPath.empty() );
    SW_ASSERT_TRUE( reload.takeChangedPaths( listPath ) ); // 알 수 없음은 한 번 꺼내면 풀린다

    // 한도를 넘으면 모른다.
    for ( size_t index = 0; index <= sw::editor::AssetHotReload::kMaxTrackedChangedPaths; ++index )
    {
        const sw::string fileName = "file" + sw::to_string( index ) + ".xml";
        reload.onResourceTreeChanged( makeEvent( sw::FileWatcherAction::Modified, "game/empty/data", fileName.c_str() ) );
    }
    SW_EXPECT_FALSE( reload.takeChangedPaths( listPath ) );

    // 감시를 켜지 않았으면 울타리는 막지 않는다.
    SW_EXPECT_FALSE( reload.pollContentWatchFence() );
}
