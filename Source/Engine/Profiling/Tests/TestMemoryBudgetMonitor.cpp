// MemoryBudgetMonitor — 예산 표 읽기 · 거절(모르는 태그 · 키 · 중복 · 0 크기, 거절하면 하나도 걸지 않는다), 저장소의 예산 파일이 읽히는지.
#include "pch.h"

#include "Core/Diagnostics/MemoryProfiler.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Profiling/MemoryBudgetMonitor.h"

using namespace sw;

// 프로파일러는 initialize 하지 않는다 — 엔진의 활성 프로파일러 · 할당 추적기 자리를 건드리지 않고 예산 칸만 쓴다.

/**
 * @brief [MemoryBudgetMonitorTest] 예산 표를 읽어 태그마다 바이트로 걸고, 표에 없는 태그의 예산은 지운다
 */
SW_TEST_CASE( MemoryBudgetMonitorTest, AppliesBudgetsFromJSON )
{
    MemoryProfiler profiler;
    profiler.setBudget( MemoryTag::Physics, 123 ); // 표에 없으니 지워진다

    string error;
    SW_REQUIRE_TRUE_MSG( MemoryBudgetMonitor::applyBudgetJSON( R"({ "_listBudget": [ { "_tag": "Texture", "_megabytes": 2 }, { "_tag": "UI", "_megabytes": 0.5 } ] })",
                                                               profiler, error ),
                         error );
    SW_EXPECT_EQUAL( uint64{ 2 * 1024 * 1024 }, profiler.getBudget( MemoryTag::Texture ) );
    SW_EXPECT_EQUAL( uint64{ 512 * 1024 }, profiler.getBudget( MemoryTag::UI ) );
    SW_EXPECT_EQUAL( uint64{ 0 }, profiler.getBudget( MemoryTag::Physics ) );
}

/**
 * @brief [MemoryBudgetMonitorTest] 모르는 태그 · 같은 태그 두 번 · 0 크기는 오류 글로 거절하고, 그때는 예산을 하나도 바꾸지 않는다
 */
SW_TEST_CASE( MemoryBudgetMonitorTest, RejectsMalformedBudgetsWithoutApplyingAny )
{
    MemoryProfiler profiler;
    profiler.setBudget( MemoryTag::Mesh, 777 );

    const utf8* const arrBadJSON[] = {
        R"({ "_listBudget": [ { "_tag": "Textures", "_megabytes": 1 } ] })",
        R"({ "_listBudget": [ { "_tag": "Mesh", "_megabytes": 1 }, { "_tag": "Mesh", "_megabytes": 2 } ] })",
        R"({ "_listBudget": [ { "_tag": "Texture", "_megabytes": 0 } ] })",
    };
    for ( const utf8* pJSON : arrBadJSON )
    {
        string error;
        SW_EXPECT_FALSE_MSG( MemoryBudgetMonitor::applyBudgetJSON( pJSON, profiler, error ), pJSON );
        SW_EXPECT_FALSE_MSG( error.empty(), pJSON );
        SW_EXPECT_EQUAL( uint64{ 777 }, profiler.getBudget( MemoryTag::Mesh ) );
    }
}

/**
 * @brief [MemoryBudgetMonitorTest] 모르는 키는 설정 읽기가 그 키를 알리고 거절한다
 */
SW_TEST_CASE( MemoryBudgetMonitorTest, RejectsUnknownKey )
{
    SW_EXPECT_LOG( "Config.KeyUnknown" );
    MemoryProfiler profiler;
    profiler.setBudget( MemoryTag::Mesh, 777 );
    string error;
    SW_EXPECT_FALSE( MemoryBudgetMonitor::applyBudgetJSON( R"({ "_listBudget": [ { "_tag": "Texture", "_megabytes": 1, "_limit": 2 } ] })", profiler, error ) );
    SW_EXPECT_FALSE( error.empty() );
    SW_EXPECT_EQUAL( uint64{ 777 }, profiler.getBudget( MemoryTag::Mesh ) );
}

/**
 * @brief [MemoryBudgetMonitorTest] 저장소의 예산 파일(`Config/Engine/MemoryBudget.json`)이 읽힌다
 */
SW_TEST_CASE( MemoryBudgetMonitorTest, RepositoryBudgetFileIsValid )
{
    const string path = FileUtil::joinPath( ProcessRoots::get()._project.str(), MemoryBudgetMonitor::kBudgetFile );
    string       text;
    SW_REQUIRE_TRUE_MSG( FileUtil::readTextFile( path, text ), path );

    MemoryProfiler profiler;
    string         error;
    SW_EXPECT_TRUE_MSG( MemoryBudgetMonitor::applyBudgetJSON( text, profiler, error ), error );
    SW_EXPECT_TRUE( profiler.getBudget( MemoryTag::Texture ) > 0 );
}
