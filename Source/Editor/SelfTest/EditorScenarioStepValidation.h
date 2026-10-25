/**
 * @file EditorScenarioStepValidation.h
 * @brief 에디터 시나리오 단계(`EditorClick` · `EditorDrag` · `EditorHover` · `EditorText` · `EditorKey` · `EditorExpectObject` · `EditorExpectDockLayout` · `CaptureEditorUI`)의
 *        속성 읽기와 검사 함수를 정의합니다. ImGui · 렌더 · 에디터 컨텍스트를 모릅니다.
 * @details 검사 함수는 순수 함수라 에디터 모듈이 올라오지 않은 프로세스(nogpu 테스트)도 부를 수 있다. 그래서 단계를 그리는 `EditorScenarioSteps.cpp` 와 따로 두고,
 *          그쪽은 이 파일의 읽기를 불러 ImGui 입력으로 옮기기만 한다(수정자는 비트 마스크로 읽고 ImGui 값으로의 바꿈은 그쪽이 한다).
 *          검사 함수는 `SW_AUTOMATION_STEP_VALIDATOR` 로도 올라가(`EditorScenarioStepValidation.cpp`) 단계 등록 없이 `AutomationStepValidation::findValidator` 가 찾는다.
 *          `EditorKey` 의 키 이름이 ImGui 가 아는 이름인지는 ImGui 의 이름표가 있어야 알 수 있어, 이 파일은 낱말 모양만 보고 이름은 단계의 준비 함수가 본다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    struct AutomationStep;
} // namespace sw

namespace sw::editor
{
    /** @brief `state` 속성("down", "up")이 가리키는 누름 방식입니다. 없으면 누르고 놓는 한 번(`Tap`)입니다. */
    enum class EditorPressState : uint8
    {
        Tap,
        Down,
        Up,
    };

    /** @brief `mods` 속성의 수정자 키 비트입니다. */
    namespace EditorModifierBit
    {
        constexpr uint8 kCtrl  = 1;
        constexpr uint8 kShift = 2;
        constexpr uint8 kAlt   = 4;
    } // namespace EditorModifierBit

    /** @brief `EditorClick` 의 인자입니다. */
    struct EditorClickArgs
    {
        uint8            _modifierMask{ 0 };
        float2           _anchor{ 0.5f, 0.5f }; ///< 레이블 안의 위치(0..1) — 적지 않으면 가운데
        int32            _button{ 0 };
        EditorPressState _state{ EditorPressState::Tap };
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief `EditorDrag` 의 인자입니다. 없는 이동량 · 버튼은 0 입니다. */
    struct EditorDragArgs
    {
        uint8   _modifierMask{ 0 };
        float64 _deltaX{ 0.0 };
        float64 _deltaY{ 0.0 };
        float64 _buttonNumber{ 0.0 };
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief `EditorExpectObject` 의 인자입니다. 적지 않으면 한 개, 선택 검사 없음입니다. */
    struct EditorExpectObjectArgs
    {
        uint32 _expectedCount{ 1 };
        uint32 _selected{ 0 }; ///< 1 이면 그 오브젝트가 선택돼 있어야 한다
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @struct EditorScenarioStepValidation @brief 에디터 시나리오 단계의 속성 읽기와 검사 함수 모음입니다. */
    struct EditorScenarioStepValidation
    {
        // 단계 종류 이름입니다(단계 등록과 검사 함수 등록이 같은 글을 쓴다).
        static constexpr const utf8* kClickKind        = "EditorClick";
        static constexpr const utf8* kDragKind         = "EditorDrag";
        static constexpr const utf8* kHoverKind        = "EditorHover";
        static constexpr const utf8* kTextKind         = "EditorText";
        static constexpr const utf8* kKeyKind          = "EditorKey";
        static constexpr const utf8* kExpectObjectKind = "EditorExpectObject";
        static constexpr const utf8* kDockLayoutKind   = "EditorExpectDockLayout";
        static constexpr const utf8* kCaptureKind      = "CaptureEditorUI";

        /** @brief 음이 아닌 정수 속성을 읽습니다. 없으면 @p inoutValue 를 둡니다. 형식이 틀리면 false 이고 값은 그대로입니다. */
        [[nodiscard]] static bool readCount( const AutomationStep& step, string_view name, uint32& inoutValue );
        /** @brief `anchor="x,y"`(레이블 안의 위치 0..1)를 읽습니다. 없으면 @p outAnchor 를 두고 true 를 반환하며, 형식이 틀리면 false 입니다. */
        [[nodiscard]] static bool readAnchor( const AutomationStep& step, float2& outAnchor );
        /** @brief `state` 속성을 읽습니다. 없으면 @p inoutState 를 두고 true 이며, 모르는 값이면 false 입니다. */
        [[nodiscard]] static bool readPressState( const AutomationStep& step, EditorPressState& inoutState );
        /** @brief `mods` 속성("ctrl", "shift", "ctrl+shift" 등)을 `EditorModifierBit` 마스크로 읽습니다. 없으면 0 이고, 모르는 이름이면 false 입니다. */
        [[nodiscard]] static bool parseModifiers( const AutomationStep& step, uint8& outMask );
        /** @brief "Ctrl+Z" 를 '+' 로 갈라 낱말로 나눕니다(낱말은 @p chord 의 조각입니다). 빈 낱말이 있거나 비었으면 false 입니다. 낱말이 키 이름인지는 보지 않습니다. */
        [[nodiscard]] static bool splitKeyChord( string_view chord, vector<string_view>& outListToken );

        [[nodiscard]] static bool parseClick( const AutomationStep& step, EditorClickArgs& outArgs, string& outError );
        [[nodiscard]] static bool parseDrag( const AutomationStep& step, EditorDragArgs& outArgs, string& outError );
        [[nodiscard]] static bool parseExpectObject( const AutomationStep& step, EditorExpectObjectArgs& outArgs, string& outError );

        // 단계 등록의 검사 함수(`AutomationStepValidateFunction`)입니다.
        static bool validateClick( const AutomationStep& step, string& outError );
        static bool validateText( const AutomationStep& step, string& outError );
        static bool validateDrag( const AutomationStep& step, string& outError );
        static bool validateHover( const AutomationStep& step, string& outError );
        /** @brief 속성 이름 · 필수 · 누름 방식 · 키 조합의 낱말 모양을 봅니다. 키 이름이 ImGui 이름인지는 단계의 준비 함수가 봅니다(이 파일은 ImGui 를 모른다). */
        static bool validateKey( const AutomationStep& step, string& outError );
        /** @brief 속성 이름 · 필수 · 수 형식을 봅니다. `component` 가 아는 컴포넌트 타입인지는 타입 레지스트리(에디터 서비스)가 있어야 알 수 있어 단계의 준비 함수가 봅니다. */
        static bool validateExpectObject( const AutomationStep& step, string& outError );
        static bool validateDockLayout( const AutomationStep& step, string& outError );
        static bool validateCapture( const AutomationStep& step, string& outError );
    };
} // namespace sw::editor
