/**
 * @file QuickLauncherLayout.h
 * @brief Quick Open(`QuickLauncherPopup`) 결과 줄의 배치입니다 — 종류 표시 · 이름 · 경로 열. ImGui 를 모릅니다(EditorTest 가 시험합니다).
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw::editor
{
    /** @brief 결과 줄 하나의 배치입니다(줄 왼쪽 위 기준 픽셀). */
    struct QuickLauncherRowLayout
    {
        float32 _badgeX{ 0.0f };         ///< 종류 표시(`[Material]`)의 x
        float32 _titleX{ 0.0f };         ///< 이름의 x
        float32 _titleClipRight{ 0.0f }; ///< 이름을 자르는 오른쪽 끝 — 경로 열 앞
        float32 _detailX{ 0.0f };        ///< 경로의 x
        float32 _rowHeight{ 0.0f };      ///< 줄 높이
        float32 _textOffsetY{ 0.0f };    ///< 글자를 줄 가운데에 놓는 y
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct QuickLauncherLayoutUtil
     * @brief 결과 줄의 열 자리를 글자 크기 · 배율에서 정합니다.
     * @details 고정 위치(이름 x 95, 경로 x 300, 줄 높이 34)는 글자 크기 · UI 배율을 보지 않아 `[GameObject]` 와 이름이 겹쳐
     *          `[GameObjectGameCamera` 처럼 보였고, 긴 에셋 이름이 경로 글과 겹쳤다.
     */
    struct QuickLauncherLayoutUtil
    {
        /**
         * @brief 줄 배치를 만듭니다.
         * @param availWidth 줄 폭
         * @param widestBadgeWidth 이번 목록의 종류 표시 가운데 가장 넓은 글의 폭
         * @param frameHeight `ImGui::GetFrameHeight()` — 줄 높이는 그 1.5 배
         * @param textHeight 글자 줄 높이
         * @param spacing 열 사이 여백(`ItemSpacing.x`)
         */
        static QuickLauncherRowLayout makeRowLayout( float32 availWidth, float32 widestBadgeWidth, float32 frameHeight, float32 textHeight, float32 spacing );
    };
} // namespace sw::editor
