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
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/EditorExports.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    struct AutomationStep;
} // namespace sw

namespace sw::editor
{
    /** @brief `state` 속성(`Down` · `Up`)이 가리키는 누름 방식입니다. 적지 않으면 누르고 놓는 한 번(`Tap`)입니다. */
    ENUM()
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
    REFLECT()
    struct SW_EDITOR_API EditorClickParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _mark{};
        PROPERTY()
        float2 _anchor{ 0.5f, 0.5f }; ///< 레이블 안의 위치(0..1) — 적지 않으면 가운데
        PROPERTY( Min = 0, Max = 4 )
        int32 _button{ 0 };
        PROPERTY()
        string _mods{}; ///< 수정자(`ctrl+shift`) — `parseModifiers`
        PROPERTY()
        EditorPressState _state{ EditorPressState::Tap };
    };

    /** @brief `EditorDrag` 의 인자입니다. 끝은 `toMark` 또는 이동량(`dx` · `dy`) 하나로 적습니다. */
    REFLECT()
    struct SW_EDITOR_API EditorDragParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _mark{};
        PROPERTY()
        float2 _anchor{ 0.5f, 0.5f }; ///< 누르기 시작할 레이블 안 위치(0..1). 적지 않으면 가운데
        PROPERTY()
        string _toMark{};
        PROPERTY()
        float32 _dx{ 0.0f };
        PROPERTY()
        float32 _dy{ 0.0f };
        PROPERTY( Min = 0, Max = 4 )
        int32 _button{ 0 };
        PROPERTY()
        string _mods{};
    };

    /** @brief `EditorHover` 의 인자입니다. `mark` 또는 `release` 하나를 적습니다(`release` 는 적었는지가 뜻입니다). */
    REFLECT()
    struct SW_EDITOR_API EditorHoverParams
    {
        REFLECT_BODY();

        PROPERTY()
        string _mark{};
        PROPERTY()
        float2 _anchor{ 0.5f, 0.5f };
    };

    /** @brief `EditorText` 의 인자입니다. */
    REFLECT()
    struct SW_EDITOR_API EditorTextParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _value{};
    };

    /** @brief `EditorSelfTest` 의 인자입니다. */
    REFLECT()
    struct SW_EDITOR_API EditorSelfTestParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _pattern{};
    };

    /** @brief `EditorKey` 의 인자입니다. */
    REFLECT()
    struct SW_EDITOR_API EditorKeyParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _key{};
        PROPERTY()
        EditorPressState _state{ EditorPressState::Tap };
    };

    /** @brief `EditorExpectObject` 의 인자입니다. 적지 않으면 한 개, 선택 검사 없음입니다. */
    REFLECT()
    struct SW_EDITOR_API EditorExpectObjectParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _name{};
        PROPERTY()
        uint32 _count{ 1 };
        PROPERTY()
        string _component{};
        PROPERTY( Max = 1 )
        uint32 _selected{ 0 }; ///< 1 이면 그 오브젝트가 선택돼 있어야 한다
    };

    /** @brief `EditorExpectDockLayout` 의 인자입니다. 0 이면 기본 최소 변입니다. */
    REFLECT()
    struct SW_EDITOR_API EditorDockLayoutParams
    {
        REFLECT_BODY();

        PROPERTY()
        uint32 _minNodeSize{ 0 };
    };

    /** @brief `CaptureEditorUI` 칸의 "상한을 적지 않음" 입니다 — `Min` 은 적힌 값만 보므로 음수 초기값이 곧 적지 않음입니다. */
    inline constexpr float32 kEditorCaptureNoLimit = -1.0f;

    /** @brief `CaptureEditorUI` 의 인자입니다. */
    REFLECT()
    struct SW_EDITOR_API EditorCaptureParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _file{};
        PROPERTY( Min = 0 )
        float32 _maxMagentaFraction{ kEditorCaptureNoLimit };
        PROPERTY( Min = 0 )
        float32 _maxClippedWindows{ kEditorCaptureNoLimit };
    };

    /** @brief 인자가 없는 에디터 단계의 스키마입니다. */
    REFLECT()
    struct SW_EDITOR_API EditorNoParams
    {
        REFLECT_BODY();
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
        static constexpr const utf8* kSelfTestKind     = "EditorSelfTest";
        static constexpr const utf8* kWaitStableKind   = "EditorWaitSettled";
        /**
         * @brief UI 캡처 한 번이 시나리오 벽시계 상한에 더하는 초입니다. 캡처는 에디터가 가라앉기(애셋 · 배경 작업 — 바깥 일)를 벽시계로 기다린다.
         * @details 캡처 수에 상한이 따라가지 않으면 패널마다 찍는 감사(uiaudit, 40 장 넘게)가 부하 걸린 기계에서 기본 150 초를 넘겨 졌다
         *          (백엔드 교체의 `kSwapBackendWallSeconds` 와 같은 모양). 한가한 기계에서 한 장은 1.5 초 안팎이다.
         */
        static constexpr float32 kCaptureWallSeconds = 4.0f;

        /** @brief 수정자 글(`ctrl` · `shift` · `ctrl+shift` 등)을 `EditorModifierBit` 마스크로 읽습니다. 비면 0 이고, 모르는 낱말이면 false 입니다. */
        [[nodiscard]] static bool parseModifiers( string_view mods, uint8& outMask );
        /** @brief "Ctrl+Z" 를 '+' 로 갈라 낱말로 나눕니다(낱말은 @p chord 의 조각입니다). 빈 낱말이 있거나 비었으면 false 입니다. 낱말이 키 이름인지는 보지 않습니다. */
        [[nodiscard]] static bool splitKeyChord( string_view chord, vector<string_view>& outListToken );

        // 단계 등록의 검사 함수(`AutomationStepValidateFunction`)입니다.
        [[nodiscard]] static expected<void> validateClick( const AutomationStep& step );
        [[nodiscard]] static expected<void> validateDrag( const AutomationStep& step );
        [[nodiscard]] static expected<void> validateHover( const AutomationStep& step );
        /** @brief 속성 이름 · 필수 · 누름 방식 · 키 조합의 낱말 모양을 봅니다. 키 이름이 ImGui 이름인지는 단계의 준비 함수가 봅니다(이 파일은 ImGui 를 모른다). */
        [[nodiscard]] static expected<void> validateKey( const AutomationStep& step );
        [[nodiscard]] static expected<void> validateCapture( const AutomationStep& step );
    };
} // namespace sw::editor
