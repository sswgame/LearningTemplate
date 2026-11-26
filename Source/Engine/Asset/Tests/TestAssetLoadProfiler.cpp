#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/TestFramework/ScopedGlobalVariableValue.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Asset/AssetLoadProfiler.h"
#include "Engine/Foundation/EngineSubsystems.h"

// 에셋 로드 기록의 켜짐 — 로드 워커가 읽는 것은 게임 스레드가 뜬 사본이다(nogpu).

using namespace sw;

/**
 * @brief [AssetLoadProfilerTest] 콘솔이 `gv_assetLoadProfile` 을 바꿔도 다시 뜨기(`refreshEnabled`) 전까지 사본은 그대로고, 뜬 뒤에 새 값이다
 * @details 변이: `isEnabled` 가 변수를 직접 읽으면(다른 스레드의 잠금 없는 읽기) 첫 기대가 진다.
 */
SW_TEST_CASE( AssetLoadProfilerTest, OtherThreadsReadTheRefreshedCopy )
{
    GlobalVariableManager& variables = engine::getSubsystem<GlobalVariableManager>();
    GlobalVariableInfo*    pInfo     = variables.findVariable( "gv_assetLoadProfile" );
    SW_REQUIRE_NOT_NULL( pInfo );
    test::ScopedGlobalVariableValue profileValue{ *pInfo, "1" }; // 케이스가 어떻게 끝나도 앞 값으로 되돌린다
    SW_REQUIRE_TRUE( profileValue.isApplied() );

    AssetLoadProfiler profiler;
    profiler.refreshEnabled();
    SW_EXPECT_TRUE( profiler.isEnabled() );

    SW_REQUIRE_TRUE( profileValue.set( "0" ) );
    SW_EXPECT_TRUE( profiler.isEnabled() ); // 아직 뜨지 않았다
    profiler.refreshEnabled();
    SW_EXPECT_FALSE( profiler.isEnabled() );
}

/** @brief [AssetLoadProfilerTest] 씬 갱신 안의 동기 로드만 세고 비우면 처음으로 돌아간다 */
SW_TEST_CASE( AssetLoadProfilerTest, SyncLoadsCountOnlyInsideTheSceneUpdate )
{
    AssetLoadProfiler profiler;
    profiler.recordSyncLoad( "Mesh", "a.mesh", AssetLoadProfiler::kSyncLoadLogNanoseconds );
    SW_EXPECT_EQUAL( 0u, profiler.getSceneUpdateSyncLoads()._count );

    profiler.setSceneUpdateActive( true );
    profiler.recordSyncLoad( "Mesh", "a.mesh", AssetLoadProfiler::kSyncLoadLogNanoseconds );
    profiler.recordSyncLoad( "Mesh", "a.mesh", AssetLoadProfiler::kSyncLoadLogNanoseconds );
    profiler.recordSyncLoad( "Mesh", "b.mesh", AssetLoadProfiler::kSyncLoadLogNanoseconds - 1 ); // 문턱 아래라 알리지 않는다
    profiler.setSceneUpdateActive( false );
    profiler.recordSyncLoad( "Mesh", "c.mesh", AssetLoadProfiler::kSyncLoadLogNanoseconds );
    const AssetLoadProfiler::SyncLoadTotal total = profiler.getSceneUpdateSyncLoads();
    SW_EXPECT_EQUAL( 3u, total._count );
    SW_EXPECT_EQUAL( AssetLoadProfiler::kSyncLoadLogNanoseconds * 3 - 1, total._nanoseconds );
    profiler.reset();
    SW_EXPECT_EQUAL( 0u, profiler.getSceneUpdateSyncLoads()._count );
}
