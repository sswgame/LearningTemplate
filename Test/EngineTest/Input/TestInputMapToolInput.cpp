#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Input/ShellInputActions.h"

#include "TestFramework/TestFramework.h"

// InputMap — 도구(에디터)도 게임과 같은 입력 맵 하나로 판정하려고 더한 규칙: 수정 키 정확 일치, 클릭 · 끌기 트리거, 맵 더하기, 도구 키보드 포커스.

namespace
{
    struct TestInputMapToolInputInternal
    {
        /** @brief 사건을 넣고 입력 매니저 한 프레임을 시작합니다(사건을 적용하고 매니저의 맵을 갱신한다). */
        static void runInputFrame( sw::InputManager& input, std::initializer_list<sw::RawInputEvent> listEvent )
        {
            for ( const sw::RawInputEvent& rawEvent : listEvent )
            {
                (void)input.postRawEvent( rawEvent ); // 큐가 가득 찰 만큼 넣지 않는다
            }
            input.beginFrame( 0.016f );
        }

        /** @brief 임시 폴더에 InputMap XML 을 쓰고 그 리소스 id 를 @p outResourceID 에 돌려줍니다. 쓰지 못하면 false 입니다. */
        [[nodiscard]] static bool writeTempInputMap( sw::string_view fileName, sw::string_view text, sw::string& outResourceID )
        {
            const sw::string path = test::makeTempPath( fileName );
            if ( sw::FileUtil::writeTextFile( path, text ) == false )
                return false;
            outResourceID = test::makeTempResourceID( path );
            return true;
        }
    };
} // namespace

/**
 * @brief [InputMapToolInputTest] 단축키는 수정 키가 정확히 그 조합일 때만 발화한다 — Ctrl+Shift+Z 가 Ctrl+Z 를 함께 발화시키지 않는다
 * @details 요구한 수정 키만 보면 Redo 를 누를 때 Undo 도 돌아 한 번에 두 번 편집이 바뀐다(에디터 명령 표가 이 겹침을 손으로 막아 왔다).
 *          왼쪽 · 오른쪽 수정 키는 같은 수정 키다.
 */
SW_TEST_CASE( InputMapToolInputTest, ShortcutFiresOnlyOnExactModifiers )
{
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    sw::InputMap& inputMap = input.getInputMap();
    inputMap.bindShortcut( "Undo", sw::Key::Z, sw::ModifierKey::Ctrl );
    inputMap.bindShortcut( "Redo", sw::Key::Z, sw::ModifierKey::Ctrl | sw::ModifierKey::Shift );

    TestInputMapToolInputInternal::runInputFrame( input, { sw::RawInputEvent::makeKeyDown( sw::Key::LeftControl ), sw::RawInputEvent::makeKeyDown( sw::Key::LeftShift ),
                                                           sw::RawInputEvent::makeKeyDown( sw::Key::Z, 0, false, sw::ModifierKey::Ctrl | sw::ModifierKey::Shift ) } );
    SW_EXPECT_TRUE( inputMap.wasActionTriggered( "Redo" ) );
    SW_EXPECT_FALSE( inputMap.wasActionTriggered( "Undo" ) );
    input.endFrame();

    TestInputMapToolInputInternal::runInputFrame( input, { sw::RawInputEvent::makeKeyUp( sw::Key::Z ), sw::RawInputEvent::makeKeyUp( sw::Key::LeftShift ), sw::RawInputEvent::makeKeyUp( sw::Key::LeftControl ) } );
    input.endFrame();

    TestInputMapToolInputInternal::runInputFrame( input, { sw::RawInputEvent::makeKeyDown( sw::Key::RightControl ), sw::RawInputEvent::makeKeyDown( sw::Key::Z, 0, false, sw::ModifierKey::Ctrl ) } );
    SW_EXPECT_TRUE( inputMap.wasActionTriggered( "Undo" ) );
    SW_EXPECT_FALSE( inputMap.wasActionTriggered( "Redo" ) );

    input.shutdown();
}

/**
 * @brief [InputMapToolInputTest] `Click` 은 끌기 문턱 안에서 뗀 순간, `Drag` 는 문턱을 처음 넘은 프레임에 한 번 발화한다
 * @details 같은 오른쪽 단추가 끌면 화면 이동, 끌지 않고 떼면 메뉴다(에디터 뷰포트 · 2D 캔버스, RTS 의 선택 상자와 클릭). 끈 누름은 뗄 때 `Click` 이 아니다.
 */
SW_TEST_CASE( InputMapToolInputTest, ClickAndDragSplitOnDragThreshold )
{
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    sw::InputMap& inputMap = input.getInputMap();
    inputMap.setDragThreshold( 6.0f );
    inputMap.bind( "ContextMenu", sw::MouseButton::Right, sw::ActionTrigger::Click );
    inputMap.bind( "Pan", sw::MouseButton::Right, sw::ActionTrigger::Drag );

    TestInputMapToolInputInternal::runInputFrame( input, { sw::RawInputEvent::makeMouseMove( 100, 100 ) } );
    input.endFrame();

    // 문턱 안(3 px)에서 뗐다 — 메뉴만
    TestInputMapToolInputInternal::runInputFrame( input, { sw::RawInputEvent::makeMouseButtonDown( sw::MouseButton::Right, 100, 100 ) } );
    input.endFrame();
    TestInputMapToolInputInternal::runInputFrame( input, { sw::RawInputEvent::makeMouseMove( 103, 100 ) } );
    SW_EXPECT_FALSE( inputMap.wasActionTriggered( "Pan" ) );
    input.endFrame();
    TestInputMapToolInputInternal::runInputFrame( input, { sw::RawInputEvent::makeMouseButtonUp( sw::MouseButton::Right, 103, 100 ) } );
    SW_EXPECT_TRUE( inputMap.wasActionTriggered( "ContextMenu" ) );
    SW_EXPECT_FALSE( inputMap.wasActionTriggered( "Pan" ) );
    input.endFrame();

    // 문턱을 넘겨 끌었다 — 넘은 프레임에 끌기 한 번, 뗄 때 메뉴 없음
    TestInputMapToolInputInternal::runInputFrame( input, { sw::RawInputEvent::makeMouseButtonDown( sw::MouseButton::Right, 103, 100 ) } );
    input.endFrame();
    TestInputMapToolInputInternal::runInputFrame( input, { sw::RawInputEvent::makeMouseMove( 120, 100 ) } );
    SW_EXPECT_TRUE( inputMap.wasActionTriggered( "Pan" ) );
    input.endFrame();
    TestInputMapToolInputInternal::runInputFrame( input, { sw::RawInputEvent::makeMouseMove( 140, 100 ) } );
    SW_EXPECT_FALSE( inputMap.wasActionTriggered( "Pan" ) );
    SW_EXPECT_TRUE( inputMap.isActionDown( "Pan" ) );
    input.endFrame();
    TestInputMapToolInputInternal::runInputFrame( input, { sw::RawInputEvent::makeMouseButtonUp( sw::MouseButton::Right, 140, 100 ) } );
    SW_EXPECT_FALSE( inputMap.wasActionTriggered( "ContextMenu" ) );
    input.endFrame();

    input.shutdown();
}

/**
 * @brief [InputMapToolInputTest] `modifier` 는 수정 키 이름을 '+' 로 이은 단축키이고, `dragThreshold` 와 함께 저장 · 다시 읽기에서 그대로 남는다
 * @details 예전에는 `modifier` 가 왼쪽 수정 키 하나와의 두 키 조합(Chord)이라 오른쪽 Ctrl 이 듣지 않았고 Ctrl+Shift 를 적을 수 없었다. 모르는 이름은 다른 종류로 읽지 않고 실패한다.
 */
SW_TEST_CASE( InputMapToolInputTest, ModifierAttributeIsAShortcutAndRoundTrips )
{
    sw::string mapID;
    SW_ASSERT_TRUE( TestInputMapToolInputInternal::writeTempInputMap( "shortcut.input.xml",
                                                                      "<InputMap defaultLayer=\"Tool\" dragThreshold=\"9\">"
                                                                      "<layer name=\"Tool\"><action name=\"Palette\"><bind source=\"key\" modifier=\"Ctrl+Shift\" code=\"P\"/></action></layer>"
                                                                      "</InputMap>",
                                                                      mapID ) );
    sw::InputMap inputMap;
    SW_ASSERT_TRUE( inputMap.loadFromResource( mapID ) );
    const sw::ActionBinding* pBinding = inputMap.getBinding( "Palette", 0 );
    SW_ASSERT_TRUE( pBinding != nullptr );
    SW_EXPECT_TRUE( pBinding->_kind == sw::BindingKind::Shortcut );
    SW_EXPECT_EQUAL( static_cast<int32>( sw::ModifierKey::Ctrl | sw::ModifierKey::Shift ), static_cast<int32>( pBinding->_modifierMask ) );
    SW_EXPECT_NEAR_EQUAL( 9.0f, inputMap.getDragThreshold(), 1.0e-5f );

    const sw::string savedPath = test::makeTempPath( "shortcut.saved.input.xml" );
    SW_ASSERT_TRUE( inputMap.saveToResource( savedPath ) );
    sw::InputMap reloaded;
    SW_ASSERT_TRUE( reloaded.loadFromResource( test::makeTempResourceID( savedPath ) ) );
    const sw::ActionBinding* pReloaded = reloaded.getBinding( "Palette", 0 );
    SW_ASSERT_TRUE( pReloaded != nullptr );
    SW_EXPECT_TRUE( pReloaded->_kind == sw::BindingKind::Shortcut );
    SW_EXPECT_EQUAL( static_cast<int32>( pBinding->_modifierMask ), static_cast<int32>( pReloaded->_modifierMask ) );
    SW_EXPECT_NEAR_EQUAL( 9.0f, reloaded.getDragThreshold(), 1.0e-5f );

    sw::string badID;
    SW_ASSERT_TRUE( TestInputMapToolInputInternal::writeTempInputMap( "badmodifier.input.xml",
                                                                      "<InputMap><action name=\"Save\"><bind source=\"key\" modifier=\"Control\" code=\"S\"/></action></InputMap>", badID ) );
    sw::InputMap badMap;
    SW_EXPECT_FALSE( badMap.loadFromResource( badID ) );
}

/**
 * @brief [InputMapToolInputTest] `appendFromResource` 는 새 레이어 · 액션만 더하고, 맵 전체 값 · 있는 액션 · 같은 레이어의 같은 입력은 거절한다
 * @details 확장 모듈이 에디터 맵에 자기 단축키를 더하는 길이다. 두 파일이 같은 이름이나 같은 조합을 쓰면 한 번 눌러 둘이 돈다.
 */
SW_TEST_CASE( InputMapToolInputTest, AppendAddsActionsAndRejectsCollisions )
{
    using Internal = TestInputMapToolInputInternal;
    sw::string baseID;
    SW_ASSERT_TRUE( Internal::writeTempInputMap( "base.input.xml",
                                                 "<InputMap defaultLayer=\"Global\"><layers><layer name=\"Global\" alwaysOn=\"1\"/></layers>"
                                                 "<layer name=\"Global\"><action name=\"Save\"><bind source=\"key\" modifier=\"Ctrl\" code=\"S\"/></action></layer>"
                                                 "</InputMap>",
                                                 baseID ) );
    sw::InputMap inputMap;
    SW_ASSERT_TRUE( inputMap.loadFromResource( baseID ) );

    sw::string addID;
    SW_ASSERT_TRUE( Internal::writeTempInputMap( "add.input.xml",
                                                 "<InputMap><layers><layer name=\"Park\" priority=\"5\"/></layers>"
                                                 "<layer name=\"Park\"><action name=\"Park.Reload\"><bind source=\"key\" modifier=\"Ctrl\" code=\"S\"/></action></layer>"
                                                 "<layer name=\"Global\"><action name=\"Park.Open\"><bind source=\"key\" modifier=\"Ctrl+Alt\" code=\"P\"/></action></layer>"
                                                 "</InputMap>",
                                                 addID ) );
    SW_ASSERT_TRUE( inputMap.appendFromResource( addID ) );
    SW_EXPECT_TRUE( inputMap.hasAction( "Save" ) );
    SW_EXPECT_TRUE( inputMap.hasAction( "Park.Reload" ) ); // 다른 레이어의 같은 Ctrl+S 는 된다(패널마다 같은 키)
    SW_EXPECT_TRUE( inputMap.hasAction( "Park.Open" ) );
    SW_EXPECT_EQUAL( 5, inputMap.getLayerPriority( "Park" ) );

    sw::string sameActionID;
    SW_ASSERT_TRUE( Internal::writeTempInputMap( "sameaction.input.xml", "<InputMap><action name=\"Save\" layer=\"Global\"><bind source=\"key\" code=\"F2\"/></action></InputMap>", sameActionID ) );
    SW_EXPECT_FALSE( inputMap.appendFromResource( sameActionID ) );

    sw::string mapWideID;
    SW_ASSERT_TRUE( Internal::writeTempInputMap( "mapwide.input.xml", "<InputMap doubleClickTime=\"0.5\"><action name=\"Other\" layer=\"Global\"><bind source=\"key\" code=\"F3\"/></action></InputMap>",
                                                 mapWideID ) );
    SW_EXPECT_FALSE( inputMap.appendFromResource( mapWideID ) );

    sw::string relayerID;
    SW_ASSERT_TRUE( Internal::writeTempInputMap( "relayer.input.xml", "<InputMap><layers><layer name=\"Global\" priority=\"9\"/></layers></InputMap>", relayerID ) );
    SW_EXPECT_FALSE( inputMap.appendFromResource( relayerID ) );

    sw::string sameInputID;
    SW_ASSERT_TRUE( Internal::writeTempInputMap( "sameinput.input.xml",
                                                 "<InputMap><layer name=\"Global\"><action name=\"SaveAgain\"><bind source=\"key\" modifier=\"Ctrl\" code=\"S\"/></action></layer></InputMap>",
                                                 sameInputID ) );
    SW_EXPECT_FALSE( inputMap.appendFromResource( sameInputID ) );
}

/**
 * @brief [InputMapToolInputTest] 키보드 포커스가 도구(`Tool`)면 게임 조회는 키를 못 보고, 포커스를 무시하는 맵(도구의 맵)은 본다
 * @details 떼어 낸 에디터 창의 키도 같은 입력 매니저에 넣어 에디터 맵이 판정하되, 그동안 게임은 그 키를 보지 않아야 한다(Ctrl+S 가 게임의 S 로 새지 않게).
 */
SW_TEST_CASE( InputMapToolInputTest, ToolFocusHidesKeysFromGameButNotFromToolMap )
{
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    sw::InputMap toolMap;
    toolMap.setInputManager( &input );
    toolMap.setKeyboardFocusIgnored( true );
    toolMap.setToolKeyboardReader( true );
    toolMap.bind( "ToolSave", sw::Key::S );
    // 셸 · UI 맵처럼 포커스를 무시하지만 도구 맵은 아닌 맵 — 도구 창에 친 글자가 콘솔 여는 키가 되지 않아야 한다.
    sw::InputMap shellMap;
    shellMap.setInputManager( &input );
    shellMap.setKeyboardFocusIgnored( true );
    shellMap.bind( "ShellSave", sw::Key::S );
    input.getInputMap().bind( "GameSave", sw::Key::S );

    input.setKeyboardFocus( sw::InputKeyboardFocus::Tool );
    TestInputMapToolInputInternal::runInputFrame( input, { sw::RawInputEvent::makeKeyDown( sw::Key::S ) } );
    toolMap.update( 0.016f );
    shellMap.update( 0.016f );
    SW_EXPECT_FALSE( input.isKeyDown( sw::Key::S ) );
    SW_EXPECT_TRUE( input.getKeyboard()->isKeyDown( sw::Key::S ) );
    SW_EXPECT_FALSE( input.getInputMap().wasActionTriggered( "GameSave" ) );
    SW_EXPECT_FALSE( shellMap.wasActionTriggered( "ShellSave" ) );
    SW_EXPECT_TRUE( toolMap.wasActionTriggered( "ToolSave" ) );

    shellMap.setInputManager( nullptr );
    toolMap.setInputManager( nullptr );
    input.shutdown();
}

/**
 * @brief [InputMapToolInputTest] 키 + 수정 키 재배치는 종류(단일 키 · 단축키)를 함께 바꾸고, 같은 레이어 겹침을 찾고, 사용자 재배치 파일에 수정 키까지 남는다
 * @details 방아쇠 키만 바꾸는 재배치(`rebindSlot`)로는 Ctrl+D 를 Ctrl+Shift+D 로 옮길 수 없다. 에디터 단축키 창과 셸 맵이 같은 파일 형식을 쓴다.
 */
SW_TEST_CASE( InputMapToolInputTest, KeyShortcutRebindKeepsModifiersThroughUserFile )
{
    sw::InputMap inputMap;
    inputMap.bindShortcut( "Duplicate", sw::Key::D, sw::ModifierKey::Ctrl, sw::ActionTrigger::Pressed, "Editor.Global" );
    inputMap.bind( "Delete", sw::Key::Delete, sw::ActionTrigger::Pressed, "Editor.Global" );

    SW_ASSERT_TRUE( inputMap.rebindKeyShortcut( "Duplicate", 0, sw::Key::D, sw::ModifierKey::Ctrl | sw::ModifierKey::Shift ) );
    SW_EXPECT_TRUE( inputMap.getBinding( "Duplicate", 0 )->_kind == sw::BindingKind::Shortcut );
    SW_ASSERT_TRUE( inputMap.rebindKeyShortcut( "Delete", 0, sw::Key::X, sw::ModifierKey::None ) );
    SW_EXPECT_TRUE( inputMap.getBinding( "Delete", 0 )->_kind == sw::BindingKind::SingleSlot );

    sw::hashed_string other{};
    SW_EXPECT_FALSE( inputMap.findSameLayerInputConflict( "Duplicate", 0, other ) );
    SW_ASSERT_TRUE( inputMap.rebindKeyShortcut( "Delete", 0, sw::Key::D, sw::ModifierKey::Ctrl | sw::ModifierKey::Shift ) );
    SW_EXPECT_TRUE( inputMap.findSameLayerInputConflict( "Delete", 0, other ) );
    SW_EXPECT_TRUE( other == sw::hashed_string( "Duplicate" ) );
    SW_ASSERT_TRUE( inputMap.rebindKeyShortcut( "Delete", 0, sw::Key::X, sw::ModifierKey::Alt ) );

    const sw::string path = test::makeTempPath( "toolbindings.xml" );
    SW_ASSERT_TRUE( sw::ShellInputActions::saveUserBindings( inputMap, path ) );

    sw::InputMap reloaded;
    reloaded.bindShortcut( "Duplicate", sw::Key::D, sw::ModifierKey::Ctrl, sw::ActionTrigger::Pressed, "Editor.Global" );
    reloaded.bind( "Delete", sw::Key::Delete, sw::ActionTrigger::Pressed, "Editor.Global" );
    SW_ASSERT_TRUE( sw::ShellInputActions::loadUserBindings( reloaded, path ) );
    SW_EXPECT_EQUAL( static_cast<int32>( sw::ModifierKey::Ctrl | sw::ModifierKey::Shift ), static_cast<int32>( reloaded.getBinding( "Duplicate", 0 )->_modifierMask ) );
    SW_EXPECT_EQUAL( static_cast<int32>( sw::ModifierKey::Alt ), static_cast<int32>( reloaded.getBinding( "Delete", 0 )->_modifierMask ) );
    SW_EXPECT_EQUAL( static_cast<uint16>( sw::Key::X ), reloaded.getBinding( "Delete", 0 )->_arrSlot[0]._controlIndex );

    // 기본으로 되돌리면 파일이 지워진다(남은 차이가 없다).
    reloaded.resetAllBindingsToDefault();
    SW_ASSERT_TRUE( sw::ShellInputActions::saveUserBindings( reloaded, path ) );
    SW_EXPECT_FALSE( sw::FileUtil::exists( path ) );

    uint8 modifierMask = 0;
    SW_EXPECT_TRUE( sw::InputMap::tryParseModifierNames( "Ctrl+Alt", modifierMask ) );
    SW_EXPECT_STREQ( "Ctrl+Alt", sw::InputMap::formatModifierNames( modifierMask ).c_str() );
    SW_EXPECT_FALSE( sw::InputMap::tryParseModifierNames( "Ctrl+Ctrl", modifierMask ) );
}
