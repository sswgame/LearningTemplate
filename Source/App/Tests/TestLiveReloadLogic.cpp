// LiveReloadLogicTest — 핫 리로드의 파일 없이 도는 규칙: 모듈 의존 그래프(의존하는 쪽 모으기 · 순서 · 순환), 섀도 복사본 이름 읽기(nogpu).
// ModuleManager 는 정적 라이브러리라 그 안의 테스트 등록은 링크에서 빠진다 - 실행 파일인 App 에 둔다.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "ModuleManager/LiveReload/ModuleDependencyGraph.h"
#include "ModuleManager/LiveReload/ShadowCopyName.h"

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

/** @brief [LiveReloadLogicTest] 정렬: 의존되는 쪽이 먼저이고, 같은 단계는 이름순이라 입력 순서와 상관없다. 그래프에 없는 이름은 빠진다 */
SW_TEST_CASE( LiveReloadLogicTest, SortsByDependency )
{
    const ModuleDependencyGraph graph = makeGraph();
    vector<string>              listFirst;
    SW_REQUIRE_TRUE( graph.sortByDependency( { "D", "C", "B", "A", "Nothing" }, listFirst ) );
    const vector<string> listExpected{ "A", "B", "D", "C" };
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
