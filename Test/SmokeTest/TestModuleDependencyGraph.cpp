/**
 * @file TestModuleDependencyGraph.cpp
 * @brief ModuleDependencyGraph — 연쇄 리로드 대상(의존하는 쪽까지)과 교체 순서(의존되는 쪽 먼저)를 DLL 없이 계산하는지.
 * @details 순서가 틀리면 연쇄 리로드가 의존 모듈의 새 이름을 모르는 채 복사본을 만들어 옛 이미지에 묶이고, 대상이 빠지면 의존하는 모듈이
 *          언로드된 옛 이미지를 부른다. 같은 단계 안은 이름순이라 결과가 입력 순서와 무관해야 한다.
 */
#include "pch.h"

#include "Core/Common/StdHeaders.h"

#include "ModuleHost/ModuleDependencyGraph.h"

#include "TestFramework/TestFramework.h"

namespace
{
    /** @brief 이름 목록을 쉼표로 이어 붙입니다(비교용). */
    sw::string joinNames( const sw::vector<sw::string>& listName )
    {
        sw::string joined;
        for ( const sw::string& name : listName )
        {
            if ( joined.empty() == false )
                joined += ",";
            joined += name;
        }
        return joined;
    }

    /** @brief GameFramework 같은 그래프 밖 의존을 낀, 게임 · 키트 모양의 그래프를 만듭니다. Kit → SWGame · Editor, Tool 은 따로입니다. */
    void populateGameGraph( sw::ModuleDependencyGraph& outGraph )
    {
        outGraph.setDependsOn( "Kit", { "GameFramework" } );
        outGraph.setDependsOn( "SWGame", { "Kit", "GameFramework" } );
        outGraph.setDependsOn( "Editor", { "SWGame" } );
        outGraph.setDependsOn( "Tool", {} );
    }
} // namespace

SW_TEST_CASE( ModuleDependencyGraphTest, ClosureCollectsEveryDependentOnce )
{
    sw::ModuleDependencyGraph graph;
    populateGameGraph( graph );

    sw::vector<sw::string> listClosure;
    graph.collectDependentClosure( "Kit", listClosure );
    std::sort( listClosure.begin(), listClosure.end() );
    SW_EXPECT_STREQ( "Editor,Kit,SWGame", joinNames( listClosure ).c_str() );

    // 이미 든 이름은 다시 넣지 않는다 — 리로드 대상 여럿의 닫힘을 한 목록에 모은다.
    graph.collectDependentClosure( "SWGame", listClosure );
    SW_EXPECT_EQUAL( 3u, static_cast<uint32>( listClosure.size() ) );

    // 그래프 밖 이름(교체하지 않는 의존)은 대상이 아니다.
    sw::vector<sw::string> listExternalClosure;
    graph.collectDependentClosure( "GameFramework", listExternalClosure );
    SW_EXPECT_TRUE( listExternalClosure.empty() );
}

SW_TEST_CASE( ModuleDependencyGraphTest, SortPutsDependenciesFirstRegardlessOfInputOrder )
{
    sw::ModuleDependencyGraph graph;
    populateGameGraph( graph );

    sw::vector<sw::string> listOrdered;
    SW_ASSERT_TRUE( graph.sortByDependency( { "Editor", "Tool", "SWGame", "Kit", "Unknown" }, listOrdered ) );
    // 의존이 없는 단계(Kit · Tool)는 이름순, 그 뒤로 SWGame · Editor. 그래프 밖 이름은 빠진다.
    SW_EXPECT_STREQ( "Kit,Tool,SWGame,Editor", joinNames( listOrdered ).c_str() );

    SW_ASSERT_TRUE( graph.sortByDependency( { "Kit", "Editor", "SWGame", "Tool" }, listOrdered ) );
    SW_EXPECT_STREQ( "Kit,Tool,SWGame,Editor", joinNames( listOrdered ).c_str() );

    // 부분 그래프 밖의 의존(SWGame 의 Kit)은 진입 차수로 세지 않는다.
    SW_ASSERT_TRUE( graph.sortByDependency( { "Editor", "SWGame" }, listOrdered ) );
    SW_EXPECT_STREQ( "SWGame,Editor", joinNames( listOrdered ).c_str() );
}

SW_TEST_CASE( ModuleDependencyGraphTest, CycleIsRefused )
{
    SW_TEST_DEFENSIVE_SCOPE( "A dependency cycle is reported and refused" );
    sw::ModuleDependencyGraph graph;
    graph.setDependsOn( "First", { "Second" } );
    graph.setDependsOn( "Second", { "First" } );
    graph.setDependsOn( "Free", {} );

    sw::vector<sw::string> listOrdered{ "stale" };
    SW_EXPECT_FALSE( graph.sortByDependency( { "First", "Second", "Free" }, listOrdered ) );
    SW_EXPECT_TRUE( listOrdered.empty() );
}

SW_TEST_CASE( ModuleDependencyGraphTest, MembershipFollowsEnsureAndRemove )
{
    sw::ModuleDependencyGraph graph;
    // 콜백만 먼저 등록한 모듈도 이름은 그래프에 있다(`LiveReloadManager::getOrCreateContext`). 의존은 비어 있다.
    graph.ensureModule( "SWGame" );
    SW_EXPECT_TRUE( graph.hasModule( "SWGame" ) );
    SW_EXPECT_TRUE( graph.getDependsOn( "SWGame" ).empty() );

    // 등록이 의존을 채운 뒤 다시 ensure 해도 의존은 그대로다.
    graph.setDependsOn( "SWGame", { "Kit" } );
    graph.ensureModule( "SWGame" );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( graph.getDependsOn( "SWGame" ).size() ) );

    graph.removeModule( "SWGame" );
    SW_EXPECT_FALSE( graph.hasModule( "SWGame" ) );
    SW_EXPECT_TRUE( graph.getDependsOn( "SWGame" ).empty() );

    graph.setDependsOn( "Kit", {} );
    graph.clear();
    sw::vector<sw::string> listName;
    graph.getModuleNames( listName );
    SW_EXPECT_TRUE( listName.empty() );
}
