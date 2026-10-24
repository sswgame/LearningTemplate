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
    const EditorSettingsRegistrar<EditorPreferencesProbe> registrar{ "probe", "Test/Probe", 0, nullptr };
    EditorPreferencesProbe&                               probe = editPreferences<EditorPreferencesProbe>();
    probe._speed                                                = 7.0f;
    const string filePath                                       = FileUtil::joinPath( test::makeTempDirectory( "editor_preferences_round_trip" ), EditorPreferencesStore::kFileName );
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
    unique_ptr<EditorSettingsRegistrar<EditorPreferencesProbe>> pRegistrar = make_unique<EditorSettingsRegistrar<EditorPreferencesProbe>>( "probe", "Test/Probe", 0, nullptr );
    const string                                                filePath   = FileUtil::joinPath( test::makeTempDirectory( "editor_preferences_reload" ), EditorPreferencesStore::kFileName );
    SW_ASSERT_TRUE( EditorPreferencesStore::loadAll( filePath ) == EditorUserFileResult::Missing ); // 파일이 아직 없다 — 섹션을 '입힘' 으로 적어 둔다
    editPreferences<EditorPreferencesProbe>()._speed = 7.0f;
    SW_ASSERT_TRUE( EditorPreferencesStore::saveAll( filePath ) );

    // 모듈 리로드: 소유자(등록자)가 사라지면 값도 '입힘' 표시도 같이 사라지고, 새 이미지의 소유자는 기본값으로 새로 생긴다.
    pRegistrar.reset(); // 같은 id 의 두 번째 등록은 거절되므로 옛 이미지가 먼저 사라진다
    pRegistrar                    = make_unique<EditorSettingsRegistrar<EditorPreferencesProbe>>( "probe", "Test/Probe", 0, nullptr );
    EditorPreferencesProbe& probe = editPreferences<EditorPreferencesProbe>();
    SW_EXPECT_NEAR_EQUAL( EditorPreferencesProbe{}._speed, probe._speed, 1e-6f );
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
    EditorPreferencesStore::resetSection( pRegistrar->getRegistration() );
}

/**
 * @brief [EditorPreferencesStoreTest] 등록되지 않은 섹션은 기본값으로 대신하지 않고 등록을 찾지 못한다 — 등록자가 사라지면 항목도 사라진다
 */
SW_TEST_CASE( EditorPreferencesStoreTest, SectionExistsOnlyWhileItsOwnerLives )
{
    const utf8* pBegin = nullptr;
    {
        const EditorSettingsRegistrar<EditorPreferencesProbe> registrar{ "probe", "Test/Probe", 0, nullptr };
        const EditorSettingsRegistration&                     registration = registrar.getRegistration();
        pBegin                                                             = static_cast<const utf8*>( registration._pInstance );
        SW_EXPECT_TRUE( &getSettingsRegistration( EditorPreferencesProbe::StaticType() ) == &registration );
        SW_EXPECT_TRUE( EditorPreferencesStore::hasSectionWithin( pBegin, pBegin + sizeof( EditorPreferencesProbe ) ) );
    }
    SW_EXPECT_FALSE( EditorPreferencesStore::hasSectionWithin( pBegin, pBegin + sizeof( EditorPreferencesProbe ) ) );
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
