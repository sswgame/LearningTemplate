/**
 * @file UiActionGlyphSource.h
 * @brief 행동 이름 → 지금 입력 장치의 글리프 글(`[ E ]` · 패드 버튼)입니다 — 리치 텍스트 `[action=이름]`(튜토리얼 힌트 · 입력 힌트)이 씁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
    enum class InputGlyphStyle : uint8;

    class InputManager;
    class InputMap;

    /**
     * @struct UiActionGlyphSource
     * @brief 글 위젯이 측정 · 칠하기 문맥으로 받는 글리프 출처입니다(UiSystem 이 하나 들고 문맥에 포인터를 싣는다).
     * @details 게임 입력 맵(`InputManager::getInputMap`)에 그 행동이 있으면 그것, 없으면 UI 행동 맵(`UI.Accept` …)에서 찾습니다. 장치 종류는
     *          `InputManager::getActiveGlyphStyle`(마지막으로 쓴 장치) — 바뀌면 `UiSystem` 이 위젯마다 `onInputGlyphsChanged` 를 부른다.
     */
    struct SW_API UiActionGlyphSource
    {
        const InputManager* _pInput{ nullptr };
        const InputMap*     _pUiInputMap{ nullptr };

        /** @brief 행동 @p action 의 글리프 글입니다. 어느 맵에도 없으면 `[ ? ]` 입니다. */
        string findGlyph( string_view action ) const;
        /** @brief 지금 장치 종류입니다(입력이 없으면 키보드 · 마우스). */
        InputGlyphStyle getStyle() const;
    };
} // namespace sw
