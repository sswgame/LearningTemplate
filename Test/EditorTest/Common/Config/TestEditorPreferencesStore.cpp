#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Editor/Common/Config/EditorSettingsRegistry.h"

#include "EditorTest/EditorPreviewProbe.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;
using sw::editortest::EditorPreferencesProbe;

/**
 * @brief [EditorPreferencesStoreTest] 저장 글에는 기본값과 다른 프로퍼티만 들어간다
 * @details 사용자 파일에 기본값을 다시 적으면 나중에 코드의 기본값을 바꿔도 그 사용자는 옛 값을 계속 쓴다(언리얼 · 유니티도 바뀐 것만 쓴다).
 */
SW_TEST_CASE( EditorPreferencesStoreTest, DifferenceHoldsOnlyChangedProperties )
{
    EditorPreferencesProbe       current{};
    const EditorPreferencesProbe defaults{};
    current._speed    = 9.0f;
    const string json = EditorPreferencesStore::makeDifferenceJSON( *EditorPreferencesProbe::StaticType(), &current, &defaults );
    SW_EXPECT_TRUE( json.find( "_speed" ) != string::npos );
    SW_EXPECT_TRUE( json.find( "_path" ) == string::npos );
    SW_EXPECT_TRUE( json.find( "_bFlag" ) == string::npos );
}

/**
 * @brief [EditorPreferencesStoreTest] 입히기는 없는 키를 그대로 두고 모르는 키를 알린다
 */
SW_TEST_CASE( EditorPreferencesStoreTest, ApplyKeepsMissingKeysAndReportsUnknown )
{
    EditorPreferencesProbe probe{};
    probe._path = "keep";
    vector<string> listUnknownKey;
    SW_ASSERT_TRUE( EditorPreferencesStore::applyJSON( *EditorPreferencesProbe::StaticType(), &probe, R"({ "_speed": "2.5", "_gone": "1" })", listUnknownKey ) );
    SW_EXPECT_NEAR_EQUAL( 2.5f, probe._speed, 1e-6f );
    SW_EXPECT_TRUE( probe._path == "keep" );
    SW_ASSERT_EQUAL( size_t{ 1 }, listUnknownKey.size() );
    SW_EXPECT_TRUE( listUnknownKey[0] == "_gone" );
}

/**
 * @brief [EditorPreferencesStoreTest] 등록된 섹션은 파일로 저장했다가 다시 읽으면 값이 돌아온다
 */
SW_TEST_CASE( EditorPreferencesStoreTest, RoundTripThroughTheFile )
{
    const EditorRegistrar<EditorSettingsRegistration> registrar{
        EditorSettingsRegistration{ { "probe", 0 }, "Test/Probe", &EditorPreferencesProbe::StaticType, &EditorSettingsInstance<EditorPreferencesProbe>::getInstance, &EditorSettingsInstance<EditorPreferencesProbe>::getDefault, nullptr }
    };
    EditorPreferencesProbe& probe = *static_cast<EditorPreferencesProbe*>( EditorSettingsInstance<EditorPreferencesProbe>::getInstance() );
    probe._speed                  = 7.0f;
    const string filePath         = FileUtil::joinPath( test::makeTempDirectory( "editor_preferences_round_trip" ), EditorPreferencesStore::kFileName );
    SW_ASSERT_TRUE( EditorPreferencesStore::saveAll( filePath ) );
    SW_EXPECT_EQUAL( 1u, EditorPreferencesStore::countSavedKeys( filePath ) );
    probe._speed = 5.0f;
    SW_ASSERT_TRUE( EditorPreferencesStore::loadAll( filePath ) );
    SW_EXPECT_NEAR_EQUAL( 7.0f, probe._speed, 1e-6f );
    EditorPreferencesStore::resetSection( registrar.getRegistration() );
    SW_EXPECT_NEAR_EQUAL( 5.0f, probe._speed, 1e-6f );
}
