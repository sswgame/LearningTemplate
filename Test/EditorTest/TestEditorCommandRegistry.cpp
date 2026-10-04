#include "pch.h"

#include "Core/Container/string.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /** @brief 커맨드가 실제로 불렸는지 세는 카운터 (테스트 전용 전역) */
    int32 s_invokeCount = 0;
    bool  s_bEnabled    = true;

    void bumpInvokeCount()
    {
        ++s_invokeCount;
    }

    bool isEnabledProbe()
    {
        return s_bEnabled;
    }

    /** @brief id 와 동작만 채운 최소 커맨드를 만듭니다. */
    EditorCommandDesc makeCommand( const utf8* pId )
    {
        EditorCommandDesc desc{};
        desc._id     = pId;
        desc._label  = pId;
        desc._action = &bumpInvokeCount;
        return desc;
    }

    /** @brief 메뉴 경로 · 순서 칸까지 채운 커맨드를 만듭니다. */
    EditorCommandDesc makeMenuCommand( const utf8* pId, const utf8* pMenuPath, int32 menuOrder )
    {
        EditorCommandDesc desc = makeCommand( pId );
        desc._menuPath         = pMenuPath;
        desc._menuOrder        = menuOrder;
        return desc;
    }

    /** @brief 메뉴 항목을 `id,id,-,id` 로 이은 글입니다(`-` 는 구분선). */
    string joinMenuItems( const EditorCommandRegistry& registry, const EditorMenu& menu )
    {
        string text;
        for ( const EditorMenuItem& item : menu._listItem )
        {
            if ( text.empty() == false )
                text += ',';
            if ( item._bSeparatorBefore )
                text += "-,";
            text += registry.getCommands()[item._commandIndex]._id;
        }
        return text;
    }
} // namespace

/**
 * @brief [EditorCommandRegistryTest] 단축키 라벨이 Ctrl→Shift→Alt 순서로, 보조 조합은 " / " 로 이어붙는지 검증
 */
SW_TEST_CASE( EditorCommandRegistryTest, ShortcutLabelUsesFixedModifierOrder )
{
    fixed_string<constant::kMaxBuffer64> label;

    EditorCommandDesc single{};
    single._shortcut = EditorCommandShortcut{ EditorCommandKey::B, commandmodifier::kCtrl | commandmodifier::kShift };
    EditorCommandRegistry::formatShortcutLabel( single, label );
    SW_EXPECT_STREQ( "Ctrl+Shift+B", label.c_str() );

    EditorCommandDesc ctrlAlt{};
    ctrlAlt._shortcut = EditorCommandShortcut{ EditorCommandKey::F11, commandmodifier::kCtrl | commandmodifier::kAlt };
    // 보조 조합은 수정자 없이 F7 하나다 — 라벨에서 보조 조합이 빠지면 이 키는 메뉴 어디에도 보이지 않는다.
    ctrlAlt._altShortcut = EditorCommandShortcut{ EditorCommandKey::F7, commandmodifier::kNone };
    EditorCommandRegistry::formatShortcutLabel( ctrlAlt, label );
    SW_EXPECT_STREQ( "Ctrl+Alt+F11 / F7", label.c_str() );

    EditorCommandDesc none{};
    EditorCommandRegistry::formatShortcutLabel( none, label );
    SW_EXPECT_TRUE( label.empty() );
}

/**
 * @brief [EditorCommandRegistryTest] 아이콘이 있으면 라벨 앞에 두 칸 띄워 붙고, 없으면 라벨만 나오는지 검증
 */
SW_TEST_CASE( EditorCommandRegistryTest, MenuLabelPrependsIconWhenPresent )
{
    fixed_string<constant::kMaxBuffer128> label;

    EditorCommandDesc withIcon{};
    withIcon._icon  = "@";
    withIcon._label = "Save";
    EditorCommandRegistry::formatMenuLabel( withIcon, label );
    SW_EXPECT_STREQ( "@  Save", label.c_str() );

    EditorCommandDesc withoutIcon{};
    withoutIcon._label = "Save";
    EditorCommandRegistry::formatMenuLabel( withoutIcon, label );
    SW_EXPECT_STREQ( "Save", label.c_str() );
}

/**
 * @brief [EditorCommandRegistryTest] id 로 찾아 실행하고, 없는 id 와 id 가 빈 등록은 거부하는지 검증
 */
SW_TEST_CASE( EditorCommandRegistryTest, ExecuteByIdRunsBoundAction )
{
    EditorCommandRegistry registry;
    s_invokeCount = 0;

    registry.registerCommand( makeCommand( "test.bump" ) );

    EditorCommandDesc noId = makeCommand( "test.dropped" );
    noId._id               = "";
    registry.registerCommand( std::move( noId ) );
    SW_EXPECT_EQUAL( static_cast<size_t>( 1 ), registry.getCommands().size() );

    SW_EXPECT_NOT_NULL( registry.find( "test.bump" ) );
    SW_EXPECT_NULL( registry.find( "test.missing" ) );

    SW_EXPECT_TRUE( registry.execute( "test.bump" ) );
    SW_EXPECT_EQUAL( 1, s_invokeCount );

    SW_EXPECT_FALSE( registry.execute( "test.missing" ) );
    SW_EXPECT_EQUAL( 1, s_invokeCount );
}

/**
 * @brief [EditorCommandRegistryTest] 활성 조건이 거짓이면 실행하지 않는지 검증 (메뉴 회색 처리와 같은 조건)
 */
SW_TEST_CASE( EditorCommandRegistryTest, DisabledCommandIsNotExecuted )
{
    EditorCommandRegistry registry;
    s_invokeCount = 0;

    EditorCommandDesc desc = makeCommand( "test.gated" );
    desc._enabledPredicate = &isEnabledProbe;
    registry.registerCommand( std::move( desc ) );

    s_bEnabled = false;
    SW_EXPECT_FALSE( registry.execute( "test.gated" ) );
    SW_EXPECT_EQUAL( 0, s_invokeCount );

    s_bEnabled = true;
    SW_EXPECT_TRUE( registry.execute( "test.gated" ) );
    SW_EXPECT_EQUAL( 1, s_invokeCount );
}

/**
 * @brief [EditorCommandRegistryTest] 같은 조합을 두 커맨드가 주장하면 validate 가 잡는지 검증
 * @details 같은 조합을 두 처리기가 받으면(전역 처리기와 Inspector 가 각각 Ctrl+Z) 한 번 눌러 두 번 되돌아간다. 정의가 표 한곳에
 *          모여 있으므로 그 충돌을 validate 가 잡는다.
 */
SW_TEST_CASE( EditorCommandRegistryTest, ValidateCatchesDuplicateIdAndChord )
{
    EditorCommandRegistry clean;
    clean.registerCommand( makeCommand( "test.a" ) );

    EditorCommandDesc withChord = makeCommand( "test.b" );
    withChord._shortcut         = EditorCommandShortcut{ EditorCommandKey::Z, commandmodifier::kCtrl };
    clean.registerCommand( std::move( withChord ) );

    string report;
    SW_EXPECT_TRUE( clean.validate( report ) );
    SW_EXPECT_TRUE( report.empty() );

    EditorCommandRegistry duplicateId;
    duplicateId.registerCommand( makeCommand( "test.same" ) );
    duplicateId.registerCommand( makeCommand( "test.same" ) );
    SW_EXPECT_FALSE( duplicateId.validate( report ) );
    SW_EXPECT_FALSE( report.empty() );

    EditorCommandRegistry duplicateChord;
    EditorCommandDesc     undo = makeCommand( "test.undo" );
    undo._shortcut             = EditorCommandShortcut{ EditorCommandKey::Z, commandmodifier::kCtrl };
    duplicateChord.registerCommand( std::move( undo ) );

    // 보조 조합끼리의 충돌도 잡아야 한다 — 주 조합만 보면 Ctrl+Z 를 보조로 든 커맨드를 놓친다.
    EditorCommandDesc redo = makeCommand( "test.redo" );
    redo._shortcut         = EditorCommandShortcut{ EditorCommandKey::Y, commandmodifier::kCtrl };
    redo._altShortcut      = EditorCommandShortcut{ EditorCommandKey::Z, commandmodifier::kCtrl };
    duplicateChord.registerCommand( std::move( redo ) );

    SW_EXPECT_FALSE( duplicateChord.validate( report ) );
    SW_EXPECT_FALSE( report.empty() );
}

/**
 * @brief [EditorCommandRegistryTest] 표시 전용 조합(Alt+F4)은 처리 대상이 아니고 충돌로도 세지 않는지 검증
 */
SW_TEST_CASE( EditorCommandRegistryTest, DisplayOnlyShortcutIsNeverHandled )
{
    const EditorCommandShortcut displayOnly{ EditorCommandKey::F4, commandmodifier::kAlt | commandmodifier::kDisplayOnly };
    const EditorCommandShortcut handled{ EditorCommandKey::F4, commandmodifier::kAlt };

    SW_EXPECT_FALSE( EditorCommandRegistry::isHandledShortcut( displayOnly ) );
    SW_EXPECT_TRUE( EditorCommandRegistry::isHandledShortcut( handled ) );
    // DisplayOnly 비트는 조합 비교에서 무시된다 — 같은 키·같은 수정자이면 같은 조합이다.
    SW_EXPECT_TRUE( EditorCommandRegistry::isSameShortcut( displayOnly, handled ) );

    // 라벨에는 그대로 보여야 한다. OS 가 가로채는 키를 메뉴에 적어 두는 것이 목적이다.
    EditorCommandDesc desc{};
    desc._shortcut = displayOnly;
    fixed_string<constant::kMaxBuffer64> label;
    EditorCommandRegistry::formatShortcutLabel( desc, label );
    SW_EXPECT_STREQ( "Alt+F4", label.c_str() );

    // 표시 전용은 다른 커맨드와 나란히 있어도 중복으로 걸리지 않는다.
    EditorCommandRegistry registry;
    EditorCommandDesc     exitCommand = makeCommand( "test.exit" );
    exitCommand._shortcut             = displayOnly;
    registry.registerCommand( std::move( exitCommand ) );

    EditorCommandDesc other = makeCommand( "test.other" );
    other._shortcut         = displayOnly;
    registry.registerCommand( std::move( other ) );

    string report;
    SW_EXPECT_TRUE( registry.validate( report ) );
}

/**
 * @brief [EditorCommandRegistryTest] 단축키 수정자는 정확히 같아야 하고, Super(Win 키)는 어떤 조합과도 맞지 않는다
 * @details 필요한 수정자만 보면 Ctrl+Shift+Z 가 Ctrl+Z(undo)까지 함께 발동한다. Super 를 Ctrl 로 치면 Win+Z 가 undo 가 된다 —
 *          지원하는 플랫폼(Windows · 리눅스)에 Cmd 키는 없다.
 */
SW_TEST_CASE( EditorCommandRegistryTest, ShortcutModifiersMustMatchExactly )
{
    const EditorCommandShortcut undo{ EditorCommandKey::Z, commandmodifier::kCtrl };
    const EditorCommandShortcut redo{ EditorCommandKey::Z, commandmodifier::kCtrl | commandmodifier::kShift };

    SW_EXPECT_TRUE( EditorCommandRegistry::matchesPressedModifiers( undo, commandmodifier::kCtrl, false ) );
    SW_EXPECT_FALSE( EditorCommandRegistry::matchesPressedModifiers( undo, commandmodifier::kCtrl | commandmodifier::kShift, false ) );
    SW_EXPECT_TRUE( EditorCommandRegistry::matchesPressedModifiers( redo, commandmodifier::kCtrl | commandmodifier::kShift, false ) );
    SW_EXPECT_FALSE( EditorCommandRegistry::matchesPressedModifiers( undo, commandmodifier::kNone, false ) );

    // Super 는 Ctrl 대신이 아니고, Ctrl 과 함께 눌려도 다른 조합이다.
    SW_EXPECT_FALSE( EditorCommandRegistry::matchesPressedModifiers( undo, commandmodifier::kNone, true ) );
    SW_EXPECT_FALSE( EditorCommandRegistry::matchesPressedModifiers( undo, commandmodifier::kCtrl, true ) );

    // DisplayOnly 비트는 수정자 비교에 들지 않는다(처리 여부는 `isHandledShortcut` 이 가린다).
    const EditorCommandShortcut displayOnly{ EditorCommandKey::F4, commandmodifier::kAlt | commandmodifier::kDisplayOnly };
    SW_EXPECT_TRUE( EditorCommandRegistry::matchesPressedModifiers( displayOnly, commandmodifier::kAlt, false ) );
}

/**
 * @brief [EditorCommandRegistryTest] 메뉴 · 항목 순서 · 구분선이 커맨드 표의 메뉴 경로 · 순서 칸에서 나온다
 * @details 메뉴바가 항목을 id 로 손으로 나열하면 커맨드를 더할 때 표와 메뉴바 두 곳을 고쳐야 한다. 표의 경로 · 순서 칸이 메뉴를
 *          만든다 — 등록 순서와 무관하게 순서 칸으로 줄 서고, 백의 자리가 바뀌는 자리에 구분선이 들어가며, 메뉴끼리는 가장 작은 순서로
 *          줄 선다.
 */
SW_TEST_CASE( EditorCommandRegistryTest, MenusComeFromMenuPathAndOrderColumns )
{
    EditorCommandRegistry registry;
    registry.registerCommand( makeMenuCommand( "edit.undo", "MainMenu/Edit", 2100 ) );
    registry.registerCommand( makeMenuCommand( "file.exit", "MainMenu/File", 1300 ) );
    registry.registerCommand( makeMenuCommand( "file.open", "MainMenu/File", 1110 ) );
    registry.registerCommand( makeCommand( "palette.only" ) );
    registry.registerCommand( makeMenuCommand( "file.new", "MainMenu/File", 1100 ) );
    registry.registerCommand( makeMenuCommand( "align.x", "Viewport/Align", 200 ) );
    registry.registerCommand( makeMenuCommand( "align.snap", "Viewport/Align", 100 ) );

    const vector<EditorMenu>& listMenu = registry.getMenus();
    SW_ASSERT_EQUAL( 3u, static_cast<uint32>( listMenu.size() ) );

    // 메뉴끼리는 메뉴 안 가장 작은 순서로 줄 선다(Align 100 < File 1100 < Edit 2100).
    SW_EXPECT_STREQ( "Viewport/Align", listMenu[0]._path.c_str() );
    SW_EXPECT_STREQ( "MainMenu/File", listMenu[1]._path.c_str() );
    SW_EXPECT_STREQ( "MainMenu/Edit", listMenu[2]._path.c_str() );
    SW_EXPECT_STREQ( "MainMenu", listMenu[1]._parentPath.c_str() );
    SW_EXPECT_STREQ( "File", listMenu[1]._name.c_str() );

    SW_EXPECT_STREQ( "file.new,file.open,-,file.exit", joinMenuItems( registry, listMenu[1] ).c_str() );
    SW_EXPECT_STREQ( "align.snap,-,align.x", joinMenuItems( registry, listMenu[0] ).c_str() );
    SW_EXPECT_STREQ( "edit.undo", joinMenuItems( registry, listMenu[2] ).c_str() );

    const EditorMenu* pFile = registry.findMenu( "MainMenu/File" );
    SW_ASSERT_TRUE( pFile != nullptr );
    SW_EXPECT_TRUE( pFile == &listMenu[1] );
    SW_EXPECT_TRUE( registry.findMenu( "MainMenu/Help" ) == nullptr );

    // 경로가 빈 커맨드는 어느 메뉴에도 없다(팔레트 · 단축키 전용).
    for ( const EditorMenu& menu : listMenu )
    {
        SW_EXPECT_TRUE( joinMenuItems( registry, menu ).find( "palette.only" ) == string::npos );
    }

    registry.clear();
    SW_EXPECT_TRUE( registry.getMenus().empty() );
}

/**
 * @brief [EditorCommandRegistryTest] 한 메뉴의 같은 순서 자리를 두 커맨드가 차지하면 validate 가 잡는다
 * @details 같은 순서는 둘 중 무엇이 위인지를 등록 순서에 맡긴다. 표가 메뉴를 정하므로 그것도 표의 오류다.
 */
SW_TEST_CASE( EditorCommandRegistryTest, ValidateCatchesTwoCommandsInOneMenuSlot )
{
    EditorCommandRegistry registry;
    registry.registerCommand( makeMenuCommand( "file.new", "MainMenu/File", 1100 ) );
    registry.registerCommand( makeMenuCommand( "edit.undo", "MainMenu/Edit", 1100 ) );

    string report;
    SW_EXPECT_TRUE( registry.validate( report ) );

    registry.registerCommand( makeMenuCommand( "file.open", "MainMenu/File", 1100 ) );
    SW_EXPECT_FALSE( registry.validate( report ) );
    SW_EXPECT_TRUE( report.find( "file.open" ) != string::npos );
}

/**
 * @brief [EditorCommandRegistryTest] 메인 메뉴바에도, 코드가 그리는 경로 목록에도 없는 메뉴 경로는 validate 가 잡는다
 * @details 메인 메뉴바는 부모가 `MainMenu` 인 메뉴만 그리고, 그 밖의 메뉴는 코드가 경로로 부를 때만 그려진다(`EditorCommandGui::drawMenuItems`). 표 줄의
 *          경로를 잘못 적거나(`MainMenu/File/Recent` 같은 하위 메뉴) 그리는 코드가 없는 경로를 적으면 그 항목은 경고 없이 어디에도 나오지 않는다.
 */
SW_TEST_CASE( EditorCommandRegistryTest, ValidateCatchesMenuPathsNothingDraws )
{
    EditorCommandRegistry registry;
    registry.registerCommand( makeMenuCommand( "file.new", "MainMenu/File", 1100 ) );
    registry.registerCommand( makeMenuCommand( "align.x", commandmenu::kViewportAlign, 200 ) );

    string report;
    SW_EXPECT_TRUE( registry.validate( report ) );

    registry.registerCommand( makeMenuCommand( "file.recent", "MainMenu/File/Recent", 1200 ) );
    registry.registerCommand( makeMenuCommand( "orphan.item", "Viewport/Orphan", 100 ) );
    SW_EXPECT_FALSE( registry.validate( report ) );
    SW_EXPECT_TRUE( report.find( "file.recent" ) != string::npos );
    SW_EXPECT_TRUE( report.find( "orphan.item" ) != string::npos );
    SW_EXPECT_TRUE( report.find( "align.x" ) == string::npos );
}
