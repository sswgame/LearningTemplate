// DevConsoleRegistryTest — 개발 명령 · 디버그 HUD 레지스트리: 등록 규칙, 찾기 · 이름 모으기, 인자 읽기, HUD 켜짐 상태 글(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Console/DebugHUDRegistry.h"
#include "Engine/Console/DevCommandRegistry.h"

using namespace sw;

namespace
{
    bool doNothingCommand( const vector<string>& /*listArgument*/, string& /*outReply*/ )
    {
        return true;
    }

    void writeNothing( DebugHUDSectionWriter& /*writer*/ )
    {
    }

    DevCommandRegistration makeCommand( const utf8* pName )
    {
        return DevCommandRegistration{ pName, "usage", "help", &doNothingCommand, nullptr };
    }

    DebugHUDSectionRegistration makeSection( const utf8* pName, int32 order = 0, uint8 flags = DebugHUDSectionFlag::kNone )
    {
        return DebugHUDSectionRegistration{ pName, "Title", &writeNothing, order, flags };
    }
} // namespace

/** @brief [DevConsoleRegistryTest] 명령: 이름은 대소문자를 가리지 않고 둘째 등록은 거절한다. 이름이나 본문이 없으면 받지 않는다. 뺀 뒤에는 찾지 못한다 */
SW_TEST_CASE( DevConsoleRegistryTest, CommandRegistration )
{
    SW_EXPECT_LOG( "Registration.DuplicateName" );
    DevCommandRegistry           registry;
    const DevCommandRegistration first  = makeCommand( "debug.draw" );
    const DevCommandRegistration second = makeCommand( "DEBUG.DRAW" );
    SW_REQUIRE_TRUE( registry.registerEntry( &first ) );
    SW_EXPECT_FALSE( registry.registerEntry( &second ) );
    SW_EXPECT_TRUE( registry.findCommand( "Debug.Draw" ) == &first );
    SW_EXPECT_EQUAL( uint32( 1 ), registry.getCount() );

    SW_EXPECT_FALSE( registry.registerEntry( nullptr ) );
    const DevCommandRegistration noName = makeCommand( "" );
    SW_EXPECT_FALSE( registry.registerEntry( &noName ) );
    const DevCommandRegistration noBody{ "nobody", "", "", nullptr, nullptr };
    SW_EXPECT_FALSE( registry.registerEntry( &noBody ) );

    registry.unregisterEntry( &second ); // 등록되지 않은 줄은 아무 일도 없다
    SW_EXPECT_EQUAL( uint32( 1 ), registry.getCount() );
    registry.unregisterEntry( &first );
    SW_EXPECT_TRUE( registry.findCommand( "debug.draw" ) == nullptr );
}

/** @brief [DevConsoleRegistryTest] 이름 모으기는 접두어(대소문자 무시)로 거르고 사전순이다 */
SW_TEST_CASE( DevConsoleRegistryTest, CollectNamesByPrefix )
{
    DevCommandRegistry           registry;
    const DevCommandRegistration zebra = makeCommand( "net.zebra" );
    const DevCommandRegistration alpha = makeCommand( "net.alpha" );
    const DevCommandRegistration other = makeCommand( "hud" );
    SW_REQUIRE_TRUE( registry.registerEntry( &zebra ) );
    SW_REQUIRE_TRUE( registry.registerEntry( &alpha ) );
    SW_REQUIRE_TRUE( registry.registerEntry( &other ) );

    vector<string> listName{ "stale" };
    registry.collectNames( "NET.", listName );
    SW_REQUIRE_EQUAL( size_t( 2 ), listName.size() );
    SW_EXPECT_EQUAL( string( "net.alpha" ), listName[0] );
    SW_EXPECT_EQUAL( string( "net.zebra" ), listName[1] );
    registry.collectNames( "", listName );
    SW_EXPECT_EQUAL( size_t( 3 ), listName.size() );
}

/** @brief [DevConsoleRegistryTest] 개수 인자: 숫자만, 상한 1000000 까지다. 부호 · 공백 · 소수 · 빈 글 · 상한을 넘는 수는 거절한다 */
SW_TEST_CASE( DevConsoleRegistryTest, CountArgument )
{
    uint32 value = 77;
    SW_EXPECT_TRUE( DevCommandRegistry::readCountArgument( "0", value ) );
    SW_EXPECT_EQUAL( uint32( 0 ), value );
    SW_EXPECT_TRUE( DevCommandRegistry::readCountArgument( "1000000", value ) );
    SW_EXPECT_EQUAL( uint32( 1000000 ), value );

    value = 77;
    for ( const utf8* pBad : { "", "-1", "+1", " 1", "1.5", "abc", "1000001", "10000000", "99999999999999999999999" } )
    {
        SW_EXPECT_FALSE( DevCommandRegistry::readCountArgument( pBad, value ) );
    }
    SW_EXPECT_EQUAL( uint32( 77 ), value );
}

/** @brief [DevConsoleRegistryTest] 경로 인자는 절대 경로만 받고 거절하면 이유를 답한다. 불리언 인자는 없으면 기본값을 둔다 */
SW_TEST_CASE( DevConsoleRegistryTest, PathAndBoolArguments )
{
    string absolutePath;
    string reply;
    SW_EXPECT_TRUE( DevCommandRegistry::readDiskPathArgument( test::makeScratchPath( "a.txt" ), absolutePath, reply ) );
    SW_EXPECT_FALSE( absolutePath.empty() );
    SW_EXPECT_FALSE( DevCommandRegistry::readDiskPathArgument( "relative/a.txt", absolutePath, reply ) );
    SW_EXPECT_TRUE( reply.find( "relative/a.txt" ) != string::npos );

    bool bValue = true;
    SW_EXPECT_TRUE( readOptionalBoolArgument( {}, 0, bValue ) );
    SW_EXPECT_TRUE( bValue );
    SW_EXPECT_TRUE( readOptionalBoolArgument( { "off" }, 0, bValue ) );
    SW_EXPECT_FALSE( bValue );
    SW_EXPECT_FALSE( readOptionalBoolArgument( { "maybe" }, 0, bValue ) );
}

/** @brief [DevConsoleRegistryTest] HUD 섹션: 명령 낱말 · 불리언 철자 · 구분자가 든 이름은 받지 않는다. 순서는 차례, 같으면 이름이다. 리비전은 바뀔 때만 오른다 */
SW_TEST_CASE( DevConsoleRegistryTest, HUDSectionRegistration )
{
    SW_EXPECT_LOG( "DebugHUD.DebugHUDSectionReserved" );
    SW_EXPECT_LOG( "Registration.DuplicateName" );
    DebugHUDRegistry registry;
    for ( const utf8* pBad : { "list", "OPACITY", "on", "off", "1", "two words", "a,b", "-minus" } )
    {
        const DebugHUDSectionRegistration bad = makeSection( pBad );
        SW_EXPECT_FALSE( registry.registerEntry( &bad ) );
    }
    SW_EXPECT_FALSE( registry.registerEntry( nullptr ) );
    SW_EXPECT_EQUAL( uint64( 0 ), registry.getRevision() );

    const DebugHUDSectionRegistration late  = makeSection( "zeta", 5 );
    const DebugHUDSectionRegistration bravo = makeSection( "bravo", 1 );
    const DebugHUDSectionRegistration alpha = makeSection( "alpha", 1 );
    const DebugHUDSectionRegistration twin  = makeSection( "ALPHA", 9 );
    SW_REQUIRE_TRUE( registry.registerEntry( &late ) );
    SW_REQUIRE_TRUE( registry.registerEntry( &bravo ) );
    SW_REQUIRE_TRUE( registry.registerEntry( &alpha ) );
    const uint64 revision = registry.getRevision();
    SW_EXPECT_FALSE( registry.registerEntry( &twin ) );
    SW_EXPECT_EQUAL( revision, registry.getRevision() );

    const vector<const DebugHUDSectionRegistration*> listSection = registry.getSections();
    SW_REQUIRE_EQUAL( size_t( 3 ), listSection.size() );
    SW_EXPECT_TRUE( listSection[0] == &alpha );
    SW_EXPECT_TRUE( listSection[1] == &bravo );
    SW_EXPECT_TRUE( listSection[2] == &late );
    SW_EXPECT_TRUE( registry.findSection( "Bravo" ) == &bravo );

    registry.unregisterEntry( &bravo );
    SW_EXPECT_TRUE( registry.findSection( "bravo" ) == nullptr );
    SW_EXPECT_EQUAL( revision + 1, registry.getRevision() );
}

/** @brief [DevConsoleRegistryTest] HUD 상태 글: 없으면 기본값, 낱말이 정하고 마지막이 이긴다. 고쳐 쓰기는 기본값과 같으면 낱말을 빼고 다른 낱말은 둔다 */
SW_TEST_CASE( DevConsoleRegistryTest, HUDStateText )
{
    const DebugHUDSectionRegistration offByDefault = makeSection( "fps" );
    const DebugHUDSectionRegistration onByDefault  = makeSection( "net", 0, DebugHUDSectionFlag::kDefaultOn );
    SW_EXPECT_FALSE( DebugHUDSectionState::isSectionShown( "", offByDefault ) );
    SW_EXPECT_TRUE( DebugHUDSectionState::isSectionShown( "", onByDefault ) );
    SW_EXPECT_TRUE( DebugHUDSectionState::isSectionShown( "net FPS", offByDefault ) );
    SW_EXPECT_FALSE( DebugHUDSectionState::isSectionShown( "-net", onByDefault ) );
    SW_EXPECT_FALSE( DebugHUDSectionState::isSectionShown( "fps,-fps", offByDefault ) );
    SW_EXPECT_FALSE( DebugHUDSectionState::isSectionShown( "fpsx", offByDefault ) ); // 앞만 같은 이름은 다른 낱말이다

    SW_EXPECT_EQUAL( string( "fps" ), DebugHUDSectionState::makeStateText( "", "fps", true, false ) );
    SW_EXPECT_EQUAL( string( "-net" ), DebugHUDSectionState::makeStateText( "", "net", false, true ) );
    SW_EXPECT_EQUAL( string( "" ), DebugHUDSectionState::makeStateText( "fps", "fps", false, false ) );
    SW_EXPECT_EQUAL( string( "net -physics fps" ), DebugHUDSectionState::makeStateText( "net, FPS  -physics", "fps", true, false ) );
    SW_EXPECT_EQUAL( string( "net" ), DebugHUDSectionState::makeStateText( "-fps net", "fps", false, false ) );
}
