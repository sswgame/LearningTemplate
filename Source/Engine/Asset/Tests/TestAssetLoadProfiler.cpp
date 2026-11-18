#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Asset/AssetLoadProfiler.h"
#include "Engine/Foundation/EngineServices.h"

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
    const string previous = pInfo->getValueAsString();

    AssetLoadProfiler profiler;
    SW_REQUIRE_TRUE( variables.setValueFromString( "gv_assetLoadProfile", "1" ) );
    profiler.refreshEnabled();
    SW_EXPECT_TRUE( profiler.isEnabled() );

    SW_REQUIRE_TRUE( variables.setValueFromString( "gv_assetLoadProfile", "0" ) );
    SW_EXPECT_TRUE( profiler.isEnabled() ); // 아직 뜨지 않았다
    profiler.refreshEnabled();
    SW_EXPECT_FALSE( profiler.isEnabled() );

    SW_REQUIRE_TRUE( variables.setValueFromString( "gv_assetLoadProfile", previous ) );
}
