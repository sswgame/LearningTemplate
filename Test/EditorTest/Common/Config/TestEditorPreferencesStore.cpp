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
    const EditorExtensionRegistrar<EditorSettingsRegistration> registrar{
        EditorSettingsRegistration{ { "probe", 0 }, "Test/Probe", &EditorPreferencesProbe::StaticType, &EditorSettingsInstance<EditorPreferencesProbe>::getInstance, &EditorSettingsInstance<EditorPreferencesProbe>::getDefault, nullptr, nullptr }
    };
    EditorPreferencesProbe& probe = *static_cast<EditorPreferencesProbe*>( EditorSettingsInstance<EditorPreferencesProbe>::getInstance() );
    probe._speed                  = 7.0f;
    const string filePath         = FileUtil::joinPath( test::makeTempDirectory( "editor_preferences_round_trip" ), EditorPreferencesStore::kFileName );
    SW_ASSERT_TRUE( EditorPreferencesStore::saveAll( filePath ) );
    SW_EXPECT_EQUAL( 1u, EditorPreferencesStore::countSavedKeys( filePath ) );
    probe._speed = 5.0f;
    SW_ASSERT_TRUE( EditorPreferencesStore::loadAll( filePath ) == EditorUserFileResult::Loaded );
    SW_EXPECT_NEAR_EQUAL( 7.0f, probe._speed, 1e-6f );
    EditorPreferencesStore::resetSection( registrar.getRegistration() );
    SW_EXPECT_NEAR_EQUAL( 5.0f, probe._speed, 1e-6f );
}

/**
 * @brief [EditorPreferencesStoreTest] 다시 올라온 모듈의 섹션은 파일 값을 다시 읽고, 그 뒤의 saveAll 이 파일 값을 지우지 않는다
 * @details 핫 리로드를 흉내 낸다: 언로드 훅이 인스턴스 범위를 잊게 하고, 새 이미지의 인스턴스는 기본값으로 돌린다. 한 번 읽은 섹션은 다시 읽어 사용자 값을 덮지 않는다.
 */
SW_TEST_CASE( EditorPreferencesStoreTest, ReloadedSectionReadsTheFileOnce )
{
    const EditorExtensionRegistrar<EditorSettingsRegistration> registrar{
        EditorSettingsRegistration{ { "probe", 0 }, "Test/Probe", &EditorPreferencesProbe::StaticType, &EditorSettingsInstance<EditorPreferencesProbe>::getInstance, &EditorSettingsInstance<EditorPreferencesProbe>::getDefault, nullptr, nullptr }
    };
    EditorPreferencesProbe& probe    = *static_cast<EditorPreferencesProbe*>( EditorSettingsInstance<EditorPreferencesProbe>::getInstance() );
    const string            filePath = FileUtil::joinPath( test::makeTempDirectory( "editor_preferences_reload" ), EditorPreferencesStore::kFileName );
    SW_ASSERT_TRUE( EditorPreferencesStore::loadAll( filePath ) == EditorUserFileResult::Missing ); // 파일이 아직 없다 — 섹션을 '입힘' 으로 적어 둔다
    probe._speed = 7.0f;
    SW_ASSERT_TRUE( EditorPreferencesStore::saveAll( filePath ) );

    // 모듈 리로드: 언로드 훅이 잊고, 새 이미지의 인스턴스는 기본값이다.
    const utf8* pInstanceBytes = static_cast<const utf8*>( static_cast<const void*>( &probe ) );
    EditorPreferencesStore::forgetSectionsWithin( pInstanceBytes, pInstanceBytes + sizeof( probe ) );
    probe = EditorPreferencesProbe{};
    SW_EXPECT_TRUE( EditorPreferencesStore::hasNewSections() );

    // 다른 패널의 저장이 먼저 와도 파일 값을 지우지 않는다.
    SW_ASSERT_TRUE( EditorPreferencesStore::saveAll( filePath ) );
    SW_EXPECT_NEAR_EQUAL( 7.0f, probe._speed, 1e-6f );
    SW_EXPECT_EQUAL( 1u, EditorPreferencesStore::countSavedKeys( filePath ) );
    SW_EXPECT_FALSE( EditorPreferencesStore::hasNewSections() );

    // 이미 읽은 섹션은 다시 읽지 않는다.
    probe._speed = 3.0f;
    SW_EXPECT_EQUAL( 0u, EditorPreferencesStore::loadNewSections( filePath ) );
    SW_EXPECT_NEAR_EQUAL( 3.0f, probe._speed, 1e-6f );
    EditorPreferencesStore::resetSection( registrar.getRegistration() );
}

/**
 * @brief [EditorPreferencesStoreTest] 깨진 환경설정 파일은 `.corrupt` 로 치우고 새로 쓴다 — 조용히 기본값으로 가지 않는다
 */
SW_TEST_CASE( EditorPreferencesStoreTest, BrokenFileIsMovedAsideAndRewritten )
{
    SW_TEST_DEFENSIVE_SCOPE( "a broken preferences file is moved aside" );
    const string filePath = FileUtil::joinPath( test::makeTempDirectory( "editor_preferences_broken" ), EditorPreferencesStore::kFileName );
    SW_ASSERT_TRUE( FileUtil::writeTextFile( filePath, "[ not an object" ) );
    SW_EXPECT_TRUE( EditorPreferencesStore::loadAll( filePath ) == EditorUserFileResult::Recovered );
    SW_EXPECT_TRUE( FileUtil::isRegularFile( filePath + EditorUserFile::kCorruptSuffix ) );
    SW_EXPECT_TRUE( EditorPreferencesStore::loadAll( filePath ) == EditorUserFileResult::Loaded );
}
