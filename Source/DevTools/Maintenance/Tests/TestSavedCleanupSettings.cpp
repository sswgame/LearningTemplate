// 보존 규칙 파일(`Config/Dev/SavedCleanup.json`)을 리플렉션 스키마로 읽는다 — 손으로 읽던 키 이름 · 필수 칸이 그대로인지.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "DevTools/Maintenance/SavedCleanupSubsystem.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Serialization/Format/JSONSerializer.h"

using namespace sw;

namespace sw
{
    /** @brief 규칙 파일 스키마는 서브시스템의 private 중첩 형식이다 — 테스트만 이 창구로 이름을 본다. */
    struct SavedCleanupTestAccess
    {
        using Settings = SavedCleanupSubsystem::RetentionSettings;
    };
} // namespace sw

/** @brief [SavedCleanupSettingsTest] 저장소의 규칙 파일이 읽히고, 쓴 글을 다시 읽어 쓰면 같은 바이트다 */
SW_TEST_CASE( SavedCleanupSettingsTest, ProjectFileReadsAndRoundTrips )
{
    using Settings                  = SavedCleanupTestAccess::Settings;
    const expected<Settings> loaded = ConfigManager::readConfig( Settings::kConfigFile );
    SW_REQUIRE_TRUE( loaded.has_value() );
    const Settings& settings = *loaded;
    SW_EXPECT_TRUE( settings._minAgeSeconds > 0 );
    SW_EXPECT_FALSE( settings._trashFolder.empty() );
    SW_REQUIRE_FALSE( settings._listRule.empty() );
    SW_EXPECT_FALSE( settings._listRule[0]._folder.empty() );

    const expected<string> written = JSONSerializer::serialize( &settings, *Settings::StaticType() );
    SW_REQUIRE_TRUE( written.has_value() );
    Settings again;
    SW_REQUIRE_TRUE( JSONSerializer::deserialize( &again, *Settings::StaticType(), *written ) );
    const expected<string> rewritten = JSONSerializer::serialize( &again, *Settings::StaticType() );
    SW_REQUIRE_TRUE( rewritten.has_value() );
    SW_EXPECT_EQUAL( *written, *rewritten );
}

/** @brief [SavedCleanupSettingsTest] 필수 칸이 빠지면 기본값으로 읽지 않고 실패한다(기본값으로 지우지 않는다) */
SW_TEST_CASE( SavedCleanupSettingsTest, MissingRequiredFieldFails )
{
    using Settings = SavedCleanupTestAccess::Settings;
    SW_EXPECT_LOG( "Config.KeyMissing" );
    SW_EXPECT_FALSE( ConfigManager::parseConfig<Settings>( R"({ "_minAgeSeconds": 1, "_trashFolder": ".Trash", "_listRule": [ { "_folder": "Logs", "_keepCount": 3, "_bPerChild": false } ] })", "test" ).has_value() );
}
