#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Commands/EditorShortcutOverrides.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /** @brief 단축키 하나만 든 시험 커맨드입니다. */
    EditorCommandDesc makeCommand( const utf8* pID, const EditorCommandShortcut& shortcut )
    {
        EditorCommandDesc desc{};
        desc._id       = pID;
        desc._label    = pID;
        desc._shortcut = shortcut;
        return desc;
    }
} // namespace

/**
 * @brief [EditorShortcutOverridesTest] 조합 글 읽기는 라벨 쓰기의 역이다 — 넓힌 키(Comma · 숫자 · Delete)도 읽고 모르는 키는 거절한다
 */
SW_TEST_CASE( EditorShortcutOverridesTest, ParseIsTheInverseOfTheLabel )
{
    EditorCommandShortcut shortcut{};
    SW_ASSERT_TRUE( EditorShortcutOverrides::parseShortcut( "Ctrl+Shift+B", shortcut ) );
    SW_EXPECT_TRUE( shortcut._key == EditorCommandKey::B );
    SW_EXPECT_EQUAL( static_cast<uint8>( commandmodifier::kCtrl | commandmodifier::kShift ), shortcut._modifier );
    SW_EXPECT_STREQ( "Ctrl+Shift+B", EditorShortcutOverrides::formatShortcut( shortcut ).c_str() );
    SW_EXPECT_TRUE( EditorShortcutOverrides::parseShortcut( "Ctrl+Comma", shortcut ) );
    SW_EXPECT_TRUE( shortcut._key == EditorCommandKey::Comma );
    SW_EXPECT_TRUE( EditorShortcutOverrides::parseShortcut( "Alt+0", shortcut ) );
    SW_EXPECT_TRUE( shortcut._key == EditorCommandKey::Num0 );
    SW_EXPECT_TRUE( EditorShortcutOverrides::parseShortcut( "Delete", shortcut ) );
    SW_EXPECT_TRUE( shortcut._key == EditorCommandKey::Delete && shortcut._modifier == commandmodifier::kNone );
    SW_EXPECT_TRUE( EditorShortcutOverrides::parseShortcut( "", shortcut ) );
    SW_EXPECT_TRUE( shortcut._key == EditorCommandKey::None );
    SW_EXPECT_FALSE( EditorShortcutOverrides::parseShortcut( "Ctrl+Banana", shortcut ) );
    SW_EXPECT_FALSE( EditorShortcutOverrides::parseShortcut( "Hyper+A", shortcut ) );
}

/**
 * @brief [EditorShortcutOverridesTest] 입히기는 덮어쓴 커맨드만 바꾸고, 기본 조합은 _defaultShortcut 에 남는다
 */
SW_TEST_CASE( EditorShortcutOverridesTest, ApplyChangesOnlyOverriddenCommands )
{
    EditorCommandRegistry registry;
    registry.registerCommand( makeCommand( "edit.undo", { EditorCommandKey::Z, commandmodifier::kCtrl } ) );
    registry.registerCommand( makeCommand( "edit.redo", { EditorCommandKey::Y, commandmodifier::kCtrl } ) );
    EditorShortcutOverrides overrides;
    overrides.setOverride( "edit.undo", { EditorCommandKey::U, commandmodifier::kCtrl }, {}, { EditorCommandKey::Z, commandmodifier::kCtrl }, {} );
    SW_EXPECT_EQUAL( 1u, overrides.applyTo( registry ) );
    SW_EXPECT_TRUE( registry.find( "edit.undo" )->_shortcut._key == EditorCommandKey::U );
    SW_EXPECT_TRUE( registry.find( "edit.undo" )->_defaultShortcut._key == EditorCommandKey::Z );
    SW_EXPECT_TRUE( registry.find( "edit.redo" )->_shortcut._key == EditorCommandKey::Y );
}

/**
 * @brief [EditorShortcutOverridesTest] 기본과 같은 조합을 두면 덮어쓰기가 지워진다(파일에는 바뀐 커맨드만)
 */
SW_TEST_CASE( EditorShortcutOverridesTest, SettingTheDefaultRemovesTheOverride )
{
    EditorShortcutOverrides     overrides;
    const EditorCommandShortcut kDefault{ EditorCommandKey::Z, commandmodifier::kCtrl };
    overrides.setOverride( "edit.undo", { EditorCommandKey::U, commandmodifier::kCtrl }, {}, kDefault, {} );
    SW_EXPECT_EQUAL( size_t{ 1 }, overrides.getOverrides().size() );
    overrides.setOverride( "edit.undo", kDefault, {}, kDefault, {} );
    SW_EXPECT_TRUE( overrides.getOverrides().empty() );
}

/**
 * @brief [EditorShortcutOverridesTest] 덮어쓰기로 생긴 충돌은 입힌 뒤 validate 가 적고, 파일 왕복은 조합을 그대로 돌려준다
 */
SW_TEST_CASE( EditorShortcutOverridesTest, ConflictIsReportedByValidateAndFileRoundTrips )
{
    EditorCommandRegistry registry;
    registry.registerCommand( makeCommand( "edit.undo", { EditorCommandKey::Z, commandmodifier::kCtrl } ) );
    registry.registerCommand( makeCommand( "edit.redo", { EditorCommandKey::Y, commandmodifier::kCtrl } ) );
    EditorShortcutOverrides overrides;
    overrides.setOverride( "edit.redo", { EditorCommandKey::Z, commandmodifier::kCtrl }, {}, { EditorCommandKey::Y, commandmodifier::kCtrl }, {} );
    (void)overrides.applyTo( registry ); // 수는 위 시험이 본다
    string report;
    SW_EXPECT_FALSE( registry.validate( report ) );

    const string filePath = FileUtil::joinPath( test::makeTempDirectory( "editor_shortcut_overrides" ), EditorShortcutOverrides::kFileName );
    SW_ASSERT_TRUE( overrides.saveToFile( filePath ) );
    EditorShortcutOverrides loaded;
    SW_ASSERT_TRUE( loaded.loadFromFile( filePath ) );
    SW_ASSERT_EQUAL( size_t{ 1 }, loaded.getOverrides().size() );
    SW_EXPECT_TRUE( loaded.getOverrides()[0]._commandID == "edit.redo" );
    SW_EXPECT_TRUE( loaded.getOverrides()[0]._shortcut._key == EditorCommandKey::Z );
}
