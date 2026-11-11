#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Widgets/Model/EditorListFilter.h"
#include "Editor/Panels/InputMap/Model/InputMapModel.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct InputMapModelTestUtil
    {
        /** @brief 액션 @p pAction 을 만들고 키 @p key 를 잡아 바인딩 하나를 더합니다. */
        static void addKeyBinding( InputMapModel& document, const utf8* pAction, Key key, uint8 modifierMask = ModifierKey::None )
        {
            document.selectAction( pAction );
            if ( document.getInputMap().hasAction( hashed_string( pAction ) ) == false )
            {
                SW_REQUIRE_TRUE( document.createAction( pAction, InputActionValueType::Boolean ) );
            }
            document.beginNewBindingCapture();
            SW_REQUIRE_TRUE( document.finishKeyCapture( key, modifierMask ) );
        }
    };
    using Util = InputMapModelTestUtil;
} // namespace

/** @brief [InputMapModelTest] 경로가 게임 설정 파일이면 묶음을 읽고, 액션마다 그것을 정한 파일을 보인다 */
SW_TEST_CASE( InputMapModelTest, GameSettingsPathOpensTheBundle )
{
    InputMapModel document;
    SW_EXPECT_FALSE( document.isBundlePath() );
    SW_REQUIRE_TRUE( document.editInputMapPath().commit( "game/empty/input/bundletest/data/gamesettings.xml" ) );
    SW_EXPECT_TRUE( document.isBundlePath() );
    SW_REQUIRE_TRUE( document.reload() );
    SW_EXPECT_EQUAL( uint32{ 3 }, document.getInputMap().getSourceCount() );
    SW_EXPECT_EQUAL( string( "game/empty/input/bundletest/diff.input.xml" ), document.describeActionSource( hashed_string( "Use" ) ) );
    SW_EXPECT_EQUAL( string( "game/empty/input/bundletest/common.input.xml" ), document.describeActionSource( hashed_string( "Move" ) ) );
    SW_EXPECT_TRUE( document.describeActionSource( hashed_string( "Missing" ) ).empty() );
}

// 바뀐 편집만 저장하지 않은 표시를 켠다 — 패널이 후속 처리를 쓰지 않아도 표시가 맞다.
SW_TEST_CASE( InputMapModelTest, EditsThatChangeNothingStayClean )
{
    InputMapModel document;
    SW_EXPECT_FALSE( document.isDirty() );
    SW_EXPECT_FALSE( document.createAction( "", InputActionValueType::Boolean ) );
    SW_EXPECT_FALSE( document.setBindingNegate( hashed_string( "Missing" ), 0, true ) );
    SW_EXPECT_FALSE( document.removeBinding( hashed_string( "Missing" ), 0 ) );
    SW_EXPECT_FALSE( document.deleteAction( hashed_string( "Missing" ) ) );
    SW_EXPECT_FALSE( document.setLayerEnabled( hashed_string( "Missing" ), false ) ); // 없는 레이어
    SW_EXPECT_FALSE( document.isDirty() );
}

SW_TEST_CASE( InputMapModelTest, EditsThatChangeTheMapMarkItDirty )
{
    InputMapModel created;
    SW_EXPECT_TRUE( created.createAction( "Jump", InputActionValueType::Boolean ) );
    SW_EXPECT_TRUE( created.isDirty() );

    InputMapModel reset;
    reset.resetAllBindingsToDefault();
    SW_EXPECT_TRUE( reset.isDirty() );
}

// 결함: 레이어를 같은 값으로 "바꿔도" 저장하지 않은 표시가 켜졌고, 없는 레이어도 그랬다.
SW_TEST_CASE( InputMapModelTest, LayerToggleMarksDirtyOnlyWhenItChanges )
{
    InputMapModel document;
    SW_REQUIRE_TRUE( document.createLayer( "Menu", 3 ) );
    SW_EXPECT_EQUAL( 3, document.getInputMap().getLayerPriority( hashed_string( "Menu" ) ) );
    SW_EXPECT_FALSE( document.createLayer( "Menu", 4 ) ); // 있는 이름은 우선순위를 덮지 않는다
    SW_EXPECT_FALSE( document.createLayer( "", 0 ) );
    SW_EXPECT_EQUAL( 3, document.getInputMap().getLayerPriority( hashed_string( "Menu" ) ) );
    SW_EXPECT_FALSE( document.setLayerEnabled( hashed_string( "Menu" ), true ) ); // 이미 켜져 있다
    SW_EXPECT_TRUE( document.setLayerEnabled( hashed_string( "Menu" ), false ) );
    SW_EXPECT_FALSE( document.getInputMap().isLayerEnabled( hashed_string( "Menu" ) ) );
}

// 결함: 있는 이름으로 만들면 아무것도 만들지 않고 "만들었다" 고 입력칸을 비웠고, 저장하지 않은 표시까지 켰다.
SW_TEST_CASE( InputMapModelTest, CreatingAnExistingActionIsRefused )
{
    InputMapModel document;
    SW_REQUIRE_TRUE( document.createAction( "Jump", InputActionValueType::Axis1D ) );
    SW_EXPECT_TRUE( document.getSelectedAction() == "Jump" ); // 만들자마자 바인딩을 더할 수 있다
    SW_EXPECT_FALSE( document.canCreateAction( "Jump" ) );
    SW_EXPECT_TRUE( document.canCreateAction( "Run" ) );
    SW_EXPECT_FALSE( document.createAction( "Jump", InputActionValueType::Boolean ) );
    SW_EXPECT_EQUAL( size_t{ 1 }, document.getInputMap().getActionNames().size() );
}

SW_TEST_CASE( InputMapModelTest, DeletingTheSelectedActionClearsTheSelection )
{
    InputMapModel document;
    SW_REQUIRE_TRUE( document.createAction( "Jump", InputActionValueType::Boolean ) );
    SW_REQUIRE_TRUE( document.createAction( "Run", InputActionValueType::Boolean ) );
    SW_REQUIRE_TRUE( document.deleteAction( hashed_string( "Jump" ) ) );
    SW_EXPECT_TRUE( document.getSelectedAction() == "Run" ); // 다른 액션을 지우면 고름은 그대로다
    SW_REQUIRE_TRUE( document.deleteAction( hashed_string( "Run" ) ) );
    SW_EXPECT_TRUE( document.getSelectedAction().empty() );
    SW_EXPECT_TRUE( document.getInputMap().getActionNames().empty() );
}

// 키 잡기: 바인딩이 없는 액션이면 바뀌지 않고, 잡기는 끝난다.
SW_TEST_CASE( InputMapModelTest, KeyCaptureWithoutABindingLeavesTheMapClean )
{
    InputMapModel document;
    document.selectAction( "Missing" );
    document.beginKeyCapture( 0 );
    SW_EXPECT_TRUE( document.isCapturingKey() );
    SW_EXPECT_FALSE( document.finishKeyCapture( Key::A ) );
    SW_EXPECT_FALSE( document.isCapturingKey() );
    SW_EXPECT_FALSE( document.isDirty() );
    SW_EXPECT_FALSE( document.finishKeyCapture( Key::A ) ); // 잡는 중이 아니다
}

// 결함: 만든 액션에 바인딩을 더할 길이 없어, 새 액션은 영영 발화하지 않았다.
SW_TEST_CASE( InputMapModelTest, NewBindingGoesToTheLayerOfTheLastBinding )
{
    InputMapModel document;
    document.beginNewBindingCapture();
    SW_EXPECT_FALSE( document.isCapturingKey() ); // 고른 액션이 없다

    Util::addKeyBinding( document, "Jump", Key::A );
    const hashed_string action( "Jump" );
    SW_REQUIRE_EQUAL( uint32{ 1 }, document.getInputMap().getBindingCount( action ) );
    SW_EXPECT_TRUE( document.getInputMap().findBindingLayer( action, 0 ) == document.getInputMap().getDefaultLayerName() );
    SW_EXPECT_TRUE( document.isDirty() );

    SW_REQUIRE_TRUE( document.createLayer( "Menu", 0 ) );
    SW_REQUIRE_TRUE( document.setBindingLayer( action, 0, hashed_string( "Menu" ) ) );
    Util::addKeyBinding( document, "Jump", Key::B, static_cast<uint8>( ModifierKey::Ctrl ) );
    SW_REQUIRE_EQUAL( uint32{ 2 }, document.getInputMap().getBindingCount( action ) );
    SW_EXPECT_TRUE( document.getInputMap().findBindingLayer( action, 1 ) == hashed_string( "Menu" ) );
    SW_EXPECT_TRUE( document.getInputMap().getBinding( action, 1 )->_kind == BindingKind::Shortcut );
}

// 결함: 다시 잡기가 수식 키를 볼 수 없어 Ctrl+S 를 Ctrl+D 로 옮기지 못했고, Ctrl 을 누르는 순간 Ctrl 이 키로 잡혔다.
SW_TEST_CASE( InputMapModelTest, CaptureTakesTheWholeCombination )
{
    InputMapModel       document;
    const hashed_string action( "Save" );
    const uint8         ctrl = static_cast<uint8>( ModifierKey::Ctrl );
    Util::addKeyBinding( document, "Save", Key::S, ctrl );

    document.beginKeyCapture( 0 );
    InputMapCaptureFrame ctrlDown{};
    ctrlDown._pressedKey           = Key::LeftControl;
    ctrlDown._bPendingModifierDown = false;
    document.advanceKeyCapture( ctrlDown ); // 수식 키를 누른 프레임에는 끝나지 않는다
    SW_EXPECT_TRUE( document.isCapturingKey() );
    SW_EXPECT_TRUE( document.getPendingCaptureModifier() == Key::LeftControl );

    InputMapCaptureFrame chord{};
    chord._chordKey     = Key::D;
    chord._modifierMask = ctrl;
    document.advanceKeyCapture( chord );
    SW_EXPECT_FALSE( document.isCapturingKey() );
    const ActionBinding* pBinding = document.getInputMap().getBinding( action, 0 );
    SW_REQUIRE_NOT_NULL( pBinding );
    SW_EXPECT_TRUE( pBinding->_kind == BindingKind::Shortcut );
    SW_EXPECT_TRUE( pBinding->_arrSlot[0] == InputSlot::fromKey( Key::D ) );
    SW_EXPECT_EQUAL( ctrl, pBinding->_modifierMask );

    // 수식 키 없이 잡으면 단일 키가 된다 — 수식 키가 남으면 Ctrl+D 로 읽힌다.
    document.beginKeyCapture( 0 );
    InputMapCaptureFrame single{};
    single._chordKey = Key::F;
    document.advanceKeyCapture( single );
    pBinding = document.getInputMap().getBinding( action, 0 );
    SW_REQUIRE_NOT_NULL( pBinding );
    SW_EXPECT_TRUE( pBinding->_kind == BindingKind::SingleSlot );
    SW_EXPECT_EQUAL( uint8{ 0 }, pBinding->_modifierMask );
}

// 수식 키를 혼자 눌렀다 떼면 그 키 자신이다(달리기의 Shift).
SW_TEST_CASE( InputMapModelTest, CaptureTakesAModifierAloneWhenReleased )
{
    InputMapModel document;
    Util::addKeyBinding( document, "Run", Key::A );

    document.beginKeyCapture( 0 );
    InputMapCaptureFrame pressed{};
    pressed._pressedKey = Key::LeftShift;
    document.advanceKeyCapture( pressed );
    InputMapCaptureFrame held{};
    held._bPendingModifierDown = true;
    document.advanceKeyCapture( held );
    SW_EXPECT_TRUE( document.isCapturingKey() );
    InputMapCaptureFrame released{};
    document.advanceKeyCapture( released );
    SW_EXPECT_FALSE( document.isCapturingKey() );
    SW_EXPECT_TRUE( document.getInputMap().getBinding( hashed_string( "Run" ), 0 )->_arrSlot[0] == InputSlot::fromKey( Key::LeftShift ) );
}

SW_TEST_CASE( InputMapModelTest, EscapeCancelsTheCapture )
{
    InputMapModel document;
    SW_REQUIRE_TRUE( document.createAction( "Run", InputActionValueType::Boolean ) );
    document.beginNewBindingCapture();
    InputMapCaptureFrame escape{};
    escape._chordKey = Key::Escape;
    document.advanceKeyCapture( escape );
    SW_EXPECT_FALSE( document.isCapturingKey() );
    SW_EXPECT_EQUAL( uint32{ 0 }, document.getInputMap().getBindingCount( hashed_string( "Run" ) ) );
}

// 패드 버튼은 단일 바인딩과 새 바인딩만 받는다. 단축키에 넣으면 파일이 이름 붙이지 못한다.
SW_TEST_CASE( InputMapModelTest, SlotCaptureOnlyFitsSingleBindings )
{
    InputMapModel       document;
    const hashed_string action( "Run" );
    const InputSlot     button = InputSlot::fromGamepadButton( GamepadButton::RightShoulder, 0 );
    Util::addKeyBinding( document, "Run", Key::A, static_cast<uint8>( ModifierKey::Ctrl ) );

    document.beginKeyCapture( 0 );
    SW_EXPECT_FALSE( document.finishSlotCapture( button ) );
    SW_EXPECT_TRUE( document.isCapturingKey() ); // 받지 않았다 — 키를 기다린다
    document.cancelKeyCapture();

    document.beginNewBindingCapture();
    SW_REQUIRE_TRUE( document.finishSlotCapture( button ) );
    SW_REQUIRE_EQUAL( uint32{ 2 }, document.getInputMap().getBindingCount( action ) );
    SW_EXPECT_TRUE( document.getInputMap().getBinding( action, 1 )->_arrSlot[0] == button );

    document.beginKeyCapture( 1 ); // 단일 바인딩은 버튼을 바꿀 수 있다
    SW_EXPECT_TRUE( document.finishSlotCapture( InputSlot::fromGamepadButton( GamepadButton::LeftShoulder, 0 ) ) );
}

// 결함: 2D 합성 · 마우스 축의 발화 규칙 · Negate 를 고칠 수 있었지만 파일 형식에 그 칸이 없어 저장 뒤 말없이 사라졌다.
SW_TEST_CASE( InputMapModelTest, FieldsTheFileCannotKeepAreNotEditable )
{
    SW_EXPECT_TRUE( InputMapModel::canSaveTriggerProperties( BindingKind::SingleSlot ) );
    SW_EXPECT_TRUE( InputMapModel::canSaveTriggerProperties( BindingKind::Shortcut ) );
    SW_EXPECT_TRUE( InputMapModel::canSaveTriggerProperties( BindingKind::Chord ) );
    SW_EXPECT_TRUE( InputMapModel::canSaveTriggerProperties( BindingKind::Axis1DComposite ) );
    SW_EXPECT_FALSE( InputMapModel::canSaveTriggerProperties( BindingKind::Vector2DComposite ) );
    SW_EXPECT_FALSE( InputMapModel::canSaveTriggerProperties( BindingKind::GamepadStick2D ) );
    SW_EXPECT_FALSE( InputMapModel::canSaveTriggerProperties( BindingKind::MouseDelta2D ) );
    SW_EXPECT_FALSE( InputMapModel::canSaveTriggerProperties( BindingKind::MouseWheel1D ) );
    SW_EXPECT_FALSE( InputMapModel::canSaveTriggerProperties( BindingKind::VirtualJoystick2D ) );
    SW_EXPECT_FALSE( InputMapModel::canSaveTriggerProperties( BindingKind::AnyKey ) );
}

// 표기는 수식 키까지 보인다. 슬롯 하나만 보이면 Ctrl+S 가 S 로 읽힌다.
SW_TEST_CASE( InputMapModelTest, BindingTextShowsTheModifiers )
{
    InputMapModel document;
    Util::addKeyBinding( document, "Save", Key::S, static_cast<uint8>( ModifierKey::Ctrl ) );
    const string text = document.describeBinding( hashed_string( "Save" ), 0 );
    SW_EXPECT_TRUE( text.find( "Ctrl" ) != string::npos );
    SW_EXPECT_TRUE( document.describeBinding( hashed_string( "Save" ), 7 ).empty() );
}

// 필터는 이름과 키 표기를 본다.
SW_TEST_CASE( InputMapModelTest, FilterLooksAtNameAndKey )
{
    InputMapModel document;
    Util::addKeyBinding( document, "Jump", Key::Space );
    Util::addKeyBinding( document, "Fire", Key::F );

    vector<hashed_string> listAction;
    document.collectVisibleActions( EditorListFilter{ string_view( "" ) }, listAction );
    SW_EXPECT_EQUAL( size_t{ 2 }, listAction.size() );
    document.collectVisibleActions( EditorListFilter{ string_view( "fir" ) }, listAction );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listAction.size() );
    SW_EXPECT_TRUE( listAction[0] == hashed_string( "Fire" ) );
    document.collectVisibleActions( EditorListFilter{ string_view( "space" ) }, listAction );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listAction.size() );
    SW_EXPECT_TRUE( listAction[0] == hashed_string( "Jump" ) );
}

// ---------------------------------------------------------------------- 충돌

// 결함: 탭은 회피, 홀드는 차지처럼 한 키를 발화 규칙으로 나눈 정상 구성이 빨간 충돌로 보였다.
SW_TEST_CASE( InputMapConflictsTest, SameKeyWithDifferentTriggersIsNotAConflict )
{
    InputMap                 map;
    vector<InputMapConflict> listConflict;
    map.bind( hashed_string( "Dodge" ), Key::Space, ActionTrigger::Tap );
    map.bind( hashed_string( "Charge" ), Key::Space, ActionTrigger::HoldThreshold );
    InputMapConflict::collect( map, listConflict );
    SW_EXPECT_TRUE( listConflict.empty() );

    map.bind( hashed_string( "Jump" ), Key::Space, ActionTrigger::Tap );
    InputMapConflict::collect( map, listConflict );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listConflict.size() );
    SW_EXPECT_TRUE( listConflict[0]._actionA == hashed_string( "Dodge" ) );
    SW_EXPECT_TRUE( listConflict[0]._actionB == hashed_string( "Jump" ) );
}

// 합성 축은 누르는 동안 값을 읽으므로 발화 규칙과 상관없이 겹친다.
SW_TEST_CASE( InputMapConflictsTest, CompositeOverlapsAnySharedKey )
{
    InputMap                 map;
    vector<InputMapConflict> listConflict;
    map.bindAxis1DComposite( hashed_string( "Move" ), Key::A, Key::D );
    map.bind( hashed_string( "Dash" ), Key::D, ActionTrigger::Tap );
    InputMapConflict::collect( map, listConflict );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listConflict.size() );
    SW_EXPECT_TRUE( listConflict[0]._slot == InputSlot::fromKey( Key::D ) );
}

// 조합은 조합 전체가 같아야 겹치고, 레이어가 다르면 겹치지 않는다.
SW_TEST_CASE( InputMapConflictsTest, CombinationsAndLayers )
{
    InputMap                 map;
    vector<InputMapConflict> listConflict;
    const uint8              ctrl = static_cast<uint8>( ModifierKey::Ctrl );
    map.bindShortcut( hashed_string( "Save" ), Key::S, ctrl );
    map.bind( hashed_string( "Back" ), Key::S );
    map.bindShortcut( hashed_string( "Other" ), Key::S, ctrl, ActionTrigger::Pressed, hashed_string( "Menu" ) );
    InputMapConflict::collect( map, listConflict );
    SW_EXPECT_TRUE( listConflict.empty() );

    map.bindShortcut( hashed_string( "SaveAgain" ), Key::S, ctrl );
    InputMapConflict::collect( map, listConflict );
    SW_EXPECT_EQUAL( size_t{ 1 }, listConflict.size() );
}

// 충돌 칸은 고른 액션 쪽에서 본 줄을 받는다.
SW_TEST_CASE( InputMapConflictsTest, ViewIsSeenFromTheAction )
{
    InputMap                 map;
    vector<InputMapConflict> listConflict;
    map.bind( hashed_string( "A" ), Key::Z );
    map.bind( hashed_string( "A" ), Key::Space );
    map.bind( hashed_string( "B" ), Key::Space );
    map.bind( hashed_string( "C" ), Key::X );
    InputMapConflict::collect( map, listConflict );
    SW_EXPECT_TRUE( InputMapConflict::isInvolved( listConflict, hashed_string( "A" ), 1 ) );
    SW_EXPECT_FALSE( InputMapConflict::isInvolved( listConflict, hashed_string( "A" ), 0 ) );
    SW_EXPECT_FALSE( InputMapConflict::isInvolved( listConflict, hashed_string( "C" ) ) );

    vector<InputMapConflictView> listView;
    InputMapConflict::collectFor( listConflict, hashed_string( "B" ), listView );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listView.size() );
    SW_EXPECT_TRUE( listView[0]._otherAction == hashed_string( "A" ) );
    SW_EXPECT_EQUAL( uint32{ 0 }, listView[0]._bindIndex );
    SW_EXPECT_EQUAL( uint32{ 1 }, listView[0]._otherBindIndex );
}

// 다시 잡아 겹치면 바꾸되 알린다.
SW_TEST_CASE( InputMapConflictsTest, CaptureWarnsWhenItOverlaps )
{
    SW_EXPECT_LOG( "InputMapModel.BindingConflictsSameLayer" );
    InputMapModel document;
    Util::addKeyBinding( document, "Jump", Key::Space );
    Util::addKeyBinding( document, "Fire", Key::F );
    document.beginKeyCapture( 0 );
    SW_EXPECT_TRUE( document.finishKeyCapture( Key::Space ) );
    SW_EXPECT_EQUAL( size_t{ 1 }, document.getConflicts().size() );
}
