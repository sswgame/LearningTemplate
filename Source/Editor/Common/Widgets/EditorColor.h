/**
 * @file EditorColor.h
 * @brief 에디터 색 타입과 공통 색 상수입니다. ImGui 없이 쓸 수 있습니다(애셋 종류 표 · 테스트).
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw::editor
{
    /** @brief RGBA 색 (0~1) */
    struct Color4
    {
        float32 _r{ 1.0f };
        float32 _g{ 1.0f };
        float32 _b{ 1.0f };
        float32 _a{ 1.0f };
    };

    /** @brief 에디터 UI 공통 색 (축 / 강조 / 헤더 / 상태) */
    namespace style
    {
        inline constexpr Color4 kAxisX{ 0.85f, 0.25f, 0.25f, 1.0f };
        inline constexpr Color4 kAxisY{ 0.30f, 0.75f, 0.35f, 1.0f };
        inline constexpr Color4 kAxisZ{ 0.25f, 0.55f, 0.95f, 1.0f };
        inline constexpr Color4 kAccent{ 0.27f, 0.57f, 1.0f, 1.0f };
        inline constexpr Color4 kHeader{ 0.18f, 0.21f, 0.28f, 1.0f };
        inline constexpr Color4 kOk{ 0.20f, 0.75f, 0.35f, 1.0f };
        inline constexpr Color4 kWarn{ 0.95f, 0.70f, 0.15f, 1.0f };
        inline constexpr Color4 kError{ 0.95f, 0.30f, 0.25f, 1.0f };
        inline constexpr Color4 kToggleActive{ 0.27f, 0.57f, 1.0f, 1.0f };
        inline constexpr Color4 kToggleInactive{ 0.20f, 0.22f, 0.26f, 0.60f };
    } // namespace style
} // namespace sw::editor
