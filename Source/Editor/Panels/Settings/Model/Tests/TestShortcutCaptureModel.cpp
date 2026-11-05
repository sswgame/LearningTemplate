#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Panels/Settings/Model/ShortcutCaptureModel.h"

#include "Engine/Input/RawInputEvent.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /** @brief 입력 맵 없이 답을 정해 두고, 불린 것을 기억합니다. */
    class FakeShortcutBindings final : public IShortcutBindings
    {
    public:
        string _conflictWith;    ///< 비어 있지 않으면 `rebindShortcut` 이 이 액션과 겹쳤다고 답한다
        string _lastAction;      ///< 마지막으로 바꾸거나 되돌린 액션
        string _lastOtherAction; ///< 마지막 `swapShortcut` 의 겹친 액션
        Key    _lastKey;         ///< 마지막으로 받은 키
        uint8  _lastModifier;
        int32  _rebindCount;
        int32  _swapCount;
        int32  _resetCount;
        int32  _resetAllCount;
        int32  _suspendCallCount;
        uint8  _bNoKeyboardBinding : 1;
        uint8  _bSwapFails         : 1;
        uint8  _bSuspended         : 1;

        FakeShortcutBindings()
            : _conflictWith{}
            , _lastAction{}
            , _lastOtherAction{}
            , _lastKey{ Key::Unknown }
            , _lastModifier{ ModifierKey::None }
            , _rebindCount{ 0 }
            , _swapCount{ 0 }
            , _resetCount{ 0 }
            , _resetAllCount{ 0 }
            , _suspendCallCount{ 0 }
            , _bNoKeyboardBinding{ SW_FALSE }
            , _bSwapFails{ SW_FALSE }
            , _bSuspended{ SW_FALSE }
        {
        }

        bool rebindShortcut( string_view action, Key key, uint8 modifierMask, hashed_string& outConflictAction ) override
        {
            ++_rebindCount;
            _lastAction   = string( action );
            _lastKey      = key;
            _lastModifier = modifierMask;
            if ( _bNoKeyboardBinding != SW_FALSE )
                return false;
            if ( _conflictWith.empty() )
                return true;
            outConflictAction = hashed_string( _conflictWith.c_str() );
            return false;
        }
        bool swapShortcut( string_view action, string_view otherAction, Key key, uint8 modifierMask ) override
        {
            ++_swapCount;
            _lastAction      = string( action );
            _lastOtherAction = string( otherAction );
            _lastKey         = key;
            _lastModifier    = modifierMask;
            return _bSwapFails == SW_FALSE;
        }
        void resetShortcut( string_view action ) override
        {
            ++_resetCount;
            _lastAction = string( action );
        }
        void resetAllShortcuts() override { ++_resetAllCount; }
        void setCommandsSuspended( bool bSuspended ) override
        {
            ++_suspendCallCount;
            _bSuspended = bSuspended ? SW_TRUE : SW_FALSE;
        }
    };

    /** @brief `undo` 줄의 조합을 받습니다. `conflictOnRedo` 는 받은 Ctrl+U 가 `redo` 와 겹쳐 묻는 상태까지 갑니다. */
    struct ShortcutCaptureModelTest
    {
        const string         _undo;
        const string         _redo;
        FakeShortcutBindings _bindings;
        ShortcutCaptureModel _model;

        ShortcutCaptureModelTest()
            : _undo{ "edit.undo" }
            , _redo{ "edit.redo" }
            , _bindings{}
            , _model{}
        {
        }

        void conflictOnRedo()
        {
            _bindings._conflictWith = _redo;
            _model.beginCapture( _bindings, _undo );
            _model.submitChord( _bindings, Key::U, ModifierKey::Ctrl );
        }
    };
} // namespace

SW_TEST_CASE_F( ShortcutCaptureModelTest, BeginWaitsForChordAndSuspendsCommands )
{
    SW_EXPECT_FALSE( _model.isWaitingForChord() );
    _model.beginCapture( _bindings, _undo );
    SW_EXPECT_TRUE( _model.isWaitingForChord() );
    SW_EXPECT_TRUE( _model.isCapturing( _undo ) );
    SW_EXPECT_FALSE( _model.isCapturing( _redo ) );
    SW_EXPECT_FALSE( _model.hasConflict() );
    SW_EXPECT_TRUE( _bindings._bSuspended != SW_FALSE );
}

SW_TEST_CASE_F( ShortcutCaptureModelTest, ChordIsAppliedAndFinishes )
{
    _model.beginCapture( _bindings, _undo );
    _model.submitChord( _bindings, Key::U, ModifierKey::Ctrl | ModifierKey::Shift );

    SW_EXPECT_EQUAL( 1, _bindings._rebindCount );
    SW_EXPECT_TRUE( _bindings._lastAction == _undo );
    SW_EXPECT_TRUE( _bindings._lastKey == Key::U );
    SW_EXPECT_TRUE( _bindings._lastModifier == ( ModifierKey::Ctrl | ModifierKey::Shift ) );
    SW_EXPECT_FALSE( _model.isWaitingForChord() );
    SW_EXPECT_FALSE( _model.isCapturing( _undo ) );
    SW_EXPECT_TRUE( _bindings._bSuspended == SW_FALSE );
}

SW_TEST_CASE_F( ShortcutCaptureModelTest, ChordWithoutCaptureDoesNothing )
{
    _model.submitChord( _bindings, Key::U, ModifierKey::Ctrl );
    SW_EXPECT_EQUAL( 0, _bindings._rebindCount );
    SW_EXPECT_EQUAL( 0, _bindings._suspendCallCount );
}

SW_TEST_CASE_F( ShortcutCaptureModelTest, PlainEscapeCancelsAndModifiedEscapeIsAChord )
{
    _model.beginCapture( _bindings, _undo );
    _model.submitChord( _bindings, Key::Escape, ModifierKey::None );
    SW_EXPECT_EQUAL( 0, _bindings._rebindCount );
    SW_EXPECT_FALSE( _model.isWaitingForChord() );
    SW_EXPECT_TRUE( _bindings._bSuspended == SW_FALSE );

    _model.beginCapture( _bindings, _undo );
    _model.submitChord( _bindings, Key::Escape, ModifierKey::Ctrl );
    SW_EXPECT_EQUAL( 1, _bindings._rebindCount );
    SW_EXPECT_TRUE( _bindings._lastKey == Key::Escape );
}

SW_TEST_CASE_F( ShortcutCaptureModelTest, ActionWithoutKeyboardBindingFinishes )
{
    _bindings._bNoKeyboardBinding = SW_TRUE;
    _model.beginCapture( _bindings, _undo );
    _model.submitChord( _bindings, Key::U, ModifierKey::Ctrl );

    SW_EXPECT_EQUAL( 1, _bindings._rebindCount );
    SW_EXPECT_FALSE( _model.hasConflict() );
    SW_EXPECT_FALSE( _model.isCapturing( _undo ) );
    SW_EXPECT_TRUE( _bindings._bSuspended == SW_FALSE );
}

SW_TEST_CASE_F( ShortcutCaptureModelTest, ConflictAsksAndStopsReadingChords )
{
    conflictOnRedo();

    SW_EXPECT_TRUE( _model.hasConflict() );
    SW_EXPECT_FALSE( _model.isWaitingForChord() );
    SW_EXPECT_TRUE( _model.isCapturing( _undo ) );
    SW_EXPECT_TRUE( _model.isConflictRow( _undo ) );
    SW_EXPECT_TRUE( _model.isConflictRow( _redo ) );
    SW_EXPECT_FALSE( _model.isConflictRow( "edit.copy" ) );
    SW_EXPECT_TRUE( _bindings._bSuspended != SW_FALSE );

    _model.submitChord( _bindings, Key::K, ModifierKey::Ctrl );
    SW_EXPECT_EQUAL( 1, _bindings._rebindCount );
}

SW_TEST_CASE_F( ShortcutCaptureModelTest, ReplaceSwapsTheChordHeldForTheQuestion )
{
    conflictOnRedo();
    _model.confirmReplace( _bindings );

    SW_EXPECT_EQUAL( 1, _bindings._swapCount );
    SW_EXPECT_TRUE( _bindings._lastAction == _undo );
    SW_EXPECT_TRUE( _bindings._lastOtherAction == _redo );
    SW_EXPECT_TRUE( _bindings._lastKey == Key::U );
    SW_EXPECT_TRUE( _bindings._lastModifier == ModifierKey::Ctrl );
    SW_EXPECT_FALSE( _model.hasConflict() );
    SW_EXPECT_FALSE( _model.isCapturing( _undo ) );
    SW_EXPECT_TRUE( _bindings._bSuspended == SW_FALSE );
}

SW_TEST_CASE_F( ShortcutCaptureModelTest, ReplaceWithoutConflictDoesNothing )
{
    _model.confirmReplace( _bindings );
    SW_EXPECT_EQUAL( 0, _bindings._swapCount );
}

// 결함: 못 바꿔도 묻기를 끝내, 한 프레임만 보인 오류 뒤로 바꾸기가 된 것처럼 보였다.
SW_TEST_CASE_F( ShortcutCaptureModelTest, FailedReplaceKeepsAsking )
{
    SW_EXPECT_LOG( "ShortcutsPanel.CouldNotSwapShortcut" );
    _bindings._bSwapFails = SW_TRUE;
    conflictOnRedo();
    _model.confirmReplace( _bindings );

    SW_EXPECT_EQUAL( 1, _bindings._swapCount );
    SW_EXPECT_TRUE( _model.hasConflict() );
    SW_EXPECT_TRUE( _bindings._bSuspended != SW_FALSE );

    _model.cancel( _bindings );
    SW_EXPECT_FALSE( _model.hasConflict() );
    SW_EXPECT_TRUE( _bindings._bSuspended == SW_FALSE );
}

SW_TEST_CASE_F( ShortcutCaptureModelTest, CancelDuringConflictDoesNotSwap )
{
    conflictOnRedo();
    _model.cancel( _bindings );

    SW_EXPECT_EQUAL( 0, _bindings._swapCount );
    SW_EXPECT_FALSE( _model.hasConflict() );
    SW_EXPECT_FALSE( _model.isConflictRow( _redo ) );
    SW_EXPECT_TRUE( _bindings._bSuspended == SW_FALSE );
}

// 결함: 받는 중에 창을 닫거나 탭을 가리면 명령 실행이 멈춘 채 남아 에디터 단축키가 모두 죽었다 — 패널은 그 두 길에서 `cancel` 을 부른다.
SW_TEST_CASE_F( ShortcutCaptureModelTest, CancelWhileWaitingResumesCommandsOnce )
{
    _model.beginCapture( _bindings, _undo );
    SW_EXPECT_TRUE( _bindings._bSuspended != SW_FALSE );

    _model.cancel( _bindings );
    SW_EXPECT_TRUE( _bindings._bSuspended == SW_FALSE );
    SW_EXPECT_FALSE( _model.isCapturing( _undo ) );
    const int32 callCount = _bindings._suspendCallCount;

    _model.cancel( _bindings ); // 접힌 창은 프레임마다 부른다 — 다른 쪽의 멈춤을 건드리지 않는다
    SW_EXPECT_EQUAL( callCount, _bindings._suspendCallCount );
}

SW_TEST_CASE_F( ShortcutCaptureModelTest, BeginOnAnotherRowDropsTheQuestion )
{
    conflictOnRedo();
    _bindings._conflictWith.clear();
    _model.beginCapture( _bindings, _redo );

    SW_EXPECT_FALSE( _model.hasConflict() );
    SW_EXPECT_TRUE( _model.isWaitingForChord() );
    SW_EXPECT_TRUE( _model.isCapturing( _redo ) );
    SW_EXPECT_FALSE( _model.isCapturing( _undo ) );

    _model.submitChord( _bindings, Key::R, ModifierKey::Alt );
    SW_EXPECT_TRUE( _bindings._lastAction == _redo );
    SW_EXPECT_TRUE( _bindings._lastKey == Key::R );
    SW_EXPECT_TRUE( _bindings._bSuspended == SW_FALSE );
}

// 결함: 묻는 동안 Reset All 을 눌러도 물음이 남아, 되돌린 맵에서 낡은 겹침으로 바꾸기를 할 수 있었다.
SW_TEST_CASE_F( ShortcutCaptureModelTest, ResetAllDuringConflictDropsTheQuestion )
{
    conflictOnRedo();
    _model.resetAllShortcuts( _bindings );

    SW_EXPECT_EQUAL( 1, _bindings._resetAllCount );
    SW_EXPECT_FALSE( _model.hasConflict() );
    SW_EXPECT_TRUE( _bindings._bSuspended == SW_FALSE );

    _model.confirmReplace( _bindings );
    SW_EXPECT_EQUAL( 0, _bindings._swapCount );
}

SW_TEST_CASE_F( ShortcutCaptureModelTest, ResetRowDuringConflictDropsTheQuestion )
{
    conflictOnRedo();
    _model.resetShortcut( _bindings, _redo );

    SW_EXPECT_EQUAL( 1, _bindings._resetCount );
    SW_EXPECT_TRUE( _bindings._lastAction == _redo );
    SW_EXPECT_FALSE( _model.hasConflict() );
    SW_EXPECT_FALSE( _model.isCapturing( _undo ) );
    SW_EXPECT_TRUE( _bindings._bSuspended == SW_FALSE );
}

SW_TEST_CASE_F( ShortcutCaptureModelTest, ResetWithoutCaptureLeavesCommandsAlone )
{
    _model.resetShortcut( _bindings, _undo );
    _model.resetAllShortcuts( _bindings );

    SW_EXPECT_EQUAL( 1, _bindings._resetCount );
    SW_EXPECT_EQUAL( 1, _bindings._resetAllCount );
    SW_EXPECT_EQUAL( 0, _bindings._suspendCallCount );
}
