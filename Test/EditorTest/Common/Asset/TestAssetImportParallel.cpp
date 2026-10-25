#include "pch.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/Task/TaskManager.h"

#include "Editor/Common/Asset/AssetImportStamp.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "TestFramework/TestFramework.h"

// AssetImportParallelTest — 일괄 임포트(`AssetImportStampUtil::importAll`)가 원본 파일을 워커와 나눠 임포트해도 출력 바이트 · 스탬프 · 요약(카운터 · 문제 목록 순서)이 직렬로 돌 때와 같다.

namespace
{
    /** @brief 가짜 임포터가 동시에 몇 개나 돌았는지 재는 상자입니다. 임포터는 이 포인터로만 쓴다(임포터 자신은 무상태다). */
    struct TestImportProbe
    {
        static constexpr int32 kOverlapWaitMs = 300;

        sw::atomic<int32>  _running{ 0 };
        sw::atomic<int32>  _maxRunning{ 0 };
        sw::atomic<uint32> _serviceUseCount{ 0 };     ///< 값으로 받은 서비스를 쓴 임포트 수(`TestHandoffRawImporter`)
        bool               _bWaitForOverlap{ false }; ///< 켜면 임포트 하나가 다른 임포트가 겹칠 때까지(최대 `kOverlapWaitMs`) 기다린다

        /** @brief 동시 실행 수를 올리고, 켜져 있으면 다른 임포트가 겹칠 때까지 기다립니다(직렬이면 시간 제한까지 기다리다 지나간다). */
        void enter()
        {
            const int32 running = _running.fetch_add( 1 ) + 1;
            _maxRunning.fetch_max( running );
            if ( _bWaitForOverlap == false )
                return;
            for ( int32 waitedMs = 0; waitedMs < kOverlapWaitMs && _maxRunning.load() < 2; ++waitedMs )
            {
                std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
            }
        }

        void leave() { _running.fetch_sub( 1 ); }
    };

    /**
     * @class TestFakeRawImporter
     * @brief `fake_raw` 폴더의 `.dat` · `.alt` · `.dep` 를 `fake` 폴더의 `.out` 으로 옮기는 가짜 임포터입니다. 모든 규칙(순서 의존 · 같은 자리 · 옆 폴더 지우기)을 한 벌씩 갖는다.
     * @details - `bad*` 는 지원하지 않는 원본, `fail*` 은 임포트가 실패한다.
     *          - `.dep` 는 같은 폴더의 `base.out` 을 읽는다(순서 의존). 아직 없으면 실패한다.
     *          - 그 밖의 원본은 결과 파일에 **이미 있던 내용 뒤에 원본을 붙여** 쓴다 — 같은 자리를 쓰는 두 원본(`same.alt` · `same.dat`)이 한 스레드에서 파일 순서대로 돌아야 같은 바이트가 나온다.
     *          - 원본 이름이 `x` 이면 옆 폴더(`fake/x/`)를 지운 뒤 잠깐 있다가 쓴다 — `x/y.dat` 의 결과가 같은 폴더 아래라 사슬로 묶이지 않으면 지워진다.
     */
    class TestFakeRawImporter final : public sw::editor::IRawAssetImporter
    {
    public:
        explicit TestFakeRawImporter( TestImportProbe* pProbe )
            : _pProbe{ pProbe }
        {
        }

        sw::string_view getRawFolderName() const override { return "fake_raw"; }
        sw::string_view getStampHeader() const override { return "FAKEIMPORT 1"; }
        sw::string_view getImportedLabel() const override { return "OUT"; }
        bool            isSourceFile( sw::string_view path ) const override { return sw::FileUtil::hasAnyExtension( path, { ".dat", ".alt", ".dep" } ); }
        sw::string      makeImportedPath( sw::string_view sourcePath ) const override
        {
            return sw::editor::AssetImportStampUtil::makeImportedPath( sourcePath, "fake_raw", "fake", ".out" );
        }
        uint64 computeSourceHash( sw::string_view sourcePath, [[maybe_unused]] sw::string_view resourcePath ) const override
        {
            return sw::editor::AssetImportStampUtil::computeFileHash( sourcePath );
        }
        const utf8* findUnsupportedReason( sw::string_view sourcePath ) const override
        {
            return sw::StringUtil::startsWith( sw::FileUtil::getFileNamePart( sourcePath ), "bad" ) ? "unsupported source" : nullptr;
        }
        bool               hasOrderDependency( sw::string_view resourcePath ) const override { return sw::FileUtil::hasExtension( resourcePath, ".dep" ); }
        [[nodiscard]] bool importSource( sw::string_view sourcePath, sw::string_view importedPath, [[maybe_unused]] sw::string_view resourcePath ) const override
        {
            const sw::string name = sw::FileUtil::removeExtension( sw::FileUtil::getFileNamePart( sourcePath ) );
            if ( sw::StringUtil::startsWith( name, "fail" ) )
                return false;

            _pProbe->enter();
            sw::string source;
            bool       bImported = sw::FileUtil::readTextFile( sourcePath, source );
            sw::string payload;
            if ( bImported && sw::FileUtil::hasExtension( sourcePath, ".dep" ) )
            {
                sw::string base;
                bImported = sw::FileUtil::readTextFile( sw::FileUtil::joinPath( sw::FileUtil::getDirectoryPart( importedPath ), "base.out" ), base );
                payload   = base + source;
            }
            else if ( bImported )
            {
                if ( name == "x" )
                {
                    bImported = sw::FileUtil::removeDirectory( sw::FileUtil::removeExtension( importedPath ) );
                    std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
                }
                sw::string existing;
                if ( sw::FileUtil::exists( importedPath ) )
                    bImported = bImported && sw::FileUtil::readTextFile( importedPath, existing );
                payload = existing + source;
            }
            bImported = bImported && sw::FileUtil::ensureParentDirectoryExists( importedPath ) && sw::FileUtil::writeTextFile( importedPath, payload );
            _pProbe->leave();
            return bImported;
        }

    private:
        TestImportProbe* _pProbe;
    };

    /**
     * @class TestHandoffRawImporter
     * @brief 제출 시점(테스트 스레드)에 찾은 작업 시스템을 값으로 받아 워커에서 쓰는 가짜 임포터입니다. `fake_raw` 의 `.dat` 를 `fake` 의 `.out` 으로 그대로 옮긴다.
     * @details 워커 본문(`importSource`)은 에디터 서비스 범위를 읽지 않는다 — 서비스는 생성자 인자로만 받는다(`TextureImporter` 가 밴드 압축의 작업 시스템을 받는 방식과 같다).
     */
    class TestHandoffRawImporter final : public sw::editor::IRawAssetImporter
    {
    public:
        TestHandoffRawImporter( sw::TaskManager* pTaskManager, TestImportProbe* pProbe )
            : _pTaskManager{ pTaskManager }
            , _pProbe{ pProbe }
        {
        }

        sw::string_view getRawFolderName() const override { return "fake_raw"; }
        sw::string_view getStampHeader() const override { return "HANDOFFIMPORT 1"; }
        sw::string_view getImportedLabel() const override { return "OUT"; }
        bool            isSourceFile( sw::string_view path ) const override { return sw::FileUtil::hasExtension( path, ".dat" ); }
        sw::string      makeImportedPath( sw::string_view sourcePath ) const override
        {
            return sw::editor::AssetImportStampUtil::makeImportedPath( sourcePath, "fake_raw", "fake", ".out" );
        }
        uint64 computeSourceHash( sw::string_view sourcePath, [[maybe_unused]] sw::string_view resourcePath ) const override
        {
            return sw::editor::AssetImportStampUtil::computeFileHash( sourcePath );
        }
        const utf8*        findUnsupportedReason( [[maybe_unused]] sw::string_view sourcePath ) const override { return nullptr; }
        [[nodiscard]] bool importSource( sw::string_view sourcePath, sw::string_view importedPath, [[maybe_unused]] sw::string_view resourcePath ) const override
        {
            _pProbe->enter();
            if ( _pTaskManager != nullptr && _pTaskManager->getWorkerCount() > 0 )
                _pProbe->_serviceUseCount.fetch_add( 1 );
            sw::string source;
            bool       bImported = sw::FileUtil::readTextFile( sourcePath, source );
            bImported            = bImported && sw::FileUtil::ensureParentDirectoryExists( importedPath ) && sw::FileUtil::writeTextFile( importedPath, source );
            _pProbe->leave();
            return bImported;
        }

    private:
        sw::TaskManager* _pTaskManager; ///< 제출 시점에 찾은 서비스(빌림)
        TestImportProbe* _pProbe;
    };

    /** @brief 스코프 동안 워커가 있는 작업 시스템을 지금 에디터 범위의 서비스로 걸어 둡니다 — 일괄 임포트가 이것으로 파일을 나눈다. */
    class TestScopedImportTaskManager
    {
    public:
        explicit TestScopedImportTaskManager( uint32 workerCount )
            : _manager{}
            , _scope{}
            , _activeScope{ _scope }
            , _bReady{ _manager.initialize( workerCount ) && _scope.bind( &_manager ) }
        {
        }

        bool isReady() const { return _bReady; }

        TestScopedImportTaskManager( const TestScopedImportTaskManager& )            = delete;
        TestScopedImportTaskManager& operator=( const TestScopedImportTaskManager& ) = delete;

    private:
        sw::TaskManager               _manager;
        sw::SubsystemScope            _scope;
        sw::editor::ScopedEditorScope _activeScope;
        bool                          _bReady;
    };

    /** @brief 한 번의 시나리오(임포트 → 소스 두 개를 바꿈 → 검사)의 결과입니다. */
    struct TestImportScenarioResult
    {
        sw::editor::AssetImportSummary _import;
        sw::editor::AssetImportSummary _check;
        sw::string                     _snapshot; ///< 임포트 직후 `game/test/` 아래 모든 파일의 상대 경로와 내용
    };

    struct TestAssetImportParallelInternal
    {
        static constexpr uint32 kPlainSourceCount = 16;

        [[nodiscard]] static bool writeSource( const sw::string& root, sw::string_view relativePath, sw::string_view text )
        {
            const sw::string path = sw::FileUtil::joinPath( sw::FileUtil::joinPath( root, "game/test/fake_raw" ), relativePath );
            return sw::FileUtil::ensureParentDirectoryExists( path ) && sw::FileUtil::writeTextFile( path, text );
        }

        /** @brief 평범한 원본 16 개와 규칙마다 하나씩(순서 의존 · 지원 안 함 · 실패 · 같은 자리 둘 · 옆 폴더 아래)을 쓴다. 의존 원본은 `base` 보다 먼저 정렬되는 이름이다. */
        [[nodiscard]] static bool writeFixture( const sw::string& root )
        {
            bool bWritten = writeSource( root, "base.dat", "base;" ) && writeSource( root, "a_dep.dep", "dep;" ) && writeSource( root, "bad_thing.dat", "bad;" ) &&
                            writeSource( root, "fail_one.dat", "fail;" ) && writeSource( root, "same.alt", "alt;" ) && writeSource( root, "same.dat", "dat;" ) &&
                            writeSource( root, "x.dat", "x;" ) && writeSource( root, "x/y.dat", "y;" );
            for ( uint32 index = 0; index < kPlainSourceCount; ++index )
            {
                sw::StringBuilder<sw::constant::kMaxBuffer32> name;
                name.appendFormat( "m%#%#.dat", index / 10, index % 10 );
                sw::StringBuilder<sw::constant::kMaxBuffer32> text;
                text.appendFormat( "payload%#;", index );
                bWritten = bWritten && writeSource( root, sw::string_view( name.c_str(), name.size() ), sw::string_view( text.c_str(), text.size() ) );
            }
            return bWritten;
        }

        /** @brief `game/test/` 아래 모든 파일을 상대 경로 순으로 `경로=내용` 줄로 합칩니다. */
        [[nodiscard]] static sw::string makeSnapshot( const sw::string& root )
        {
            const sw::string       folder = sw::FileUtil::joinPath( root, "game/test" );
            sw::vector<sw::string> listFile;
            (void)sw::FileUtil::collectFiles( folder, "", listFile, true );
            for ( sw::string& file : listFile )
            {
                file = sw::FileUtil::normalizeSeparators( file );
            }
            std::sort( listFile.begin(), listFile.end() );
            sw::string snapshot;
            for ( const sw::string& file : listFile )
            {
                sw::string text;
                const bool bRead = sw::FileUtil::readTextFile( file, text );
                snapshot += file.substr( folder.size() ) + ( bRead ? "=" : "!unreadable=" ) + text + "\n";
            }
            return snapshot;
        }

        [[nodiscard]] static sw::string joinProblems( const sw::editor::AssetImportSummary& summary )
        {
            sw::string joined;
            for ( const sw::string& problem : summary._listProblem )
            {
                joined += problem + "\n";
            }
            return joined;
        }

        /** @brief 이 스레드에서 지금 에디터 범위를 묻습니다(워커 본문이 하면 안 되는 일을 워커 대신 해 보는 스레드 함수). */
        static void lookupScopeOnThisThread( sw::SubsystemScope** ppOutScope ) { *ppOutScope = sw::editor::findEditorScope(); }

        static void runScenario( const sw::string& root, TestImportProbe* pProbe, TestImportScenarioResult& outResult )
        {
            SW_ASSERT_TRUE( writeFixture( root ) );
            const TestFakeRawImporter importer{ pProbe };
            outResult._import   = sw::editor::AssetImportStampUtil::importAll( root, importer, sw::editor::AssetImportMode::ImportStale, sw::editor::findSubsystem<sw::TaskManager>() );
            outResult._snapshot = makeSnapshot( root );

            // 스탬프에는 맞는 원본 둘의 내용을 바꾸고 검사만 돌린다 — 어긋남 줄도 직렬과 같은 순서여야 한다.
            SW_ASSERT_TRUE( writeSource( root, "m03.dat", "changed03;" ) && writeSource( root, "m11.dat", "changed11;" ) );
            outResult._check = sw::editor::AssetImportStampUtil::importAll( root, importer, sw::editor::AssetImportMode::CheckOnly, sw::editor::findSubsystem<sw::TaskManager>() );
        }
    };
} // namespace

/**
 * @brief [AssetImportParallelTest] 원본 파일을 워커와 나눠 임포트한 결과가 직렬 임포트와 같다 — 출력 · 스탬프 · 카운터 · 문제 목록의 순서
 * @details 규칙마다 하나씩 섞은 24 개 원본이다. 의존 원본(`a_dep.dep`)은 파일 순서로는 `base.dat` 보다 앞이지만 의존 없는 원본이 끝난 뒤에 임포트돼 성공해야 하고,
 *          같은 자리에 쓰는 두 원본(`same.alt` · `same.dat`)과 옆 폴더 아래 원본(`x.dat` · `x/y.dat`)은 서로 밟지 않아야 한다. 병렬 쪽은 워커 4 개이고
 *          임포트 하나가 다른 임포트가 겹칠 때까지 기다려 실제로 둘 이상이 동시에 돌았음을 확인한다.
 */
SW_TEST_CASE( AssetImportParallelTest, ParallelImportMatchesSerialImport )
{
    using Internal = TestAssetImportParallelInternal;

    // 직렬: 작업 시스템이 에디터 범위에 없으면 일괄 임포트는 이 스레드에서 사슬을 파일 순서대로 돈다.
    TestImportProbe          serialProbe;
    TestImportScenarioResult serial;
    Internal::runScenario( test::makeTempDirectory( "import_serial" ), &serialProbe, serial );

    TestImportProbe          parallelProbe;
    TestImportScenarioResult parallel;
    {
        TestScopedImportTaskManager taskManager{ 4 };
        SW_ASSERT_TRUE( taskManager.isReady() );
        parallelProbe._bWaitForOverlap = true;
        Internal::runScenario( test::makeTempDirectory( "import_parallel" ), &parallelProbe, parallel );
    }

    // 직렬 기준: 24 개 중 지원 안 함 1 개와 실패 1 개를 뺀 22 개를 임포트하고, 문제는 그 둘이 파일 순서로 나온다.
    SW_EXPECT_EQUAL( 24u, serial._import._sourceCount );
    SW_EXPECT_EQUAL( 22u, serial._import._importedCount );
    SW_ASSERT_EQUAL( size_t( 2 ), serial._import._listProblem.size() );
    SW_EXPECT_TRUE( serial._import._listProblem[0].find( "bad_thing.dat: unsupported source" ) != sw::string::npos );
    SW_EXPECT_TRUE( serial._import._listProblem[1].find( "fail_one.dat" ) != sw::string::npos );
    SW_EXPECT_TRUE( serial._snapshot.find( "/fake/x/y.out=y;" ) != sw::string::npos );          // 옆 폴더 아래 결과가 `x.dat` 의 폴더 지우기에 살아남았다
    SW_EXPECT_TRUE( serial._snapshot.find( "/fake/same.out=alt;dat;" ) != sw::string::npos );   // 같은 자리 둘이 파일 순서로 이어졌다
    SW_EXPECT_TRUE( serial._snapshot.find( "/fake/a_dep.out=base;dep;" ) != sw::string::npos ); // 의존 원본이 `base` 의 결과를 읽었다

    // 병렬도 같은 것을 같은 순서로 낸다.
    SW_EXPECT_EQUAL( serial._import._sourceCount, parallel._import._sourceCount );
    SW_EXPECT_EQUAL( serial._import._importedCount, parallel._import._importedCount );
    SW_EXPECT_EQUAL( Internal::joinProblems( serial._import ), Internal::joinProblems( parallel._import ) );
    SW_EXPECT_EQUAL( serial._snapshot, parallel._snapshot );
    SW_EXPECT_EQUAL( Internal::joinProblems( serial._check ), Internal::joinProblems( parallel._check ) );
    SW_EXPECT_EQUAL( serial._check._sourceCount, parallel._check._sourceCount );

    // 검사는 바뀐 원본 둘(m03 · m11)을 어긋남으로 잡는다 — 직렬과 같은 순서로.
    SW_EXPECT_TRUE( parallel._check._listProblem.size() >= 2 );
    SW_EXPECT_TRUE( Internal::joinProblems( parallel._check ).find( "m03.dat" ) < Internal::joinProblems( parallel._check ).find( "m11.dat" ) );

    // 병렬 쪽에서 실제로 둘 이상이 동시에 돌았다. 직렬 쪽은 하나씩만 돌았다.
    SW_EXPECT_TRUE( parallelProbe._maxRunning.load() >= 2 );
    SW_EXPECT_EQUAL( 1, serialProbe._maxRunning.load() );
}

/**
 * @brief [AssetImportParallelTest] 워커에서 도는 임포트 본문이 서비스를 값으로 받아 쓰고, 에디터 서비스 범위(`findEditorScope`)를 읽지 않는다
 * @details 작업 시스템은 제출 시점(범위를 등록한 테스트 스레드)에 한 번 찾아 임포터에 넘긴다. 임포트 하나가 다른 임포트가 겹칠 때까지 기다리므로 워커가 실제로 임포터를 불렀음이 보이고,
 *          Debug 에서는 워커가 범위를 읽으면 단언이 울리므로(`findEditorScope`) 단언 수가 0 이어야 한다.
 */
SW_TEST_CASE( AssetImportParallelTest, WorkerBodyUsesServiceHandedByValueWithoutReadingEditorScope )
{
    using Internal = TestAssetImportParallelInternal;

    const sw::string root = test::makeTempDirectory( "import_handoff" );
    for ( uint32 index = 0; index < Internal::kPlainSourceCount; ++index )
    {
        sw::StringBuilder<sw::constant::kMaxBuffer32> name;
        name.appendFormat( "h%#%#.dat", index / 10, index % 10 );
        SW_ASSERT_TRUE( Internal::writeSource( root, sw::string_view( name.c_str(), name.size() ), "payload;" ) );
    }

    TestImportProbe probe;
    probe._bWaitForOverlap = true;
    sw::editor::AssetImportSummary summary;
    test::ScopedAssertCapture      assertCapture;
    {
        TestScopedImportTaskManager taskManager{ 4 };
        SW_ASSERT_TRUE( taskManager.isReady() );
        // 제출 시점(이 스레드)에 서비스를 찾아 값으로 넘긴다. 임포터의 본문은 범위를 찾지 않는다.
        const TestHandoffRawImporter importer{ sw::editor::findSubsystem<sw::TaskManager>(), &probe };
        summary = sw::editor::AssetImportStampUtil::importAll( root, importer, sw::editor::AssetImportMode::ImportStale, sw::editor::findSubsystem<sw::TaskManager>() );
    }

    SW_EXPECT_EQUAL( Internal::kPlainSourceCount, summary._sourceCount );
    SW_EXPECT_EQUAL( Internal::kPlainSourceCount, summary._importedCount );
    SW_EXPECT_TRUE( summary._listProblem.empty() );
    SW_EXPECT_EQUAL( Internal::kPlainSourceCount, probe._serviceUseCount.load() ); // 모든 임포트가 넘겨받은 서비스를 썼다
    SW_EXPECT_TRUE( probe._maxRunning.load() >= 2 );                               // 워커가 실제로 임포터를 불렀다
    if ( test::ScopedAssertCapture::kAssertsAreActive )
        SW_EXPECT_EQUAL( 0u, assertCapture.getCount() );
}

/**
 * @brief [AssetImportParallelTest] 범위를 등록한 스레드가 아닌 스레드가 `findEditorScope` 를 부르면 Debug 에서 단언이 울린다
 * @details 워커가 락 없는 범위 포인터를 읽다가 종료가 범위를 허물면 해제된 메모리를 본다. 그래서 등록한 스레드만 읽게 하고 어기면 그 자리에서 잡는다.
 *          등록한 스레드에서는 울리지 않는다. 범위가 없을 때는 어느 스레드든 읽을 수 있다(범위가 허물어진 뒤 호스트 서비스만 찾는 종료 경로).
 */
SW_TEST_CASE( AssetImportParallelTest, EditorScopeLookupFromAnotherThreadIsRejected )
{
    using Internal = TestAssetImportParallelInternal;

    sw::SubsystemScope*       pSeenByWorker = nullptr;
    test::ScopedAssertCapture assertCapture;
    {
        sw::SubsystemScope            scope;
        sw::editor::ScopedEditorScope activeScope{ scope };

        SW_EXPECT_TRUE( sw::editor::findEditorScope() == &scope ); // 등록한 스레드에서는 통과한다
        SW_EXPECT_EQUAL( 0u, assertCapture.getCount() );

        // 어기면 모든 구성에서 오류 로그를 남기고 멈춘다. 멈춤을 가로채 세는 것은 Debug 에만 있어서, 프로세스 안에서 볼 수 있는 구성에서만 부른다.
        if ( test::ScopedAssertCapture::kAssertsAreActive )
        {
            SW_TEST_DEFENSIVE_SCOPE( "the editor scope is read off the editor thread" );
            std::thread worker( &Internal::lookupScopeOnThisThread, &pSeenByWorker );
            worker.join();
            SW_EXPECT_EQUAL( 1u, assertCapture.getCount() );
        }
    }

    // 범위를 걷은 뒤에는 다른 스레드도 읽을 수 있다 — 단언이 더 늘지 않는다.
    const uint32        afterScopeCount   = assertCapture.getCount();
    sw::SubsystemScope* pSeenWithoutScope = nullptr;
    std::thread         workerWithoutScope( &Internal::lookupScopeOnThisThread, &pSeenWithoutScope );
    workerWithoutScope.join();
    SW_EXPECT_EQUAL( afterScopeCount, assertCapture.getCount() );
}
