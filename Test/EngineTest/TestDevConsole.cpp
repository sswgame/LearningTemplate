#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Utility/Console/DevCommandRegistry.h"
#include "Engine/Utility/Console/DevConsole.h"
#include "Engine/Window/DevConsoleOverlay.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

#if SW_DEV_COMMANDS_ENABLED

namespace
{
    /** @brief 시험 명령의 본문입니다 — 인자를 이어 답하고, 인자가 없으면 실패(사용법)입니다. */
    bool runEchoForTest( const vector<string>& listArgument, string& outReply )
    {
        if ( listArgument.empty() )
            return false;
        for ( const string& argument : listArgument )
        {
            outReply += "[" + argument + "]";
        }
        return true;
    }

    const DevCommandRegistration kEchoCommand{ "test.echo", "test.echo <words...>", "Echo the arguments (test)", &runEchoForTest };

    /** @brief 시험용 전역 변수 표입니다(프로세스 표를 건드리지 않는다). */
    struct ConsoleVariableFixture
    {
        GlobalVariableManager _manager;
        float32               _speed{ 1.5f };
        int32                 _count{ 3 };

        ConsoleVariableFixture()
        {
            (void)_manager.registerVariable( "gv_testSpeed", GlobalVariableType::Float, &_speed, 1.5f, "test speed" );
            (void)_manager.registerVariable( "gv_testSpawnCount", GlobalVariableType::Int32, &_count, int32{ 3 }, "test count" );
        }
    };
} // namespace

/**
 * @brief [DevCommandRegistryTest] 등록자는 만들 때 올리고 없앨 때 빼며, 이름은 대소문자를 가리지 않고, 같은 이름의 둘째는 거절된다
 * @details 게임 모듈을 내리면(핫 리로드) 그 모듈의 정적 등록자가 사라지며 명령이 빠진다 — 지운 코드를 가리키는 명령이 남으면 안 된다.
 */
SW_TEST_CASE( DevCommandRegistryTest, RegistrarAddsAndRemovesByScope )
{
    DevCommandRegistry& registry = DevCommandRegistry::get();
    SW_EXPECT_NOT_NULL( registry.findCommand( "timescale" ) ); // 엔진이 내주는 명령
    SW_EXPECT_NULL( registry.findCommand( "test.echo" ) );
    {
        test::ScopedLogSuppressor suppressor;
        const DevCommandRegistrar registrar{ &kEchoCommand };
        SW_EXPECT_TRUE( registry.findCommand( "TEST.Echo" ) == &kEchoCommand );
        const DevCommandRegistration duplicate{ "test.echo", "", "", &runEchoForTest };
        const DevCommandRegistrar    secondRegistrar{ &duplicate };
        SW_EXPECT_FALSE( secondRegistrar._bRegistered );

        vector<string> listName;
        registry.collectNames( "test.", listName );
        SW_ASSERT_EQUAL( size_t( 1 ), listName.size() );
        SW_EXPECT_STREQ( "test.echo", listName[0].c_str() );
    }
    SW_EXPECT_TRUE_MSG( registry.findCommand( "test.echo" ) == nullptr, "a registrar that went out of scope left its command behind" );
}

/**
 * @brief [DevConsoleTest] 낱말 나누기는 큰따옴표를 한 낱말로 묶는다
 */
SW_TEST_CASE( DevConsoleTest, TokenizeHonoursQuotes )
{
    vector<string> listToken;
    DevConsole::tokenize( "  teleport \"Main Camera\"  1 2.5 -3 ", listToken );
    SW_ASSERT_EQUAL( size_t( 5 ), listToken.size() );
    SW_EXPECT_STREQ( "teleport", listToken[0].c_str() );
    SW_EXPECT_STREQ( "Main Camera", listToken[1].c_str() );
    SW_EXPECT_STREQ( "-3", listToken[4].c_str() );
    DevConsole::tokenize( "say \"\"", listToken );
    SW_ASSERT_EQUAL( size_t( 2 ), listToken.size() );
    SW_EXPECT_TRUE( listToken[1].empty() );
}

/**
 * @brief [DevConsoleTest] 전역 변수는 `get` · `set` · 이름만으로 읽고 쓰며, 명령은 인자를 받고, 실패는 사용법을 붙인다
 */
SW_TEST_CASE( DevConsoleTest, ExecutesVariablesAndCommands )
{
    test::ScopedLogSuppressor suppressor;
    ConsoleVariableFixture    fixture;
    const DevCommandRegistrar registrar{ &kEchoCommand };
    DevConsole                console{ &fixture._manager };
    string                    reply;

    SW_EXPECT_TRUE( console.execute( "set gv_testSpeed 0.25", reply ) == DevConsoleResult::Ok );
    SW_EXPECT_NEAR_EQUAL( 0.25f, fixture._speed, 1e-6f );
    SW_EXPECT_TRUE( console.execute( "gv_testSpawnCount 12", reply ) == DevConsoleResult::Ok );
    SW_EXPECT_EQUAL( 12, fixture._count );
    SW_EXPECT_TRUE( console.execute( "GET gv_testSpawnCount", reply ) == DevConsoleResult::Ok );
    SW_EXPECT_STREQ( "gv_testSpawnCount = 12", reply.c_str() );
    SW_EXPECT_TRUE( console.execute( "gv_testSpawnCount", reply ) == DevConsoleResult::Ok );
    SW_EXPECT_TRUE( console.execute( "set gv_testSpawnCount many", reply ) == DevConsoleResult::Failed );
    SW_EXPECT_EQUAL( 12, fixture._count );
    SW_EXPECT_TRUE( console.execute( "set gv_missing 1", reply ) == DevConsoleResult::Failed );

    SW_EXPECT_TRUE( console.execute( "test.echo a \"b c\"", reply ) == DevConsoleResult::Ok );
    SW_EXPECT_STREQ( "[a][b c]", reply.c_str() );
    SW_EXPECT_TRUE( console.execute( "test.echo", reply ) == DevConsoleResult::Failed );
    SW_EXPECT_TRUE( reply.find( "usage: test.echo <words...>" ) != string::npos );
    SW_EXPECT_TRUE( console.execute( "nonsense 1", reply ) == DevConsoleResult::UnknownCommand );
    SW_EXPECT_TRUE( console.execute( "   ", reply ) == DevConsoleResult::Empty );
    SW_EXPECT_TRUE( console.execute( "help test.", reply ) == DevConsoleResult::Ok );
    SW_EXPECT_TRUE( reply.find( "Echo the arguments" ) != string::npos );
}

/**
 * @brief [DevConsoleTest] 자동완성은 첫 낱말에 명령 · 변수를, `set`/`get` 뒤에 변수를 내고, 하나면 채우고 여럿이면 공통 접두어까지 늘린다
 */
SW_TEST_CASE( DevConsoleTest, CompletesCommandsAndVariables )
{
    ConsoleVariableFixture    fixture;
    const DevCommandRegistrar registrar{ &kEchoCommand };
    const DevConsole          console{ &fixture._manager };
    vector<string>            listCandidate;

    console.collectCompletions( "test.", listCandidate );
    SW_ASSERT_EQUAL( size_t( 1 ), listCandidate.size() );
    SW_EXPECT_STREQ( "test.echo", listCandidate[0].c_str() );

    string line = "TEST.e";
    SW_EXPECT_TRUE( console.complete( line, listCandidate ) );
    SW_EXPECT_STREQ( "test.echo ", line.c_str() );

    line = "set gv_testSp";
    SW_EXPECT_FALSE( console.complete( line, listCandidate ) ); // gv_testSpeed · gv_testSpawnCount 의 공통 접두어 그대로라 바뀌지 않는다
    line = "set gv_t";
    SW_EXPECT_TRUE( console.complete( line, listCandidate ) );
    SW_EXPECT_STREQ( "set gv_testSp", line.c_str() );
    SW_EXPECT_EQUAL( size_t( 2 ), listCandidate.size() );

    line = "set gv_testSpe";
    SW_EXPECT_TRUE( console.complete( line, listCandidate ) );
    SW_EXPECT_STREQ( "set gv_testSpeed ", line.c_str() );

    console.collectCompletions( "test.echo x", listCandidate ); // 명령의 인자는 완성하지 않는다
    SW_EXPECT_TRUE( listCandidate.empty() );
}

/**
 * @brief [DevConsoleTest] 입력 기록은 ↑ 로 오래된 쪽, ↓ 로 새 쪽으로 가고 끝을 지나면 빈 줄이다. 같은 줄을 잇달아 넣으면 하나로 남는다
 */
SW_TEST_CASE( DevConsoleTest, HistoryWalksBothWays )
{
    test::ScopedLogSuppressor suppressor;
    ConsoleVariableFixture    fixture;
    DevConsole                console{ &fixture._manager };
    (void)console.submit( "gv_testSpeed 1" );
    (void)console.submit( "gv_testSpeed 2" );
    (void)console.submit( "gv_testSpeed 2" );
    SW_ASSERT_EQUAL( size_t( 2 ), console.getHistory().size() );

    const string* pLine = console.moveHistoryBack();
    SW_ASSERT_NOT_NULL( pLine );
    SW_EXPECT_STREQ( "gv_testSpeed 2", pLine->c_str() );
    pLine = console.moveHistoryBack();
    SW_ASSERT_NOT_NULL( pLine );
    SW_EXPECT_STREQ( "gv_testSpeed 1", pLine->c_str() );
    SW_EXPECT_NULL( console.moveHistoryBack() );
    pLine = console.moveHistoryForward();
    SW_ASSERT_NOT_NULL( pLine );
    SW_EXPECT_STREQ( "gv_testSpeed 2", pLine->c_str() );
    pLine = console.moveHistoryForward();
    SW_ASSERT_NOT_NULL( pLine );
    SW_EXPECT_TRUE( pLine->empty() );
    SW_EXPECT_NULL( console.moveHistoryForward() );

    // 출력 줄: 입력 줄과 답이 쌓인다.
    SW_EXPECT_TRUE( console.getOutput().size() >= 4 );
    SW_EXPECT_STREQ( "> gv_testSpeed 1", console.getOutput()[0]._text.c_str() );
}

/**
 * @brief [DevConsoleOverlayTest] 게임 창 오버레이 — 닫혀 있으면 여는 키만 가져가고, 열리면 글자(UTF-8) · 지우기 · Enter · 기록 · Esc 를 처리한다
 */
SW_TEST_CASE( DevConsoleOverlayTest, KeysEditAndSubmitTheLine )
{
    test::ScopedLogSuppressor suppressor;
    DevConsoleOverlay         overlay;
    const auto                makeKey = []( DevConsoleKey::Kind kind, uint32 codepoint ) -> DevConsoleKey
    {
        DevConsoleKey key{};
        key._kind      = kind;
        key._codepoint = codepoint;
        return key;
    };

    SW_EXPECT_FALSE( overlay.handleKey( makeKey( DevConsoleKey::Kind::Character, 'a' ) ) ); // 닫혀 있으면 게임 입력이다
    SW_EXPECT_TRUE( overlay.handleKey( makeKey( DevConsoleKey::Kind::Toggle, 0 ) ) );
    SW_EXPECT_TRUE( overlay.isOpen() );

    for ( const utf8 ch : string( "gv_x" ) )
    {
        SW_EXPECT_TRUE( overlay.handleKey( makeKey( DevConsoleKey::Kind::Character, static_cast<uint32>( ch ) ) ) );
    }
    SW_EXPECT_TRUE( overlay.handleKey( makeKey( DevConsoleKey::Kind::Character, 0xAC00 ) ) ); // '가' — UTF-8 세 바이트
    SW_EXPECT_EQUAL( size_t( 7 ), overlay.getInputLine().size() );
    SW_EXPECT_TRUE( overlay.handleKey( makeKey( DevConsoleKey::Kind::Backspace, 0 ) ) ); // 세 바이트를 한 번에 지운다
    SW_EXPECT_STREQ( "gv_x", overlay.getInputLine().c_str() );

    SW_EXPECT_TRUE( overlay.handleKey( makeKey( DevConsoleKey::Kind::Enter, 0 ) ) );
    SW_EXPECT_TRUE( overlay.getInputLine().empty() );
    vector<string> listLine;
    vector<uint8>  listErrorFlag;
    overlay.buildVisibleLines( listLine, listErrorFlag );
    SW_ASSERT_TRUE( listLine.size() >= 3 );
    SW_EXPECT_STREQ( "> gv_x", listLine[listLine.size() - 3].c_str() );
    SW_EXPECT_TRUE( listErrorFlag[listLine.size() - 2] != 0 ); // 모르는 명령은 오류 색
    SW_EXPECT_STREQ( "] _", listLine.back().c_str() );

    SW_EXPECT_TRUE( overlay.handleKey( makeKey( DevConsoleKey::Kind::HistoryBack, 0 ) ) );
    SW_EXPECT_STREQ( "gv_x", overlay.getInputLine().c_str() );
    SW_EXPECT_TRUE( overlay.handleKey( makeKey( DevConsoleKey::Kind::Close, 0 ) ) );
    SW_EXPECT_FALSE( overlay.isOpen() );
}

#else

/**
 * @brief [DevCommandRegistryTest] Shipping 에는 개발 명령이 없다 — 매크로는 아무것도 남기지 않는다
 */
SW_TEST_CASE( DevCommandRegistryTest, IsCompiledOutOfShipping )
{
    static_assert( SW_DEV_COMMANDS_ENABLED == 0, "Shipping must compile the dev command registry out" );
    SW_EXPECT_TRUE( true );
}

#endif
