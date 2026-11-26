// UserSettingsFileEntry — 사용자 파일의 주인별 판 · 올림 · 새 판 파일 · 모르는 키 보존(nogpu).
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Serialization/JSON/JSONDocument.h"
#include "Engine/UserSettings/UserSettingsFileEntry.h"
#include "Engine/UserSettings/UserSettingsSchema.h"

namespace
{
    /** @brief 엔진 판 @p engineXML 위에 게임 판 @p gameXML 을 덧붙인 스키마입니다. */
    void loadEngineAndGame( sw::UserSettingsSchema& schema, const char* engineXML, const char* gameXML )
    {
        SW_REQUIRE_TRUE( schema.loadFromXMLText( engineXML, "engine" ) );
        SW_REQUIRE_TRUE( schema.loadFromXMLText( gameXML, "game" ) );
    }

    const sw::UserSettingsFileEntry* findEntry( const sw::UserSettingsFileContent& content, const char* pKey )
    {
        for ( const sw::UserSettingsFileEntry& entry : content._listEntry )
        {
            if ( entry._key == pKey )
                return &entry;
        }
        return nullptr;
    }
} // namespace

/**
 * @brief [UserSettingsFileEntryTest] 엔진 판이 게임 판보다 커도 게임의 올림 단계가 돈다
 * @details 판 번호 하나(최댓값)를 쓰던 때는 엔진 3 · 게임 1 파일이 "version": 3 이었고, 게임이 2 로 올려도 스키마 판(최댓값)이 3 이라 단계가 돌지 않았다.
 */
SW_TEST_CASE( UserSettingsFileEntryTest, GameUpgradeRunsWhenEngineVersionIsHigher )
{
    sw::UserSettingsSchema schema;
    loadEngineAndGame( schema, "<UserSettingsSchema owner=\"engine\" version=\"3\"><Setting id=\"audio.master\" type=\"Float\" default=\"0.5\" min=\"0\" max=\"1\"/></UserSettingsSchema>",
                       "<UserSettingsSchema owner=\"game\" version=\"2\"><Setting id=\"game.speed\" type=\"Int\" default=\"1\" min=\"0\" max=\"10\"/>"
                       "<Upgrade version=\"1\" op=\"Rename\" key=\"game.oldSpeed\" to=\"game.speed\"/></UserSettingsSchema>" );

    sw::UserSettingsFileContent content;
    SW_REQUIRE_TRUE( sw::UserSettingsFileEntry::parseJSON( R"({ "versions": { "engine": 3, "game": 1 }, "values": { "game.oldSpeed": 4 } })", "test", schema, content ) ==
                     sw::UserSettingsFileReadResult::Read );
    const sw::UserSettingsFileEntry* pSpeed = findEntry( content, "game.speed" );
    SW_REQUIRE_TRUE( pSpeed != nullptr );
    SW_EXPECT_EQUAL( sw::string( "4" ), pSpeed->_value );
    SW_EXPECT_TRUE( findEntry( content, "game.oldSpeed" ) == nullptr );
}

/**
 * @brief [UserSettingsFileEntryTest] 판 칸이 없는 파일은 첫 판으로 받아들이지 않고 거절한다. 파일에 없는 주인은 지금 판이다(단계를 돌리지 않는다)
 */
SW_TEST_CASE( UserSettingsFileEntryTest, FileWithoutVersionsIsRefused )
{
    sw::UserSettingsSchema schema;
    loadEngineAndGame( schema, "<UserSettingsSchema owner=\"engine\"><Setting id=\"audio.master\" type=\"Float\" default=\"0.5\" min=\"0\" max=\"1\"/></UserSettingsSchema>",
                       "<UserSettingsSchema owner=\"game\" version=\"1\"><Setting id=\"game.speed\" type=\"Int\" default=\"1\" min=\"0\" max=\"10\"/>"
                       "<Upgrade version=\"0\" op=\"Rename\" key=\"game.oldSpeed\" to=\"game.speed\"/></UserSettingsSchema>" );

    sw::UserSettingsFileContent content;
    SW_EXPECT_LOG( "UserSettings.UserSettingsNoVersions" );
    SW_EXPECT_TRUE( sw::UserSettingsFileEntry::parseJSON( R"({ "values": { "audio.master": 0.25 } })", "test", schema, content ) == sw::UserSettingsFileReadResult::Unreadable );
    SW_REQUIRE_TRUE( sw::UserSettingsFileEntry::parseJSON( R"({ "versions": { "engine": 0 }, "values": { "game.oldSpeed": 4, "audio.master": 0.25 } })", "test", schema, content ) ==
                     sw::UserSettingsFileReadResult::Read );
    SW_EXPECT_TRUE( findEntry( content, "game.oldSpeed" ) != nullptr );
    SW_EXPECT_TRUE( findEntry( content, "audio.master" ) != nullptr );
}

/**
 * @brief [UserSettingsFileEntryTest] 이 빌드보다 새 판 파일은 내용을 넘기지 않는다 — 옛 빌드가 그 값을 옛 판 번호로 다시 써 새 빌드가 `Scale` 을 또 돌리는 일이 없다
 * @details 판 번호 하나를 쓰던 때는 경고만 남기고 값을 넘겼고, 저장이 이 빌드의 판(옛 번호)을 적었다.
 */
SW_TEST_CASE( UserSettingsFileEntryTest, NewerFileIsNotRewrittenAsOlder )
{
    SW_EXPECT_LOG( "UserSettings.UserSettingsVersionNewer" );
    sw::UserSettingsSchema schema;
    loadEngineAndGame( schema,
                       "<UserSettingsSchema owner=\"engine\" version=\"2\"><Setting id=\"audio.master\" type=\"Float\" default=\"0.5\" min=\"0\" max=\"1\"/>"
                       "<Upgrade version=\"1\" op=\"Scale\" key=\"audio.master\" scale=\"0.01\"/></UserSettingsSchema>",
                       "<UserSettingsSchema owner=\"game\" version=\"1\"><Setting id=\"game.speed\" type=\"Int\" default=\"1\" min=\"0\" max=\"10\"/></UserSettingsSchema>" );

    sw::UserSettingsFileContent content;
    SW_EXPECT_TRUE( sw::UserSettingsFileEntry::parseJSON( R"({ "versions": { "engine": 3, "game": 1 }, "values": { "audio.master": 0.4 } })", "test", schema, content ) ==
                    sw::UserSettingsFileReadResult::NewerThanBuild );
    SW_EXPECT_TRUE( content._listEntry.empty() );
    SW_EXPECT_TRUE( content._listForeignOwner.empty() );

    // 이 빌드가 쓰는 파일은 자기 판을 주인별로 적는다 — 읽지 않은 새 파일의 값이 섞이지 않는다.
    const sw::expected<sw::string> text = sw::UserSettingsFileEntry::makeJSON( schema, {}, content );
    SW_REQUIRE_TRUE( text.has_value() );
    sw::JSONDocument doc;
    SW_REQUIRE_TRUE( doc.parse( *text, "test" ) );
    SW_EXPECT_EQUAL( uint32{ 2 }, doc.getRoot().get( "versions" ).get( "engine" ).read<uint32>().value() );
    SW_EXPECT_EQUAL( size_t{ 0 }, doc.getRoot().get( "values" ).size() );
}

/**
 * @brief [UserSettingsFileEntryTest] 이 빌드가 모르는 주인의 판 · 모르는 키는 원문 그대로 다시 쓴다(Shipping 이 Dev 의 debughud 값을 지우지 않는다)
 */
SW_TEST_CASE( UserSettingsFileEntryTest, UnknownKeysAndOwnersAreKeptAsWritten )
{
    sw::UserSettingsSchema schema;
    SW_REQUIRE_TRUE( schema.loadFromXMLText( "<UserSettingsSchema owner=\"engine\" version=\"1\"><Setting id=\"audio.master\" type=\"Float\" default=\"0.5\" min=\"0\" max=\"1\"/></UserSettingsSchema>",
                                             "engine" ) );

    sw::UserSettingsFileContent content;
    SW_REQUIRE_TRUE( sw::UserSettingsFileEntry::parseJSON( R"({ "versions": { "engine": 1, "debughud": 4 }, "values": { "audio.master": 0.5, "debug.hud": true, "debug.list": [ 1, 2 ] } })",
                                                           "test", schema, content ) == sw::UserSettingsFileReadResult::Read );
    SW_REQUIRE_EQUAL( size_t{ 1 }, content._listForeignOwner.size() );

    // 관리자처럼 아는 키는 쓸 목록으로, 모르는 키는 보관 몫으로 나눈다.
    const sw::UserSettingDef* pMaster = schema.findSetting( sw::hashed_string( "audio.master" ) );
    SW_REQUIRE_TRUE( pMaster != nullptr );
    sw::UserSettingsFileContent carried{ {}, content._listForeignOwner };
    for ( const sw::UserSettingsFileEntry& entry : content._listEntry )
    {
        if ( schema.findSetting( sw::hashed_string( entry._key ) ) == nullptr )
            carried._listEntry.push_back( entry );
    }
    const sw::expected<sw::string> text = sw::UserSettingsFileEntry::makeJSON( schema, {
                                                                                           sw::UserSettingsWriteEntry{ *pMaster, "0.25" }
    },
                                                                               carried );
    SW_REQUIRE_TRUE( text.has_value() );

    sw::JSONDocument doc;
    SW_REQUIRE_TRUE( doc.parse( *text, "test" ) );
    const sw::JSONValue versions = doc.getRoot().get( "versions" );
    SW_EXPECT_EQUAL( uint32{ 1 }, versions.get( "engine" ).read<uint32>().value() );
    SW_EXPECT_EQUAL( uint32{ 4 }, versions.get( "debughud" ).read<uint32>().value() );
    const sw::JSONValue values = doc.getRoot().get( "values" );
    SW_EXPECT_TRUE( values.get( "debug.hud" ).isBool() );
    SW_EXPECT_TRUE( values.get( "debug.list" ).isArray() && values.get( "debug.list" ).size() == 2 );
    SW_EXPECT_EQUAL( 0.25, values.get( "audio.master" ).read<float64>().value() );
}

/**
 * @brief [UserSettingsFileEntryTest] 판의 주인이 둘이면 스키마를 받지 않는다(한 판을 두 스키마가 나눠 쓰게 된다)
 */
SW_TEST_CASE( UserSettingsFileEntryTest, DuplicateOwnerIsRejected )
{
    SW_EXPECT_LOG( "UserSettingsSchema.OwnerDefinedTwice" );
    sw::UserSettingsSchema schema;
    SW_REQUIRE_TRUE( schema.loadFromXMLText( "<UserSettingsSchema owner=\"game\" version=\"1\"><Setting id=\"a.x\" type=\"Bool\" default=\"false\"/></UserSettingsSchema>", "a" ) );
    SW_EXPECT_FALSE( schema.loadFromXMLText( "<UserSettingsSchema owner=\"game\" version=\"1\"><Setting id=\"b.x\" type=\"Bool\" default=\"false\"/></UserSettingsSchema>", "b" ) );
    SW_EXPECT_EQUAL( size_t{ 1 }, schema.getOwners().size() );
}
