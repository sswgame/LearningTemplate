// LiveReloadLogicTest — 핫 리로드의 파일 없이 도는 규칙: 모듈 의존 그래프(의존하는 쪽 모으기 · 순서 · 순환), 섀도 복사본 이름 읽기(nogpu).
// App 이 ModuleManager 를 통째로 링크해 이 등록이 남는다(`Source/App/CMakeLists.txt` 의 `sw_link_whole_archive`) — Server 는 이 스위트를 돌리지 않는다.
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Module/ModuleLibraryUtil.h"
#include "Core/Testing/TestFramework.h"

#include "DevTools/ModuleReload/LiveReload/DeferredLibraryUnloadQueue.h"
#include "DevTools/ModuleReload/LiveReload/ModuleDependencyGraph.h"
#include "DevTools/ModuleReload/LiveReload/ModuleShadowCopy.h"
#include "DevTools/ModuleReload/LiveReload/ShadowCopyName.h"

using namespace sw;

namespace
{
    /** @brief A <- B <- C(C 가 B 에, B 가 A 에 의존), D 는 A 에 의존, E 는 따로입니다. */
    ModuleDependencyGraph makeGraph()
    {
        ModuleDependencyGraph graph;
        graph.setDependsOn( "A", { "Engine" } ); // 그래프 밖 의존은 세지 않는다
        graph.setDependsOn( "B", { "A" } );
        graph.setDependsOn( "C", { "B" } );
        graph.setDependsOn( "D", { "A" } );
        graph.ensureModule( "E" );
        return graph;
    }
} // namespace

/** @brief [LiveReloadLogicTest] 그래프 기본: 있는 모듈 · 의존 목록 · 빼기. ensureModule 은 있던 의존을 지우지 않는다 */
SW_TEST_CASE( LiveReloadLogicTest, GraphBasics )
{
    ModuleDependencyGraph graph = makeGraph();
    SW_EXPECT_TRUE( graph.hasModule( "B" ) );
    SW_EXPECT_FALSE( graph.hasModule( "Engine" ) );
    SW_EXPECT_TRUE( graph.getDependsOn( "Nothing" ).empty() );

    graph.ensureModule( "B" );
    SW_EXPECT_EQUAL( size_t( 1 ), graph.getDependsOn( "B" ).size() );

    vector<string> listName;
    graph.getModuleNames( listName );
    SW_EXPECT_EQUAL( size_t( 5 ), listName.size() );

    graph.removeModule( "E" );
    SW_EXPECT_FALSE( graph.hasModule( "E" ) );
    graph.clear();
    SW_EXPECT_FALSE( graph.hasModule( "A" ) );
}

/** @brief [LiveReloadLogicTest] 의존하는 쪽 모으기: 간접까지 모으고 자기를 넣는다. 이미 든 이름과 그래프에 없는 뿌리는 더하지 않는다 */
SW_TEST_CASE( LiveReloadLogicTest, DependentClosure )
{
    const ModuleDependencyGraph graph = makeGraph();
    vector<string>              listModule;
    graph.collectDependentClosure( "A", listModule );
    SW_EXPECT_EQUAL( size_t( 4 ), listModule.size() ); // A · B · C · D
    SW_EXPECT_EQUAL( string( "A" ), listModule[0] );

    vector<string> listLeaf;
    graph.collectDependentClosure( "C", listLeaf );
    SW_REQUIRE_EQUAL( size_t( 1 ), listLeaf.size() );

    graph.collectDependentClosure( "C", listLeaf ); // 다시 넣지 않는다
    SW_EXPECT_EQUAL( size_t( 1 ), listLeaf.size() );

    vector<string> listNone;
    graph.collectDependentClosure( "Nothing", listNone );
    SW_EXPECT_TRUE( listNone.empty() );

    ModuleDependencyGraph ring; // 순환이 있어도 끝난다
    ring.setDependsOn( "X", { "Y" } );
    ring.setDependsOn( "Y", { "X" } );
    vector<string> listRing;
    ring.collectDependentClosure( "X", listRing );
    SW_EXPECT_EQUAL( size_t( 2 ), listRing.size() );
}

/** @brief [LiveReloadLogicTest] 정렬: 의존되는 쪽이 먼저이고, 준비된 것은 이름순이라(모듈 적재 순서와 같은 규칙) 입력 순서와 상관없다. 그래프에 없는 이름은 빠진다 */
SW_TEST_CASE( LiveReloadLogicTest, SortsByDependency )
{
    const ModuleDependencyGraph graph = makeGraph();
    vector<string>              listFirst;
    SW_REQUIRE_TRUE( graph.sortByDependency( { "D", "C", "B", "A", "Nothing" }, listFirst ) );
    const vector<string> listExpected{ "A", "B", "C", "D" }; // B 를 꺼내면 C 와 D 가 함께 준비되고 이름이 앞인 C 가 먼저다
    SW_EXPECT_TRUE( listFirst == listExpected );

    vector<string> listSecond;
    SW_REQUIRE_TRUE( graph.sortByDependency( { "A", "B", "C", "D" }, listSecond ) );
    SW_EXPECT_TRUE( listSecond == listExpected );

    vector<string> listPartial;
    SW_REQUIRE_TRUE( graph.sortByDependency( { "C", "A" }, listPartial ) ); // 고른 것 사이의 의존만 본다
    SW_EXPECT_EQUAL( size_t( 2 ), listPartial.size() );

    vector<string> listEmpty{ "stale" };
    SW_EXPECT_TRUE( graph.sortByDependency( { "Nothing" }, listEmpty ) );
    SW_EXPECT_TRUE( listEmpty.empty() );
}

/** @brief [LiveReloadLogicTest] 순환이면 로그를 남기고 false 이며 결과는 비어 있다 */
SW_TEST_CASE( LiveReloadLogicTest, CycleFailsSort )
{
    SW_EXPECT_LOG( "LiveReloadManager.DependencyCycleModuleGraph" );
    ModuleDependencyGraph graph;
    graph.setDependsOn( "X", { "Y" } );
    graph.setDependsOn( "Y", { "X" } );
    graph.ensureModule( "Z" );
    vector<string> listOrdered{ "stale" };
    SW_EXPECT_FALSE( graph.sortByDependency( { "X", "Y", "Z" }, listOrdered ) );
    SW_EXPECT_TRUE( listOrdered.empty() );
}

/** @brief [LiveReloadLogicTest] 섀도 복사본 이름: 만든 이름을 경로째로 읽고 프로세스를 꺼낸다. 옛 형식은 0 이고, 형식이 틀리면 복사본이 아니다 */
SW_TEST_CASE( LiveReloadLogicTest, ShadowCopyNames )
{
    int32        processID = -1;
    const string name      = ShadowCopyName::make( "SWGame", 4120, 3, 13435508261ull );
    SW_EXPECT_EQUAL( string( "SWGame_temp_p4120_3_13435508261" ), name );
    SW_EXPECT_TRUE( ShadowCopyName::parse( name, processID ) );
    SW_EXPECT_EQUAL( 4120, processID );
    SW_EXPECT_TRUE( ShadowCopyName::parse( "Bin/Modules/" + name + ".dll", processID ) );
    SW_EXPECT_EQUAL( 4120, processID );

    SW_EXPECT_TRUE( ShadowCopyName::parse( "SWGame_temp_3_13435508261", processID ) ); // 프로세스 번호를 넣기 전 형식
    SW_EXPECT_EQUAL( 0, processID );

    for ( const utf8* pNot : { "SWGame.dll", "SWGame_temp_", "SWGame_temp_x1", "SWGame_temp_p", "SWGame_temp_p12", "SWGame_temp_p_3", "SWGame_temp_pAB_3", "SWGame_temp_p0_3" } )
    {
        SW_EXPECT_FALSE( ShadowCopyName::parse( pNot, processID ) );
    }
}

namespace
{
    /** @brief 언로드 큐 테스트의 재료입니다. */
    struct DeferredUnloadTestUtil
    {
        /** @brief 언제나 있는 시스템 라이브러리를 한 번 더 엽니다. 참조 수만 오르므로 큐가 내려도 프로세스에는 남습니다. */
        static void* openSystemLibrary()
        {
#if defined( SW_PLATFORM_WINDOWS )
            return ModuleLibraryUtil::loadSystemLibrary( "kernel32.dll" );
#elif defined( SW_PLATFORM_LINUX )
            return ModuleLibraryUtil::loadSystemLibrary( "libc.so.6" );
#else
    #error "Unsupported platform"
#endif
        }

        /** @brief 내려갈 때 지워지는지 볼 섀도 복사본 자리의 파일을 만듭니다. */
        static string makeShadowFile( string_view name )
        {
            const string path = test::makeScratchPath( name );
            SW_EXPECT_TRUE( FileUtil::writeTextFile( path, "shadow" ) );
            return path;
        }
    };
} // namespace

/** @brief [LiveReloadLogicTest] 언로드 큐: 상한을 넘으면 가장 오래된 배치가 통째로 내려가고 복사본이 지워진다. 비어 있어도 안전하고 unloadAll 은 전부 내린다 */
SW_TEST_CASE( LiveReloadLogicTest, DeferredUnloadKeepsRecentBatches )
{
    DeferredLibraryUnloadQueue queue( 2 );
    queue.unloadOldestBatch(); // 비어 있으면 아무 일도 없다
    queue.deferUnload( "Null", nullptr, "ignored" );
    SW_EXPECT_EQUAL( 0u, queue.getLibraryCount() );

    const string a1 = DeferredUnloadTestUtil::makeShadowFile( "a1" );
    const string a2 = DeferredUnloadTestUtil::makeShadowFile( "a2" );
    const string b  = DeferredUnloadTestUtil::makeShadowFile( "b" );
    const string c  = DeferredUnloadTestUtil::makeShadowFile( "c" );

    queue.beginBatch();
    queue.deferUnload( "A1", DeferredUnloadTestUtil::openSystemLibrary(), a1 );
    queue.deferUnload( "A2", DeferredUnloadTestUtil::openSystemLibrary(), a2 );
    queue.beginBatch();
    queue.deferUnload( "B", DeferredUnloadTestUtil::openSystemLibrary(), b );
    SW_EXPECT_EQUAL( 3u, queue.getLibraryCount() );
    SW_EXPECT_TRUE( FileUtil::exists( a1 ) );

    queue.beginBatch();
    queue.deferUnload( "C", DeferredUnloadTestUtil::openSystemLibrary(), c ); // 배치 A 가 최근 둘 밖으로 나간다
    SW_EXPECT_EQUAL( 2u, queue.getLibraryCount() );
    SW_EXPECT_FALSE( FileUtil::exists( a1 ) );
    SW_EXPECT_FALSE( FileUtil::exists( a2 ) );
    SW_EXPECT_TRUE( FileUtil::exists( b ) );

    queue.unloadOldestBatch();
    SW_EXPECT_EQUAL( 1u, queue.getLibraryCount() );
    SW_EXPECT_FALSE( FileUtil::exists( b ) );
    SW_EXPECT_TRUE( FileUtil::exists( c ) );

    queue.unloadAll();
    SW_EXPECT_EQUAL( 0u, queue.getLibraryCount() );
    SW_EXPECT_FALSE( FileUtil::exists( c ) );
}

/** @brief [LiveReloadLogicTest] 언로드 큐: 상한이 0 이면 넣자마자 내려간다(비운 목록의 끝을 읽지 않는다) */
SW_TEST_CASE( LiveReloadLogicTest, DeferredUnloadWithoutKeepingAnyBatch )
{
    DeferredLibraryUnloadQueue queue( 0 );
    const string               path = DeferredUnloadTestUtil::makeShadowFile( "now" );
    queue.deferUnload( "Now", DeferredUnloadTestUtil::openSystemLibrary(), path );
    SW_EXPECT_EQUAL( 0u, queue.getLibraryCount() );
    SW_EXPECT_FALSE( FileUtil::exists( path ) );
}

/** @brief [LiveReloadLogicTest] 섀도 복사본: 엔진 ABI 스탬프가 없는 파일은 복사본을 만들지 않고 이름 상태도 그대로다. 복사본 지우기는 빈 경로에 아무 일도 하지 않는다 */
SW_TEST_CASE( LiveReloadLogicTest, ShadowCopyRefusesUnstampedFile )
{
    SW_EXPECT_LOG( "LiveReloadManager.ModuleBuiltAgainstDifferent" );
    const string original = test::makeScratchPath( "NotAModule.bin" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( original, "no stamp in here" ) );

    ShadowCopyParams params{};
    params._moduleName         = "NotAModule";
    params._originalModulePath = original;
    ShadowLibraryNameState name{};
    name._current = "current";
    name._loaded  = "loaded";
    ShadowCopySerials serials{};
    string            shadowPath{ "untouched" };
    SW_EXPECT_FALSE( ModuleShadowCopy::writeShadowCopy( params, serials, name, shadowPath ) );
    SW_EXPECT_EQUAL( string( "untouched" ), shadowPath );
    SW_EXPECT_EQUAL( string( "current" ), name._current );

    vector<uint8> bytes;
    SW_REQUIRE_TRUE( ModuleShadowCopy::readModuleFile( original, bytes ) );
    SW_EXPECT_EQUAL( size_t( 16 ), bytes.size() );
    SW_EXPECT_FALSE( ModuleShadowCopy::shouldRejectDelayImports( "NotAModule", original, bytes ) ); // PE 가 아니면 지연 import 표도 없다

    ModuleShadowCopy::removeArtifacts( {} );
    SW_EXPECT_TRUE( FileUtil::exists( original ) );
    ModuleShadowCopy::removeArtifacts( original );
    SW_EXPECT_FALSE( FileUtil::exists( original ) );
}
