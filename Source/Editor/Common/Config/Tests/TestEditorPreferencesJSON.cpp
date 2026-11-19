// 환경설정 JSON — 기본값과 다른 프로퍼티만 담은 글(makeDifferenceJSON)을 applyJSON 이 그대로 입히고, 모르는 키는 알려 주며, 오브젝트가 아닌 글은 거절한다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Config/EditorSettingsRegistry.h"

#include "Engine/Config/EngineConfig.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [EditorPreferencesJSONTest] 차이 글을 새 인스턴스에 입히면 바꾼 값만 따라오고, 글에 없는 키는 그대로 둔다
 */
SW_TEST_CASE( EditorPreferencesJSONTest, DifferenceTextRoundTripsThroughApply )
{
    const TypeInfo&    type = *EngineConfig::StaticType();
    const EngineConfig defaults{};
    EngineConfig       edited{};
    edited._dataInheritanceMaxDepth = defaults._dataInheritanceMaxDepth + 3;

    const expected<string> jsonText = EditorPreferencesStore::makeDifferenceJSON( type, &edited, &defaults );
    SW_REQUIRE_TRUE( jsonText.has_value() );

    EngineConfig   target{};
    vector<string> listUnknownKey;
    SW_REQUIRE_TRUE( EditorPreferencesStore::applyJSON( type, &target, *jsonText, listUnknownKey ) );
    SW_EXPECT_EQUAL( edited._dataInheritanceMaxDepth, target._dataInheritanceMaxDepth );
    SW_EXPECT_EQUAL( defaults._maxFixedStepPerFrame, target._maxFixedStepPerFrame );
    SW_EXPECT_TRUE( listUnknownKey.empty() );
}

/**
 * @brief [EditorPreferencesJSONTest] 모르는 키는 건너뛰고 목록에 담으며, JSON 오브젝트가 아닌 글은 false 다
 */
SW_TEST_CASE( EditorPreferencesJSONTest, UnknownKeysAreListedAndNonObjectsRefused )
{
    const TypeInfo& type = *EngineConfig::StaticType();
    EngineConfig    target{};
    vector<string>  listUnknownKey;
    SW_REQUIRE_TRUE( EditorPreferencesStore::applyJSON( type, &target, R"({"_noSuchSetting": "1"})", listUnknownKey ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listUnknownKey.size() );
    SW_EXPECT_EQUAL( string( "_noSuchSetting" ), listUnknownKey[0] );

    SW_EXPECT_FALSE( EditorPreferencesStore::applyJSON( type, &target, "[1, 2]", listUnknownKey ) );
}
