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
} // namespace

/**
 * @brief [EditorCommandRegistryTest] 단축키 라벨이 Ctrl→Shift→Alt 순서로, 보조 조합은 " / " 로 이어붙는지 검증
 */
SW_TEST_CASE( EditorCommandRegistryTest, ShortcutLabelUsesFixedModifierOrder )
{
    fixed_string<constant::kMaxBuffer64> label;

    EditorCommandDesc single{};
    single._shortcut = EditorCommandShortcut{ EditorCommandKey::B, commandmod::kCtrl | commandmod::kShift };
    EditorCommandRegistry::formatShortcutLabel( single, label );
    SW_EXPECT_STREQ( "Ctrl+Shift+B", label.c_str() );

    EditorCommandDesc ctrlAlt{};
    ctrlAlt._shortcut = EditorCommandShortcut{ EditorCommandKey::F11, commandmod::kCtrl | commandmod::kAlt };
    // 보조 조합은 수정자 없이 F7 하나다 — 예전에는 이 키가 어느 라벨에도 없어서 아무도 몰랐다.
    ctrlAlt._altShortcut = EditorCommandShortcut{ EditorCommandKey::F7, commandmod::kNone };
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
 * @details 표면이 셋으로 갈려 있던 동안 Ctrl+Z 를 전역 처리기와 Inspector 가 각각 처리해 두 번
 *          되돌아갔다. 정의를 한곳에 모은 지금은 그 충돌이 기계에 걸린다.
 */
SW_TEST_CASE( EditorCommandRegistryTest, ValidateCatchesDuplicateIdAndChord )
{
    EditorCommandRegistry clean;
    clean.registerCommand( makeCommand( "test.a" ) );

    EditorCommandDesc withChord = makeCommand( "test.b" );
    withChord._shortcut         = EditorCommandShortcut{ EditorCommandKey::Z, commandmod::kCtrl };
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
    undo._shortcut             = EditorCommandShortcut{ EditorCommandKey::Z, commandmod::kCtrl };
    duplicateChord.registerCommand( std::move( undo ) );

    // 보조 조합끼리의 충돌도 잡아야 한다 — 주 조합만 보면 Ctrl+Z 를 보조로 든 커맨드를 놓친다.
    EditorCommandDesc redo = makeCommand( "test.redo" );
    redo._shortcut         = EditorCommandShortcut{ EditorCommandKey::Y, commandmod::kCtrl };
    redo._altShortcut      = EditorCommandShortcut{ EditorCommandKey::Z, commandmod::kCtrl };
    duplicateChord.registerCommand( std::move( redo ) );

    SW_EXPECT_FALSE( duplicateChord.validate( report ) );
    SW_EXPECT_FALSE( report.empty() );
}

/**
 * @brief [EditorCommandRegistryTest] 표시 전용 조합(Alt+F4)은 처리 대상이 아니고 충돌로도 세지 않는지 검증
 */
SW_TEST_CASE( EditorCommandRegistryTest, DisplayOnlyShortcutIsNeverHandled )
{
    const EditorCommandShortcut displayOnly{ EditorCommandKey::F4, commandmod::kAlt | commandmod::kDisplayOnly };
    const EditorCommandShortcut handled{ EditorCommandKey::F4, commandmod::kAlt };

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
