/**
 * @file TestEditorShortcutMap.cpp
 * @brief 단축키 Model(`EditorShortcutMap`)의 발화 판정을 봅니다 — 포커스 패널 레이어의 명령이 같은 누름의 전역 명령보다 앞선다. ImGui · 에디터 없이 지역 입력 매니저로 돈다.
 */
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Input/EditorInputActionNames.h"
#include "Editor/Common/Input/Model/EditorShortcutMap.h"

#include "Engine/Input/InputManager.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct EditorShortcutMapTestInternal
    {
        /** @brief 입력 액션 @p pAction 을 부르는 명령 @p pID 입니다. */
        static EditorCommandDesc makeCommand( const utf8* pID, const utf8* pAction )
        {
            EditorCommandDesc desc;
            desc._id          = pID;
            desc._inputAction = pAction;
            return desc;
        }

        /** @brief Delete 를 한 번 눌러 맵을 갱신하고, 판정한 명령 id 를 돌려줍니다. */
        static vector<string> pressDelete( InputManager& input, EditorShortcutMap& shortcuts, const vector<EditorCommandDesc>& listCommand )
        {
            SW_EXPECT_TRUE( input.postRawEvent( RawInputEvent::makeKeyDown( Key::Delete ) ) );
            input.beginFrame( 0.016f );
            shortcuts.update( 0.016f );
            vector<string> listCommandID;
            shortcuts.collectTriggeredCommands( listCommand, listCommandID );
            input.endFrame();
            SW_EXPECT_TRUE( input.postRawEvent( RawInputEvent::makeKeyUp( Key::Delete ) ) );
            input.beginFrame( 0.016f );
            shortcuts.update( 0.016f );
            input.endFrame();
            return listCommandID;
        }
    };
} // namespace

/**
 * @brief [EditorShortcutMapTest] 같은 키가 전역과 포커스 패널 레이어에 있으면 패널 명령만, 패널이 포커스가 아니면 전역 명령만 판정한다
 */
SW_TEST_CASE( EditorShortcutMapTest, FocusedPanelCommandWinsOverGlobal )
{
    using Internal = EditorShortcutMapTestInternal;

    InputManager input;
    SW_REQUIRE_TRUE( input.initialize() );

    EditorShortcutMap                       shortcuts;
    InputMap&                               map = shortcuts.getInputMap();
    const hashed_string                     globalLayer{ EditorShortcutMap::kGlobalLayer };
    fixed_string<constant::buffer::kMax256> layerName;
    formatstring( layerName.data(), layerName.capacity(), "%#hierarchy", EditorShortcutMap::kPanelLayerPrefix );
    const hashed_string panelLayer{ layerName.c_str() };
    map.setInputManager( &input );
    map.setKeyboardFocusIgnored( true );
    map.registerLayer( globalLayer, 0, true );
    map.registerLayer( panelLayer, 10, false );
    map.createAction( hashed_string( "scene.delete" ) );
    map.bind( hashed_string( "scene.delete" ), Key::Delete, ActionTrigger::Pressed, globalLayer );
    map.createAction( hashed_string( "hierarchy.delete" ) );
    map.bind( hashed_string( "hierarchy.delete" ), Key::Delete, ActionTrigger::Pressed, panelLayer );

    const vector<EditorCommandDesc> listCommand = { Internal::makeCommand( "cmd.globalDelete", "scene.delete" ), Internal::makeCommand( "cmd.panelDelete", "hierarchy.delete" ),
                                                    Internal::makeCommand( "cmd.noShortcut", "" ) };

    shortcuts.setFocusedPanel( "hierarchy" );
    SW_EXPECT_TRUE( shortcuts.getFocusedPanelLayer() == panelLayer );
    const vector<string> listFocused = Internal::pressDelete( input, shortcuts, listCommand );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listFocused.size() );
    SW_EXPECT_TRUE( listFocused[0] == "cmd.panelDelete" );

    shortcuts.setFocusedPanel( "console" ); // 레이어가 없는 패널 — 패널 레이어를 모두 끈다
    SW_EXPECT_TRUE( shortcuts.getFocusedPanelLayer().empty() );
    const vector<string> listGlobal = Internal::pressDelete( input, shortcuts, listCommand );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listGlobal.size() );
    SW_EXPECT_TRUE( listGlobal[0] == "cmd.globalDelete" );

    map.setInputManager( nullptr );
    input.shutdown();
}

SW_TEST_CASE( EditorShortcutMapTest, ActionMismatchesNameTyposAndMissingCodeActions )
{
    // 명령의 액션도 코드 액션도 아닌 줄(오타)과, 입력 맵에 없는 코드 액션을 모은다 — 에디터가 명령 표를 맞출 때 오류로 낸다.
    vector<hashed_string> listMapAction{ hashed_string( "cmd.undo" ), hashed_string( "cmd.undoo" ), hashed_string( editorinput::kViewportFlyForward ) };
    for ( const utf8* pBookmark : editorinput::kArrViewportBookmark )
        listMapAction.push_back( hashed_string( pBookmark ) );
    const vector<EditorCommandDesc> listCommand{ EditorShortcutMapTestInternal::makeCommand( "edit.undo", "cmd.undo" ) };

    vector<string> listUnknown;
    vector<string> listMissing;
    EditorShortcutMap::collectActionMismatches( listMapAction, listCommand, listUnknown, listMissing );
    SW_REQUIRE_TRUE( listUnknown.size() == 1 );
    SW_EXPECT_TRUE( listUnknown[0] == "cmd.undoo" );
    // 맵에 없는 코드 액션: 북마크 저장 아홉 + 코드 액션 가운데 비행 앞으로를 뺀 전부
    const size_t codeActionCount = sizeof( editorinput::kArrCodeAction ) / sizeof( editorinput::kArrCodeAction[0] );
    SW_EXPECT_TRUE( listMissing.size() == codeActionCount - 1 + 9 );
    SW_EXPECT_TRUE( VectorUtil::contains( listMissing, string{ editorinput::kViewportFlyForward } ) == false );
}
