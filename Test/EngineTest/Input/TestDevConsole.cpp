#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Console/DevConsole.h"
#include "Engine/EngineLoop.h"
#include "Engine/Input/DevConsoleController.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Window/NativeWindowEvent.h"

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

    /**
     * @brief 게임 창 콘솔의 시험 자리입니다 — 입력 관리자 · 셸 맵(실제 `default.input.xml`) · 컨트롤러를 앱과 같은 순서로 돌립니다.
     * @details 플랫폼 창 없이 `RawInputEvent` 만 넣습니다(InputReplay 재생과 같은 길). 멤버 순서가 소멸 순서다 — 컨트롤러가 입력 관리자보다 먼저 내린다.
     */
    struct DevConsoleRig
    {
        InputManager         _input;
        unique_ptr<InputMap> _pShellMap;
        DevConsoleController _controller;
        bool                 _bFrameOpen{ false };

        bool initialize()
        {
            if ( _input.initialize() == false || ResourceUtil::initialize() == false )
                return false;
            _pShellMap = EngineLoop::createShellInputMap( EngineDefaultAssets{}._shellInputMap );
            _pShellMap->setInputManager( &_input );
            _pShellMap->setLayerEnabled( InputMapDefaults::kTitleLayerName, false ); // `EngineLoop::updateShellActions` 와 같다
            (void)_controller.initialize( &_input, nullptr );
            return _pShellMap->hasAction( InputMapDefaults::kDevConsoleToggleAction );
        }

        /**
         * @brief 한 프레임 — 지난 프레임을 닫고, 이벤트를 넣고 입력 관리자 → 셸 맵 → 콘솔 순서로 갱신합니다(`App::run` 과 같은 순서).
         * @details 프레임을 연 채로 돌아온다 — 엣지(`wasKeyPressed`)는 `endFrame` 이 지우므로 단언은 그 사이에 한다.
         */
        void runFrame( std::initializer_list<RawInputEvent> listEvent, float32 deltaSeconds = 0.016f )
        {
            if ( _bFrameOpen )
                _input.endFrame();
            for ( const RawInputEvent& event : listEvent )
            {
                (void)_input.postRawEvent( event );
            }
            _input.beginFrame( deltaSeconds );
            _pShellMap->update( deltaSeconds );
            _controller.update( *_pShellMap );
            _bFrameOpen = true;
        }

        /** @brief 키 하나를 누르고(그 키가 내는 글자와 함께) 다음 프레임에 뗍니다. */
        void tapKey( Key key, string_view text = {} )
        {
            if ( text.empty() )
                runFrame( { RawInputEvent::makeKeyDown( key ) } );
            else
                runFrame( { RawInputEvent::makeKeyDown( key ), RawInputEvent::makeTextInput( text ) } );
            runFrame( { RawInputEvent::makeKeyUp( key ) } );
        }

        void tapButton( GamepadButton button )
        {
            runFrame( { RawInputEvent::makeGamepadButtonDown( button ) } );
            runFrame( { RawInputEvent::makeGamepadButtonUp( button ) } );
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
 * @brief [DevConsoleControllerTest] 셸 맵(`default.input.xml`)에 콘솔 레이어와 액션 일곱이 있고, 콘솔 레이어는 닫힌 채로 시작한다
 * @details 액션 이름은 `InputMapDefaults` 상수 하나만 쓴다 — XML 과 코드가 어긋나면 여기서 진다(콘솔은 실행 중에 오류를 남기고 키를 받지 않는다).
 *          셸 맵은 키보드 포커스를 무시한다(콘솔이 열린 동안에도 닫는 키를 받는다).
 */
SW_TEST_CASE( DevConsoleControllerTest, ShellMapDeclaresTheConsoleActions )
{
    test::ScopedLogSuppressor suppressor;
    DevConsoleRig             rig;
    SW_ASSERT_TRUE( rig.initialize() );
    const InputMap& shellMap = *rig._pShellMap;
    SW_EXPECT_TRUE( shellMap.isKeyboardFocusIgnored() );
    SW_EXPECT_TRUE( shellMap.hasLayer( InputMapDefaults::kDevConsoleLayerName ) );
    SW_EXPECT_FALSE( shellMap.isLayerEnabled( InputMapDefaults::kDevConsoleLayerName ) );
    for ( const utf8* pActionName : { InputMapDefaults::kDevConsoleToggleAction, InputMapDefaults::kDevConsoleCloseAction, InputMapDefaults::kDevConsoleSubmitAction,
                                      InputMapDefaults::kDevConsoleCompleteAction, InputMapDefaults::kDevConsoleHistoryBackAction,
                                      InputMapDefaults::kDevConsoleHistoryForwardAction, InputMapDefaults::kDevConsoleDeleteBackwardAction } )
    {
        SW_EXPECT_TRUE_MSG( shellMap.hasAction( hashed_string( pActionName ) ), pActionName );
    }
    SW_EXPECT_TRUE( shellMap.findBindingLayer( InputMapDefaults::kDevConsoleToggleAction, 0 ) == hashed_string( "Debug" ) ); // 닫혀 있어도 받는다
    SW_EXPECT_TRUE( shellMap.findBindingLayer( InputMapDefaults::kDevConsoleSubmitAction, 0 ) == hashed_string( InputMapDefaults::kDevConsoleLayerName ) );
    SW_EXPECT_TRUE( shellMap.getBindingTrigger( InputMapDefaults::kDevConsoleHistoryBackAction, 0 ) == ActionTrigger::Repeat );
    SW_EXPECT_TRUE( shellMap.getBindingTrigger( InputMapDefaults::kDevConsoleDeleteBackwardAction, 0 ) == ActionTrigger::Repeat );

    rig.tapKey( Key::Grave, "`" );
    SW_EXPECT_TRUE( shellMap.isLayerEnabled( InputMapDefaults::kDevConsoleLayerName ) ); // 열려 있는 동안만 켠다
}

/**
 * @brief [DevConsoleControllerTest] 셸 맵의 콘솔 액션과 글자 입력만으로 연다 · 글자(UTF-8) · 지우기 · Enter · 기록 · Esc
 * @details 닫혀 있을 때 친 글자는 게임 몫이라 입력 줄에 들어가지 않는다. 키는 플랫폼 키코드가 아니라 `RawInputEvent` 로 들어온다.
 */
SW_TEST_CASE( DevConsoleControllerTest, KeysEditAndSubmitTheLine )
{
    test::ScopedLogSuppressor suppressor;
    DevConsoleRig             rig;
    SW_ASSERT_TRUE( rig.initialize() );

    rig.runFrame( { RawInputEvent::makeTextInput( "a" ) } ); // 닫혀 있으면 게임 입력이다
    SW_EXPECT_FALSE( rig._controller.isOpen() );
    rig.tapKey( Key::Grave, "`" );
    SW_ASSERT_TRUE( rig._controller.isOpen() );
    SW_EXPECT_TRUE( rig._input.getKeyboardFocus() == InputKeyboardFocus::DevConsole );
    SW_EXPECT_TRUE( rig._controller.getInputLine().empty() );

    rig.runFrame( { RawInputEvent::makeTextInput( "gv_x\xEA\xB0\x80" ) } ); // '가' — UTF-8 세 바이트
    SW_EXPECT_EQUAL( size_t( 7 ), rig._controller.getInputLine().size() );
    rig.tapKey( Key::Backspace ); // 세 바이트를 한 번에 지운다
    SW_EXPECT_STREQ( "gv_x", rig._controller.getInputLine().c_str() );

    rig.tapKey( Key::Enter, "\r" ); // Enter 가 내는 제어 문자는 글자가 아니다
    SW_EXPECT_TRUE( rig._controller.getInputLine().empty() );
    vector<string> listLine;
    vector<uint8>  listErrorFlag;
    rig._controller.buildVisibleLines( listLine, listErrorFlag );
    SW_ASSERT_TRUE( listLine.size() >= 3 );
    SW_EXPECT_STREQ( "> gv_x", listLine[listLine.size() - 3].c_str() );
    SW_EXPECT_TRUE( listErrorFlag[listLine.size() - 2] != 0 ); // 모르는 명령은 오류 색
    SW_EXPECT_STREQ( "] _", listLine.back().c_str() );

    rig.tapKey( Key::Up );
    SW_EXPECT_STREQ( "gv_x", rig._controller.getInputLine().c_str() );
    rig.tapKey( Key::Escape );
    SW_EXPECT_FALSE( rig._controller.isOpen() );
    SW_EXPECT_TRUE( rig._input.getKeyboardFocus() == InputKeyboardFocus::Game );
}

/**
 * @brief [DevConsoleControllerTest] 여닫는 키가 같은 프레임에 내는 글자(` · ~)는 입력 줄에 들어가지 않는다
 * @details 글자를 하드코딩해 거르지 않는다 — "그 프레임에 여닫기가 발화했으면 그 프레임 글자를 버린다". 닫는 프레임은 콘솔이 포커스를 쥔 채라
 *          그 글자가 콘솔 몫으로 온다. 다시 열면 닫기 전 입력 줄이 그대로 있어야 한다.
 */
SW_TEST_CASE( DevConsoleControllerTest, ToggleCharacterIsNotTyped )
{
    test::ScopedLogSuppressor suppressor;
    DevConsoleRig             rig;
    SW_ASSERT_TRUE( rig.initialize() );

    rig.tapKey( Key::Grave, "`" );
    SW_ASSERT_TRUE( rig._controller.isOpen() );
    SW_EXPECT_TRUE( rig._controller.getInputLine().empty() );
    rig.runFrame( { RawInputEvent::makeTextInput( "ab" ) } );

    rig.tapKey( Key::Grave, "`" ); // 닫는다 — 이 글자는 콘솔 몫으로 오지만 버린다
    SW_ASSERT_FALSE( rig._controller.isOpen() );
    rig.runFrame( { RawInputEvent::makeKeyDown( Key::LeftShift ), RawInputEvent::makeKeyDown( Key::Grave, 0, false, ModifierKey::Shift ), RawInputEvent::makeTextInput( "~" ) } );
    rig.runFrame( { RawInputEvent::makeKeyUp( Key::Grave ), RawInputEvent::makeKeyUp( Key::LeftShift ) } );
    SW_ASSERT_TRUE( rig._controller.isOpen() );
    SW_EXPECT_STREQ( "ab", rig._controller.getInputLine().c_str() );
}

/**
 * @brief [DevConsoleControllerTest] 닫혀 있으면 게임이 키를 받고, 열면 못 받으며, 닫으면 여는 동안 눌려 있던 키를 다시 받는다
 * @details 콘솔이 창 메시지를 삼키지 않는다 — 막는 것은 `InputManager` 키보드 포커스다. 콘솔을 닫은 Esc 는 뗄 때까지 게임에 보이지 않는다
 *          (다음 프레임 게임의 "일시정지" 로 새지 않는다).
 */
SW_TEST_CASE( DevConsoleControllerTest, ClosedConsoleLeavesKeysToGame )
{
    test::ScopedLogSuppressor suppressor;
    DevConsoleRig             rig;
    SW_ASSERT_TRUE( rig.initialize() );
    InputMap& gameMap = rig._input.getInputMap();
    gameMap.bind( "Forward", Key::W, ActionTrigger::Down );
    gameMap.bind( "Pause", Key::Escape, ActionTrigger::Pressed );

    rig.runFrame( { RawInputEvent::makeKeyDown( Key::W ) } );
    SW_EXPECT_TRUE( rig._input.isKeyDown( Key::W ) );
    SW_EXPECT_TRUE( gameMap.isActionDown( "Forward" ) );

    rig.tapKey( Key::Grave, "`" );
    SW_ASSERT_TRUE( rig._controller.isOpen() );
    SW_EXPECT_FALSE( rig._input.isKeyDown( Key::W ) );
    SW_EXPECT_FALSE( gameMap.isActionDown( "Forward" ) );
    SW_EXPECT_TRUE( rig._input.getKeyboard()->isKeyDown( Key::W ) ); // 장치 상태는 그대로 갱신된다

    rig.runFrame( { RawInputEvent::makeKeyDown( Key::D ), RawInputEvent::makeTextInput( "d" ) } );
    SW_EXPECT_FALSE( rig._input.isKeyDown( Key::D ) );
    SW_EXPECT_FALSE( rig._input.wasKeyPressed( Key::D ) );
    SW_EXPECT_STREQ( "d", rig._controller.getInputLine().c_str() );
    rig.runFrame( { RawInputEvent::makeKeyUp( Key::D ) } );

    rig.runFrame( { RawInputEvent::makeKeyDown( Key::Escape ) } );
    SW_ASSERT_FALSE( rig._controller.isOpen() );
    rig.runFrame( {} );
    SW_EXPECT_TRUE( rig._input.isKeyDown( Key::W ) ); // 열기 전부터 눌려 있던 키는 다시 보인다
    SW_EXPECT_TRUE( gameMap.isActionDown( "Forward" ) );
    SW_EXPECT_FALSE( rig._input.isKeyDown( Key::Escape ) ); // 콘솔을 닫은 Esc 는 뗄 때까지 가린다
    SW_EXPECT_FALSE( gameMap.wasActionTriggered( "Pause" ) );
    rig.runFrame( { RawInputEvent::makeKeyUp( Key::Escape ) } );
    SW_EXPECT_FALSE( rig._input.wasKeyReleased( Key::Escape ) ); // 누른 적을 못 본 키의 뗌도 보이지 않는다
    SW_EXPECT_FALSE( gameMap.wasActionTriggered( "Pause" ) );

    rig.runFrame( { RawInputEvent::makeKeyDown( Key::Escape ) } ); // 닫힌 뒤 새로 누른 Esc 는 게임 것이다
    SW_EXPECT_TRUE( rig._input.wasKeyPressed( Key::Escape ) );
    SW_EXPECT_TRUE( gameMap.wasActionTriggered( "Pause" ) );
    SW_EXPECT_FALSE( rig._controller.isOpen() );
}

/**
 * @brief [DevConsoleControllerTest] 셸 맵에서 여는 키를 바꾸면 콘솔이 그 키를 따른다 — 키는 코드가 아니라 데이터다
 */
SW_TEST_CASE( DevConsoleControllerTest, RebindingMovesTheToggle )
{
    test::ScopedLogSuppressor suppressor;
    DevConsoleRig             rig;
    SW_ASSERT_TRUE( rig.initialize() );
    SW_ASSERT_TRUE( rig._pShellMap->rebindKey( InputMapDefaults::kDevConsoleToggleAction, Key::F2, 0 ) );

    rig.tapKey( Key::Grave, "`" );
    SW_EXPECT_FALSE( rig._controller.isOpen() );
    rig.tapKey( Key::F2 );
    SW_EXPECT_TRUE( rig._controller.isOpen() );
    rig.tapKey( Key::F2 );
    SW_EXPECT_FALSE( rig._controller.isOpen() );
}

/**
 * @brief [DevConsoleControllerTest] 패드만으로 열고, 마지막 명령을 기록에서 꺼내 다시 실행하고, 닫는다(Back → ↑ → A → B)
 */
SW_TEST_CASE( DevConsoleControllerTest, GamepadSubmitsHistory )
{
    test::ScopedLogSuppressor suppressor;
    DevConsoleRig             rig;
    SW_ASSERT_TRUE( rig.initialize() );
    (void)rig._controller.getConsole().submit( "gv_padProbe" );
    const size_t outputCount = rig._controller.getConsole().getOutput().size();

    rig.tapButton( GamepadButton::Back );
    SW_ASSERT_TRUE( rig._controller.isOpen() );
    rig.tapButton( GamepadButton::DPadUp );
    SW_EXPECT_STREQ( "gv_padProbe", rig._controller.getInputLine().c_str() );
    rig.tapButton( GamepadButton::A );
    SW_EXPECT_TRUE( rig._controller.getInputLine().empty() );
    const vector<DevConsoleLine>& listOutput = rig._controller.getConsole().getOutput();
    SW_ASSERT_TRUE( listOutput.size() > outputCount );
    SW_EXPECT_STREQ( "> gv_padProbe", listOutput[outputCount]._text.c_str() );
    rig.tapButton( GamepadButton::B );
    SW_EXPECT_FALSE( rig._controller.isOpen() );
}

/**
 * @brief [DevConsoleControllerTest] BMP 밖 글자(이모지 · 확장 한자)도 입력 줄에 한 글자로 들어가고, 지우기 한 번에 통째로 지워진다
 * @details Win32 는 서로게이트 `WM_CHAR` 둘을 `InputManager` 가 합쳐 UTF-8 네 바이트 한 글자로 보낸다(`InputManagerTest.WmCharSurrogatePairBecomesOneUtf8Character`).
 */
SW_TEST_CASE( DevConsoleControllerTest, SupplementaryCharactersAreTyped )
{
    test::ScopedLogSuppressor suppressor;
    DevConsoleRig             rig;
    SW_ASSERT_TRUE( rig.initialize() );
    rig.tapKey( Key::Grave, "`" );
    SW_ASSERT_TRUE( rig._controller.isOpen() );

    rig.runFrame( { RawInputEvent::makeTextInput( "a\xF0\x9F\x98\x80" ), RawInputEvent::makeTextInput( "\xF0\xA0\x80\x8B" ) } ); // U+1F600 · U+2000B
    SW_EXPECT_STREQ( "a\xF0\x9F\x98\x80\xF0\xA0\x80\x8B", rig._controller.getInputLine().c_str() );
    rig.tapKey( Key::Backspace );
    SW_EXPECT_STREQ( "a\xF0\x9F\x98\x80", rig._controller.getInputLine().c_str() );
    rig.tapKey( Key::Backspace );
    SW_EXPECT_STREQ( "a", rig._controller.getInputLine().c_str() );
}

    #if defined( SW_PLATFORM_WINDOWS )
/**
 * @brief [DevConsoleControllerTest] 창 메시지(WM_KEYDOWN · 서로게이트 WM_CHAR 쌍)에서 콘솔 입력 줄까지 — 플랫폼 키코드는 입력 관리자 한 곳에서만 푼다
 */
SW_TEST_CASE( DevConsoleControllerTest, NativeMessagesReachTheConsoleThroughTheInputManager )
{
    test::ScopedLogSuppressor suppressor;
    DevConsoleRig             rig;
    SW_ASSERT_TRUE( rig.initialize() );

    const auto sendMessage = [&rig]( uint32 message, WPARAM wParam )
    {
        NativeWindowEvent event{};
        event._message = message;
        event._wParam  = wParam;
        rig._input.processNativeEvent( event );
    };
    sendMessage( WM_KEYDOWN, VK_OEM_3 );
    sendMessage( WM_CHAR, '`' );
    rig.runFrame( {} );
    sendMessage( WM_KEYUP, VK_OEM_3 );
    rig.runFrame( {} );
    SW_ASSERT_TRUE( rig._controller.isOpen() );

    sendMessage( WM_CHAR, 0xD83D );
    sendMessage( WM_CHAR, 0xDE00 );
    sendMessage( WM_CHAR, 0x4E2D ); // '中'
    rig.runFrame( {} );
    SW_EXPECT_STREQ( "\xF0\x9F\x98\x80\xE4\xB8\xAD", rig._controller.getInputLine().c_str() );
}
    #endif

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
