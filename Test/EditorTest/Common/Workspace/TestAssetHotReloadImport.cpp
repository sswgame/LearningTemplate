#include "pch.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Task/TaskManager.h"

#include "Editor/Common/Asset/AssetImportStamp.h"
#include "Editor/Common/Commands/EditorBackgroundIO.h"
#include "Editor/Common/Workspace/AssetHotReload.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Resource/AssetManager.h"
#include "Engine/Resource/Cache/IAssetCache.h"
#include "Engine/Resource/ResourcePaths.h"

#include "TestFramework/TestFramework.h"

// AssetHotReloadImportTest — 핫 리로드의 원본 임포트: 바뀐 원본만 임포트하고, 워커에서 돌며, 같은 원본이 또 바뀌면 앞 일을 멈추고 다시 하고,
// 결과 적용(캐시 리로드)은 임포트가 끝난 뒤 에디터 스레드에서만 일어난다.

namespace
{
    /** @brief 가짜 임포터의 호출을 세는 상자입니다. 임포터는 이 포인터로만 쓴다. */
    struct TestScopeProbe
    {
        sw::atomic<int32>      _hashCount{ 0 };
        sw::atomic<int32>      _importCount{ 0 };
        sw::vector<sw::string> _listImported; ///< 가져온 순서의 파일 이름. 작업 시스템 없이 한 스레드로 돌 때만 읽는다
    };

    /** @brief `fake_raw` 의 `.dat` 를 `fake` 의 `.out` 으로 복사하는 가짜 임포터입니다. 원본 해시 계산 횟수와 임포트 횟수를 센다. */
    class TestScopeRawImporter final : public sw::editor::IRawAssetImporter
    {
    public:
        explicit TestScopeRawImporter( TestScopeProbe* pProbe )
            : _pProbe{ pProbe }
        {
        }

        sw::string_view getRawFolderName() const override { return "fake_raw"; }
        sw::string_view getStampHeader() const override { return "FAKEIMPORT 1"; }
        sw::string_view getImportedLabel() const override { return "OUT"; }
        bool            isSourceFile( sw::string_view path ) const override { return sw::FileUtil::hasExtension( path, ".dat" ); }
        sw::string      makeImportedPath( sw::string_view sourcePath ) const override
        {
            return sw::editor::AssetImportStampUtil::makeImportedPath( sourcePath, "fake_raw", "fake", ".out" );
        }
        uint64 computeSourceHash( sw::string_view sourcePath, [[maybe_unused]] sw::string_view resourcePath ) const override
        {
            _pProbe->_hashCount.fetch_add( 1 );
            return sw::editor::AssetImportStampUtil::computeFileHash( sourcePath );
        }
        const utf8*        findUnsupportedReason( [[maybe_unused]] sw::string_view sourcePath ) const override { return nullptr; }
        [[nodiscard]] bool importSource( sw::string_view sourcePath, sw::string_view importedPath, [[maybe_unused]] sw::string_view resourcePath ) const override
        {
            sw::string text;
            if ( sw::FileUtil::readTextFile( sourcePath, text ) == false || sw::FileUtil::ensureParentDirectoryExists( importedPath ) == false )
                return false;
            _pProbe->_importCount.fetch_add( 1 );
            _pProbe->_listImported.push_back( sw::FileUtil::getFileNamePart( sourcePath ) );
            return sw::FileUtil::writeTextFile( importedPath, text );
        }

    private:
        TestScopeProbe* _pProbe;
    };

    [[nodiscard]] bool writeScopeSource( const sw::string& root, sw::string_view relativePath, sw::string_view text )
    {
        const sw::string path = sw::FileUtil::joinPath( root, relativePath );
        return sw::FileUtil::ensureParentDirectoryExists( path ) && sw::FileUtil::writeTextFile( path, text );
    }

    /** @brief 워커 안의 가짜 임포터가 쓰는 신호입니다. 막는 임포터는 풀어 줄 때까지 기다리고, 어느 스레드에서 돌았는지 남긴다. */
    struct TestImportGate
    {
        sw::atomic<int32>  _blockingRuns{ 0 };
        sw::atomic<int32>  _countingRuns{ 0 };
        sw::atomic<bool>   _bRelease{ false };
        sw::atomic<uint64> _workerThreadHash{ 0 };

        void reset()
        {
            _blockingRuns.store( 0 );
            _countingRuns.store( 0 );
            _bRelease.store( false );
            _workerThreadHash.store( 0 );
        }
    };

    TestImportGate g_importGate{};

    [[nodiscard]] uint64 hashCurrentThread()
    {
        return static_cast<uint64>( std::hash<std::thread::id>{}( std::this_thread::get_id() ) );
    }

    /** @brief 풀어 줄 때까지 막히는 임포터입니다(첫 호출만 막고, 풀린 뒤의 호출은 바로 지나간다). */
    bool blockingImport( [[maybe_unused]] sw::string_view relativePath )
    {
        g_importGate._workerThreadHash.store( hashCurrentThread() );
        g_importGate._blockingRuns.fetch_add( 1 );
        for ( int32 waited = 0; waited < 10000 && g_importGate._bRelease.load() == false; ++waited )
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
        }
        return true;
    }

    bool countingImport( [[maybe_unused]] sw::string_view relativePath )
    {
        g_importGate._countingRuns.fetch_add( 1 );
        return true;
    }

    template <typename TPredicate>
    [[nodiscard]] bool waitFor( TPredicate predicate, int32 timeoutMs = 10000 )
    {
        for ( int32 waited = 0; waited < timeoutMs; ++waited )
        {
            if ( predicate() )
                return true;
            std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
        }
        return predicate();
    }

    /** @brief 스코프 동안 워커가 있는 작업 시스템과 에셋 관리자를 지금 에디터 범위의 서비스로 걸어 둡니다 — 임포트 잡이 워커에서 돌고, 캐시 리로드가 에셋 관리자를 찾는다. */
    class TestScopedEditorServices
    {
    public:
        explicit TestScopedEditorServices( uint32 workerCount )
            : _manager{}
            , _resources{}
            , _scope{}
            , _activeScope{ _scope }
            , _bReady{ _manager.initialize( workerCount ) && _scope.bind( &_manager ) && _scope.bind( &_resources ) }
        {
        }

        bool              isReady() const { return _bReady; }
        sw::AssetManager& getResources() { return _resources; }

        TestScopedEditorServices( const TestScopedEditorServices& )            = delete;
        TestScopedEditorServices& operator=( const TestScopedEditorServices& ) = delete;

    private:
        sw::TaskManager               _manager;
        sw::AssetManager              _resources;
        sw::SubsystemScope            _scope;
        sw::editor::ScopedEditorScope _activeScope;
        bool                          _bReady;
    };

    /** @brief 캐시 리로드가 어느 스레드에서 불렸는지 남기는 모듈 캐시 흉내입니다(경로 하나만 든다). */
    class ThreadProbeCache final : public sw::IAssetCache
    {
    public:
        const utf8* getAssetKindName() const override { return "ThreadProbe"; }
        bool        isCached( sw::string_view relativePath ) const override { return relativePath == "data/held.interactions.xml"; }
        void        reload( sw::string_view, sw::IRHIDevice* ) override
        {
            ++_reloadCount;
            _reloadThreadHash = hashCurrentThread();
        }
        size_t getCachedCount() const override { return 1u; }
        void   clear() override {}

        uint32 _reloadCount{ 0 };
        uint64 _reloadThreadHash{ 0 };
    };
} // namespace

/**
 * @brief [AssetHotReloadImportTest] 바뀐 원본만 대조하고 임포트한다 — 루트 전체를 훑지 않고, 범위 밖 스탬프 줄은 그대로 두며, 범위 밖 어긋남은 보정하지 않는다
 * @details 원본 넷(폴더 둘)을 임포트한 뒤 하나만 바꿔 그 경로로 `importSources` 를 부른다. 원본 해시는 그 하나만 읽고(전체 훑기는 4 번), 다른 폴더는 열지도 않는다.
 *          구분자 · 대소문자가 달라도 같은 원본이다. 전체 대조(`importAll`)가 그 뒤에도 깨끗해야 스탬프 줄이 보존된 것이다.
 *          변이: 범위를 무시하고 전체를 훑으면 해시 횟수가 4 가 되고, 범위 밖 줄을 지우면 마지막 대조가 어긋남을 보고한다.
 */
SW_TEST_CASE( AssetHotReloadImportTest, ImportSourcesTouchesOnlyTheChangedSource )
{
    const sw::string root = test::makeTempDirectory( "import_scope" );
    SW_ASSERT_TRUE( writeScopeSource( root, "game/test/fake_raw/a.dat", "a1;" ) && writeScopeSource( root, "game/test/fake_raw/b.dat", "b1;" ) &&
                    writeScopeSource( root, "game/test/fake_raw/c.dat", "c1;" ) && writeScopeSource( root, "game/other/fake_raw/d.dat", "d1;" ) );

    TestScopeProbe                 probe;
    const TestScopeRawImporter     importer{ &probe };
    sw::editor::AssetImportSummary summary = sw::editor::AssetImportStampUtil::importAll( root, importer, sw::editor::AssetImportMode::ImportStale );
    SW_ASSERT_EQUAL( 4u, summary._importedCount );
    SW_ASSERT_EQUAL( 4, probe._hashCount.load() );

    // b 만 바뀌었다 — b 하나만 읽고 임포트한다.
    SW_ASSERT_TRUE( writeScopeSource( root, "game/test/fake_raw/b.dat", "b2;" ) );
    probe._hashCount.store( 0 );
    probe._importCount.store( 0 );
    probe._listImported.clear();
    summary = sw::editor::AssetImportStampUtil::importSources( root, importer, { "game/test/fake_raw/b.dat" } );
    SW_EXPECT_EQUAL( 1u, summary._sourceCount );
    SW_EXPECT_EQUAL( 1u, summary._importedCount );
    SW_EXPECT_TRUE( summary.isClean() );
    SW_EXPECT_EQUAL( 1, probe._hashCount.load() ); // 루트 전체를 훑었다면 4
    SW_ASSERT_EQUAL( size_t( 1 ), probe._listImported.size() );
    SW_EXPECT_STREQ( "b.dat", probe._listImported[0].c_str() );
    sw::string outText;
    SW_ASSERT_TRUE( sw::FileUtil::readTextFile( sw::FileUtil::joinPath( root, "game/test/fake/b.out" ), outText ) );
    SW_EXPECT_STREQ( "b2;", outText.c_str() );

    // 다른 원본의 스탬프 줄은 보존됐다 — 전체 대조가 깨끗하다.
    SW_EXPECT_TRUE( sw::editor::AssetImportStampUtil::importAll( root, importer, sw::editor::AssetImportMode::CheckOnly ).isClean() );

    // 내용이 그대로이면(저장만 다시 했다) 임포트하지 않는다.
    probe._importCount.store( 0 );
    summary = sw::editor::AssetImportStampUtil::importSources( root, importer, { "game/test/fake_raw/b.dat" } );
    SW_EXPECT_EQUAL( 0u, summary._importedCount );
    SW_EXPECT_EQUAL( 0, probe._importCount.load() );

    // 구분자 · 대소문자가 달라도 같은 원본이다.
    SW_ASSERT_TRUE( writeScopeSource( root, "game/test/fake_raw/c.dat", "c2;" ) );
    summary = sw::editor::AssetImportStampUtil::importSources( root, importer, { "GAME\\test\\fake_raw\\C.DAT" } );
    SW_EXPECT_EQUAL( 1u, summary._importedCount );

    // 범위 밖에서 어긋난 것(a · d)은 보정하지 않는다 — 전체 대조가 그 둘을 보고한다.
    SW_ASSERT_TRUE( writeScopeSource( root, "game/test/fake_raw/a.dat", "a2;" ) && writeScopeSource( root, "game/other/fake_raw/d.dat", "d2;" ) );
    summary = sw::editor::AssetImportStampUtil::importSources( root, importer, { "game/test/fake_raw/c.dat" } );
    SW_EXPECT_EQUAL( 0u, summary._importedCount );
    SW_EXPECT_EQUAL( size_t( 2 ), sw::editor::AssetImportStampUtil::importAll( root, importer, sw::editor::AssetImportMode::CheckOnly )._listProblem.size() );

    // 원본이 없는 경로는 아무 일도 아니다.
    summary = sw::editor::AssetImportStampUtil::importSources( root, importer, { "game/test/fake_raw/none.dat" } );
    SW_EXPECT_EQUAL( 0u, summary._sourceCount );
    SW_EXPECT_TRUE( summary.isClean() );
}

/**
 * @brief [AssetHotReloadImportTest] 임포트 잡은 워커에서 돌고, 도는 동안 에디터 스레드는 막히지 않으며, 하나씩만 돈다
 * @details 막는 임포터를 워커에 올리고 `take()` 가 `Running` 을 주는지(에디터 스레드가 기다리지 않는다), 같은 때 두 번째 `request` 가 거절되는지(두 워커가 같은 결과 자리를 밟지 않는다),
 *          임포터가 에디터 스레드가 아닌 스레드에서 불렸는지 본다.
 */
SW_TEST_CASE( AssetHotReloadImportTest, ImportJobRunsOnAWorkerWithoutBlockingTheEditorThread )
{
    g_importGate.reset();
    TestScopedEditorServices services{ 2 };
    SW_ASSERT_TRUE( services.isReady() );

    sw::editor::EditorAssetImportJob              job;
    sw::vector<sw::editor::EditorAssetImportItem> listItem;
    listItem.push_back( sw::editor::EditorAssetImportItem{ "game/test/models_raw/a.glb", &blockingImport } );
    listItem.push_back( sw::editor::EditorAssetImportItem{ "game/test/models_raw/b.glb", &countingImport } );
    SW_ASSERT_TRUE( job.request( listItem ) );
    SW_ASSERT_TRUE( waitFor( []
    { return g_importGate._blockingRuns.load() == 1; } ) );

    sw::editor::EditorAssetImportResult result;
    SW_EXPECT_TRUE( job.take( result ) == sw::editor::EditorTaskPoll::Running ); // 임포터가 막혀 있어도 에디터 스레드는 기다리지 않는다
    SW_EXPECT_FALSE( job.request( listItem ) );                                  // 하나씩만 돈다

    g_importGate._bRelease.store( true );
    sw::editor::EditorTaskPoll poll = sw::editor::EditorTaskPoll::Running;
    SW_ASSERT_TRUE( waitFor( [&]
    { poll = job.take( result ); return poll != sw::editor::EditorTaskPoll::Running; } ) );
    SW_ASSERT_TRUE( poll == sw::editor::EditorTaskPoll::Ready );
    SW_ASSERT_EQUAL( size_t( 2 ), result._listItem.size() );
    SW_EXPECT_TRUE( result._listItem[0]._bRan && result._listItem[0]._bHandled );
    SW_EXPECT_TRUE( result._listItem[1]._bRan && result._listItem[1]._bHandled );
    SW_EXPECT_FALSE( result._bCanceled );
    SW_EXPECT_TRUE( g_importGate._workerThreadHash.load() != 0 );
    SW_EXPECT_TRUE( g_importGate._workerThreadHash.load() != hashCurrentThread() ); // 임포터는 에디터 스레드가 아닌 곳에서 돌았다
}

/**
 * @brief [AssetHotReloadImportTest] 취소하면 도는 원본 하나만 끝까지 가고 나머지는 건너뛴다
 */
SW_TEST_CASE( AssetHotReloadImportTest, CancelSkipsTheSourcesAfterTheRunningOne )
{
    g_importGate.reset();
    TestScopedEditorServices services{ 2 };
    SW_ASSERT_TRUE( services.isReady() );

    sw::editor::EditorAssetImportJob              job;
    sw::vector<sw::editor::EditorAssetImportItem> listItem;
    listItem.push_back( sw::editor::EditorAssetImportItem{ "game/test/models_raw/a.glb", &blockingImport } );
    listItem.push_back( sw::editor::EditorAssetImportItem{ "game/test/models_raw/b.glb", &countingImport } );
    listItem.push_back( sw::editor::EditorAssetImportItem{ "game/test/models_raw/c.glb", &countingImport } );
    SW_ASSERT_TRUE( job.request( std::move( listItem ) ) );
    SW_ASSERT_TRUE( waitFor( []
    { return g_importGate._blockingRuns.load() == 1; } ) );

    job.cancel();
    g_importGate._bRelease.store( true );
    sw::editor::EditorAssetImportResult result;
    SW_ASSERT_TRUE( waitFor( [&]
    { return job.take( result ) == sw::editor::EditorTaskPoll::Ready; } ) );
    SW_ASSERT_EQUAL( size_t( 3 ), result._listItem.size() );
    SW_EXPECT_TRUE( result._bCanceled );
    SW_EXPECT_TRUE( result._listItem[0]._bRan );
    SW_EXPECT_FALSE( result._listItem[1]._bRan );
    SW_EXPECT_FALSE( result._listItem[2]._bRan );
    SW_EXPECT_EQUAL( 0, g_importGate._countingRuns.load() ); // 건너뛴 원본의 임포터는 부르지 않았다
}

/**
 * @brief [AssetHotReloadImportTest] 같은 원본이 임포트 중에 또 바뀌면 앞 잡의 남은 일을 버리고 그 원본을 다시 임포트한다
 * @details 원본 둘(A, B)의 잡이 A 에서 막혀 있을 때 A 가 또 바뀐다. 앞 잡은 B 를 건너뛰고 끝나고, A 와 B 가 다시 올라가 A 는 모두 두 번, B 는 한 번 임포트된다.
 *          끝난 수는 3(앞 잡의 A, 다음 잡의 A · B)이고, 임포트가 모두 끝나야 busy 가 풀린다.
 */
SW_TEST_CASE( AssetHotReloadImportTest, SourceChangedAgainDuringImportRunsAgain )
{
    g_importGate.reset();
    TestScopedEditorServices services{ 2 };
    SW_ASSERT_TRUE( services.isReady() );

    sw::editor::AssetHotReload reload;
    SW_EXPECT_FALSE( reload.isImportBusy() );
    reload.requestSourceImport( "game/test/models_raw/a.glb", &blockingImport );
    reload.requestSourceImport( "game/test/models_raw/b.glb", &countingImport );
    reload.requestSourceImport( "game/test/models_raw/b.glb", &countingImport ); // 기다리는 같은 원본은 합쳐진다
    SW_EXPECT_EQUAL( size_t( 2 ), reload.getQueuedImportCount() );
    SW_EXPECT_TRUE( reload.isImportBusy() );

    reload.update(); // 잡을 시작한다
    SW_EXPECT_EQUAL( size_t( 2 ), reload.getRunningImportCount() );
    SW_EXPECT_EQUAL( size_t( 0 ), reload.getQueuedImportCount() );
    SW_ASSERT_TRUE( waitFor( []
    { return g_importGate._blockingRuns.load() == 1; } ) );

    // A 가 임포트 중에 또 바뀌었다.
    reload.requestSourceImport( "GAME/test/models_raw/A.glb", &blockingImport );
    SW_EXPECT_EQUAL( size_t( 1 ), reload.getQueuedImportCount() );
    g_importGate._bRelease.store( true );

    SW_ASSERT_TRUE( waitFor( [&]
    { reload.update(); return reload.isImportBusy() == false; } ) );
    SW_EXPECT_EQUAL( 2, g_importGate._blockingRuns.load() ); // A 는 두 번
    SW_EXPECT_EQUAL( 1, g_importGate._countingRuns.load() ); // B 는 앞 잡에서 건너뛰고 다음 잡에서 한 번
    SW_EXPECT_EQUAL( uint64{ 3 }, reload.getFinishedImportCount() );
}

/**
 * @brief [AssetHotReloadImportTest] 임포트가 도는 동안 온 파일 변경(임포트 산출물)은 미루고, 임포트가 모두 끝난 뒤 에디터 스레드에서 캐시를 다시 읽는다
 * @details 임포트 결과는 여러 파일이고 쓰는 도중에 읽으면 안 된다. 막는 임포트가 도는 동안 캐시를 든 파일의 변경 사건을 넣으면 리로드가 일어나지 않고,
 *          풀어 `update` 가 임포트를 마친 뒤에야 한 번 일어나며, 그 리로드는 `update` 를 부른 스레드(에디터 스레드)에서 불린다.
 *          변이: 미루지 않으면 첫 단언이, 워커에서 리로드하면 마지막 단언이 깨진다.
 */
SW_TEST_CASE( AssetHotReloadImportTest, ChangedFilesWaitForTheImportAndReloadOnTheEditorThread )
{
    g_importGate.reset();
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );
    TestScopedEditorServices services{ 2 };
    SW_ASSERT_TRUE( services.isReady() );
    ThreadProbeCache probe;
    services.getResources().registerAssetCache( &probe );

    sw::editor::AssetHotReload reload;
    reload.requestSourceImport( "game/test/models_raw/a.glb", &blockingImport );
    reload.update();
    SW_ASSERT_TRUE( waitFor( []
    { return g_importGate._blockingRuns.load() == 1; } ) );

    const sw::FileChangeEvent changed{ sw::FileWatcherAction::Modified, sw::FileUtil::joinPath( sw::ResourcePaths::getRootFolderPath(), "data" ), "held.interactions.xml" };
    reload.onResourceFileChanged( changed );
    reload.onResourceFileChanged( changed ); // 같은 파일은 합쳐진다
    reload.update();
    SW_EXPECT_EQUAL( 0u, probe._reloadCount ); // 임포트가 도는 동안은 읽지 않는다

    g_importGate._bRelease.store( true );
    SW_ASSERT_TRUE( waitFor( [&]
    { reload.update(); return reload.isImportBusy() == false; } ) );
    SW_EXPECT_EQUAL( 1u, probe._reloadCount );
    SW_EXPECT_EQUAL( hashCurrentThread(), probe._reloadThreadHash ); // 적용은 에디터 스레드가 했다
    SW_EXPECT_TRUE( probe._reloadThreadHash != g_importGate._workerThreadHash.load() );

    // 임포트가 없으면 바로 읽는다.
    reload.onResourceFileChanged( changed );
    SW_EXPECT_EQUAL( 2u, probe._reloadCount );
    services.getResources().unregisterAssetCache( &probe );
}

/**
 * @brief [AssetHotReloadImportTest] 모델 · 텍스처 · 높이장 원본의 임포터는 워커 임포터로 올라 있고, 에디터 스레드 임포터(테마 토큰)와 `.dds` 는 그렇지 않다
 * @details 표에서 `Worker` 로 적히지 않은 임포터는 에디터 스레드에서 동기로 돈다. 긴 임포트가 에디터 스레드에 남지 않게 표의 종류를 고정한다.
 */
SW_TEST_CASE( AssetHotReloadImportTest, LongImportersAreRoutedToTheWorker )
{
    using sw::editor::AssetImportThread;
    using sw::editor::EditorAssetTypeRegistry;
    for ( const sw::string_view path : { sw::string_view( "game/empty/models_raw/crate.glb" ), sw::string_view( "game/empty/textures_raw/hero.png" ),
                                         sw::string_view( "game/empty/heightfields_raw/valley.png" ), sw::string_view( "game/empty/heightfields_raw/valley.r16" ) } )
    {
        const sw::editor::AssetReloadRoute route = EditorAssetTypeRegistry::findReloadRoute( path );
        SW_EXPECT_NOT_NULL( route._pfnImportSource );
        SW_EXPECT_TRUE( route._importThread == AssetImportThread::Worker );
    }
    // 임포트된 결과(.dds · .mesh)는 임포터가 없고 캐시가 다시 읽는다.
    for ( const sw::string_view path : { sw::string_view( "game/empty/textures/hero.dds" ), sw::string_view( "game/empty/models/crate.mesh" ) } )
    {
        SW_EXPECT_NULL( EditorAssetTypeRegistry::findReloadRoute( path )._pfnImportSource );
    }
}
