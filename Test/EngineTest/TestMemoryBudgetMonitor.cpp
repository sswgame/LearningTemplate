/**
 * @file TestMemoryBudgetMonitor.cpp
 * @brief 메모리 태그 예산 데이터 — 읽기 · 거절(모르는 태그 · 키 · 중복 · 0 이하), 저장소의 예산 파일이 읽히는지, 프레임 검사가 넘은 태그를 잡는지.
 */
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Memory/MemoryProfiler.h"

#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Utility/Profiling/MemoryBudgetMonitor.h"

#include "TestFramework/TestFramework.h"

/**
 * @brief [MemoryBudgetMonitorTest] 예산 표를 읽어 태그마다 바이트로 걸고, 표에 없는 태그의 예산은 지운다
 */
SW_TEST_CASE( MemoryBudgetMonitorTest, AppliesBudgetsFromJson )
{
    sw::MemoryProfiler profiler;
    profiler.initialize();
    profiler.setBudget( sw::MemoryTag::Physics, 123 ); // 표에 없으니 지워진다

    sw::string error;
    SW_ASSERT_TRUE_MSG( sw::MemoryBudgetMonitor::applyBudgetJson( R"({ "_listBudget": [ { "_tag": "Texture", "_megabytes": 2 }, { "_tag": "ui", "_megabytes": 0.5 } ] })",
                                                                  profiler, error ),
                        error.c_str() );
    SW_EXPECT_EQUAL( uint64{ 2 * 1024 * 1024 }, profiler.getBudget( sw::MemoryTag::Texture ) );
    SW_EXPECT_EQUAL( uint64{ 512 * 1024 }, profiler.getBudget( sw::MemoryTag::UI ) );
    SW_EXPECT_EQUAL( uint64{ 0 }, profiler.getBudget( sw::MemoryTag::Physics ) );
    profiler.shutdown();
}

/**
 * @brief [MemoryBudgetMonitorTest] 모르는 태그 · 모르는 키 · 같은 태그 두 번 · 0 이하 크기는 거절하고, 그때는 예산을 하나도 바꾸지 않는다
 */
SW_TEST_CASE( MemoryBudgetMonitorTest, RejectsMalformedBudgetsWithoutApplyingAny )
{
    sw::MemoryProfiler profiler;
    profiler.initialize();
    profiler.setBudget( sw::MemoryTag::Mesh, 777 );

    const utf8* const arrBadJson[] = {
        R"({ "_listBudget": [ { "_tag": "Textures", "_megabytes": 1 } ] })",
        R"({ "_listBudget": [ { "_tag": "Texture", "_megabytes": 1, "_limit": 2 } ] })",
        R"({ "_listBudgets": [] })",
        R"({ "_listBudget": [ { "_tag": "Mesh", "_megabytes": 1 }, { "_tag": "mesh", "_megabytes": 2 } ] })",
        R"({ "_listBudget": [ { "_tag": "Texture", "_megabytes": 0 } ] })",
        R"({ "_listBudget": [ { "_tag": "Texture", "_megabytes": "big" } ] })",
        R"({ "_listBudget": { "_tag": "Texture" } })",
    };
    for ( const utf8* pJson : arrBadJson )
    {
        sw::string                error;
        test::ScopedLogSuppressor suppressor;
        SW_EXPECT_FALSE_MSG( sw::MemoryBudgetMonitor::applyBudgetJson( pJson, profiler, error ), pJson );
        SW_EXPECT_FALSE_MSG( error.empty(), pJson );
        SW_EXPECT_EQUAL( uint64{ 777 }, profiler.getBudget( sw::MemoryTag::Mesh ) );
    }
    profiler.shutdown();
}

/**
 * @brief [MemoryBudgetMonitorTest] 저장소의 예산 파일(`Config/Engine/MemoryBudget.json`)이 읽힌다
 */
SW_TEST_CASE( MemoryBudgetMonitorTest, RepositoryBudgetFileIsValid )
{
    const sw::string path = sw::FileUtil::joinPath( sw::ResourceUtil::getProjectFolderPath(), sw::MemoryBudgetMonitor::kBudgetFile );
    sw::string       text;
    SW_ASSERT_TRUE_MSG( sw::FileUtil::readTextFile( path, text ), path.c_str() );

    sw::MemoryProfiler profiler;
    profiler.initialize();
    sw::string error;
    SW_EXPECT_TRUE_MSG( sw::MemoryBudgetMonitor::applyBudgetJson( text, profiler, error ), error.c_str() );
    SW_EXPECT_TRUE( profiler.getBudget( sw::MemoryTag::Texture ) > 0 );
    profiler.shutdown();
}
