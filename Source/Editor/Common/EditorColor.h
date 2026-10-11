/**
 * @file EditorColor.h
 * @brief 에디터 공통 색 상수입니다. 색 타입은 Core 의 `sw::Color` 이고 ImGui 없이 쓸 수 있습니다(애셋 종류 표 · 테스트).
 * @details ImGui 값(`ImVec4` · `ImU32`)으로 바꾸는 곳은 `EditorColorUtil` 하나입니다. 테마를 따라 바뀌는 색은 `EditorThemeUtil` 의 접근자를 씁니다.
 */
#pragma once
#include "Core/Math/Color.h"

namespace sw::editor
{
    /** @brief 에디터 UI 공통 색 (축 / 강조 / 헤더 / 상태 / 선택) — 테마와 무관하게 고정입니다. */
    namespace style
    {
        inline constexpr Color kAxisX{ 0.85f, 0.25f, 0.25f, 1.0f };
        inline constexpr Color kAxisY{ 0.30f, 0.75f, 0.35f, 1.0f };
        inline constexpr Color kAxisZ{ 0.25f, 0.55f, 0.95f, 1.0f };
        inline constexpr Color kAccent{ 0.27f, 0.57f, 1.0f, 1.0f };
        inline constexpr Color kHeader{ 0.18f, 0.21f, 0.28f, 1.0f };
        inline constexpr Color kOk{ 0.20f, 0.75f, 0.35f, 1.0f };
        inline constexpr Color kWarn{ 0.95f, 0.70f, 0.15f, 1.0f };
        inline constexpr Color kError{ 0.95f, 0.30f, 0.25f, 1.0f };
        inline constexpr Color kToggleActive{ 0.27f, 0.57f, 1.0f, 1.0f };
        inline constexpr Color kToggleInactive{ 0.20f, 0.22f, 0.26f, 0.60f };
        /** @brief 뷰포트 위에 뜨는 오버레이(검색 팝업 · 알림)의 바탕입니다. 알파는 쓰는 곳이 정합니다. */
        inline constexpr Color kOverlayBg{ 0.12f, 0.12f, 0.14f, 1.0f };
        /** @brief 씬 뷰 선택 외곽선(주황)입니다. 테마 강조색(파랑 계열)은 격자 · 카메라 프러스텀과 섞여 쓰지 않습니다. */
        inline constexpr Color kSelectionOutline{ 1.0f, 0.62f, 0.15f, 0.95f };
        /** @brief 2D 미리보기 · 그래프에서 고른 칸 · 지금 프레임을 짚는 선(노랑)입니다. */
        inline constexpr Color kHighlight{ Color::fromBytes( 255, 200, 60 ) };
    } // namespace style
} // namespace sw::editor
