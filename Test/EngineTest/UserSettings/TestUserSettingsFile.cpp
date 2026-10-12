#include "pch.h"

#include "Core/Container/StringUtil.h"

#include "Engine/UserSettings/UserSettingsFile.h"
#include "Engine/UserSettings/UserSettingsSchema.h"

#include "TestFramework/TestFramework.h"

// 사용자 파일 형식(usersettings.json) — 매니저 없이 JSON 글과 키 · 값 목록을 오가고, 옛 버전 파일을 버전 단계로 올린다.

namespace
{
    struct UserSettingsFileTestInternal
    {
        /** @brief 버전 3 스키마 — 버전 1 · 2 의 올리기 단계 넷(rename · scale · mapValue · remove)과 타입 셋(float · enum · bool). */
        static constexpr const utf8* kSchemaXML = R"(<UserSettingsSchema version="3"><Category id="a"/>
        <Setting id="a.volume" category="a" type="float" default="1" min="0" max="1"/>
        <Setting id="a.mode" category="a" type="enum" default="windowed"><Option value="windowed"/><Option value="borderless"/></Setting>
        <Setting id="a.flag" category="a" type="bool" default="false"/>
        <Upgrade version="1" op="rename" key="a.masterVolume" to="a.volume"/>
        <Upgrade version="1" op="scale" key="a.volume" scale="0.01"/>
        <Upgrade version="2" op="mapValue" key="a.mode" valueFrom="fullscreen" valueTo="borderless"/>
        <Upgrade version="2" op="remove" key="a.legacy"/></UserSettingsSchema>)";

        static const sw::UserSettingsFileEntry* findEntry( const sw::vector<sw::UserSettingsFileEntry>& listEntry, const utf8* pKey )
        {
            for ( const sw::UserSettingsFileEntry& entry : listEntry )
            {
                if ( entry._key == pKey )
                    return &entry;
            }
            return nullptr;
        }
    };
} // namespace

/**
 * @brief [UserSettingsFileTest] 옛 버전 파일은 버전마다 단계를 적힌 순서대로 올린다 — 모르는 키는 형식이 버리지 않고 매니저에 넘긴다
 */
SW_TEST_CASE( UserSettingsFileTest, ParseUpgradesOldVersionsInOrder )
{
    using Internal = UserSettingsFileTestInternal;
    sw::UserSettingsSchema schema;
    SW_ASSERT_TRUE( schema.loadFromXMLText( Internal::kSchemaXML, "file.settings.xml" ) );

    sw::vector<sw::UserSettingsFileEntry> listEntry;
    SW_ASSERT_TRUE( sw::UserSettingsFile::parseJSON( R"({ "version": 1, "values": { "a.masterVolume": 50, "a.mode": "fullscreen", "a.legacy": true,
                                                          "a.unknown": 3, "a.flag": true } })",
                                                     "old.json", schema, listEntry ) );

    const sw::UserSettingsFileEntry* pVolume = Internal::findEntry( listEntry, "a.volume" );
    SW_ASSERT_TRUE( pVolume != nullptr );
    float64 volume{ 0.0 };
    SW_ASSERT_TRUE( sw::StringUtil::parseDouble( pVolume->_value, volume ) );
    SW_EXPECT_NEAR_EQUAL( 0.5, volume, 0.0001 );
    SW_EXPECT_TRUE( Internal::findEntry( listEntry, "a.masterVolume" ) == nullptr );

    const sw::UserSettingsFileEntry* pMode = Internal::findEntry( listEntry, "a.mode" );
    SW_ASSERT_TRUE( pMode != nullptr );
    SW_EXPECT_STREQ( "borderless", pMode->_value.c_str() );
    SW_EXPECT_TRUE( Internal::findEntry( listEntry, "a.legacy" ) == nullptr );
    SW_EXPECT_TRUE( Internal::findEntry( listEntry, "a.unknown" ) != nullptr );

    const sw::UserSettingsFileEntry* pFlag = Internal::findEntry( listEntry, "a.flag" );
    SW_ASSERT_TRUE( pFlag != nullptr );
    SW_EXPECT_STREQ( "true", pFlag->_value.c_str() );
}

/**
 * @brief [UserSettingsFileTest] JSON 이 아니거나 values 오브젝트가 없으면 읽지 않는다(매니저는 기본값으로 둔다)
 */
SW_TEST_CASE( UserSettingsFileTest, ParseRejectsBrokenFiles )
{
    using Internal = UserSettingsFileTestInternal;
    sw::UserSettingsSchema schema;
    SW_ASSERT_TRUE( schema.loadFromXMLText( Internal::kSchemaXML, "file.settings.xml" ) );

    SW_TEST_DEFENSIVE_SCOPE( "broken user settings files are refused with a warning" );
    sw::vector<sw::UserSettingsFileEntry> listEntry;
    SW_EXPECT_FALSE( sw::UserSettingsFile::parseJSON( "{ not json", "broken.json", schema, listEntry ) );
    SW_EXPECT_FALSE( sw::UserSettingsFile::parseJSON( R"({ "version": 3 })", "novalues.json", schema, listEntry ) );
    SW_EXPECT_TRUE( listEntry.empty() );
}

/**
 * @brief [UserSettingsFileTest] 쓰는 글은 지금 스키마 버전이고, 불리언 · 숫자 설정은 JSON 불리언 · 숫자다 — 다시 읽으면 같은 값이다
 */
SW_TEST_CASE( UserSettingsFileTest, MakeWritesTypedValuesThatParseBack )
{
    using Internal = UserSettingsFileTestInternal;
    sw::UserSettingsSchema schema;
    SW_ASSERT_TRUE( schema.loadFromXMLText( Internal::kSchemaXML, "file.settings.xml" ) );

    const sw::vector<sw::UserSettingsFileEntry> listEntry{
        {"a.volume",       "0.25"},
        {  "a.mode", "borderless"},
        {  "a.flag",       "true"},
    };
    const sw::string json = sw::UserSettingsFile::makeJSON( schema, listEntry );
    SW_EXPECT_TRUE( json.find( "\"version\": 3" ) != sw::string::npos );
    SW_EXPECT_TRUE( json.find( "\"a.flag\": true" ) != sw::string::npos );
    SW_EXPECT_TRUE( json.find( "\"a.mode\": \"borderless\"" ) != sw::string::npos );
    SW_EXPECT_TRUE( json.find( "\"a.volume\": \"" ) == sw::string::npos );

    sw::vector<sw::UserSettingsFileEntry> listParsed;
    SW_ASSERT_TRUE( sw::UserSettingsFile::parseJSON( json, "roundtrip.json", schema, listParsed ) );
    SW_ASSERT_EQUAL( listEntry.size(), listParsed.size() );
    const sw::UserSettingsFileEntry* pVolume = Internal::findEntry( listParsed, "a.volume" );
    SW_ASSERT_TRUE( pVolume != nullptr );
    float64 volume{ 0.0 };
    SW_ASSERT_TRUE( sw::StringUtil::parseDouble( pVolume->_value, volume ) );
    SW_EXPECT_NEAR_EQUAL( 0.25, volume, 0.0001 );
    const sw::UserSettingsFileEntry* pMode = Internal::findEntry( listParsed, "a.mode" );
    const sw::UserSettingsFileEntry* pFlag = Internal::findEntry( listParsed, "a.flag" );
    SW_ASSERT_TRUE( pMode != nullptr && pFlag != nullptr );
    SW_EXPECT_STREQ( "borderless", pMode->_value.c_str() );
    SW_EXPECT_STREQ( "true", pFlag->_value.c_str() );
}
