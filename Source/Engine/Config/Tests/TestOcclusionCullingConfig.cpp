#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/OcclusionCullingConfig.h"
#include "Engine/Reflection/TypeInfo.h"

using namespace sw;

// HZB 가림 판정 설정 — 커밋한 파일이 읽히고 검사를 통과하며, 문턱이 범위 밖이면 이유를 돌려준다(렌더러는 그 이유로 멈춘다).

/** @brief [OcclusionCullingConfigTest] 커밋한 Config/Engine/OcclusionCulling.json 은 읽히고 맞다. */
SW_TEST_CASE( OcclusionCullingConfigTest, CommittedFileLoadsAndValidates )
{
    OcclusionCullingConfig config;
    SW_REQUIRE_TRUE( ConfigManager::readConfigFileInto( OcclusionCullingConfig::kConfigFile, config ) == ConfigReadResult::Loaded );
    SW_EXPECT_TRUE( config.validate().empty() );
}

/** @brief [OcclusionCullingConfigTest] 발자국은 1 ~ 4, 깊이 치우침은 0 이상이다 — 밖이면 이유가 있다. */
SW_TEST_CASE( OcclusionCullingConfigTest, ValidateRejectsOutOfRange )
{
    OcclusionCullingConfig config;
    config._footprintTexels = 2;
    config._depthBias       = 0.0f;
    SW_EXPECT_TRUE( config.validate().empty() );
    config._footprintTexels = 0;
    SW_EXPECT_FALSE( config.validate().empty() );
    config._footprintTexels = OcclusionCullingConfig::kMaxFootprintTexels + 1u;
    SW_EXPECT_FALSE( config.validate().empty() );
    config._footprintTexels = 2;
    config._depthBias       = -0.001f;
    SW_EXPECT_FALSE( config.validate().empty() );
}
