#include "pch.h"

#include "Editor/Popups/QuickLauncherLayout.h"

#include "Core/Math/MathUtil.h"

namespace sw::editor
{
    namespace
    {
        struct QuickLauncherLayoutInternal
        {
            static constexpr float32 kRowHeightScale  = 1.5f;  ///< 줄 높이 = 프레임 높이 × 이것
            static constexpr float32 kDetailColumnPos = 0.55f; ///< 경로 열 = 이름 열 뒤 남은 폭의 이 비율 지점
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    QuickLauncherRowLayout QuickLauncherLayoutUtil::makeRowLayout( float32 availWidth, float32 widestBadgeWidth, float32 frameHeight, float32 textHeight,
                                                                   float32 spacing )
    {
        QuickLauncherRowLayout layout{};
        layout._badgeX         = spacing;
        layout._titleX         = layout._badgeX + widestBadgeWidth + spacing * 2.0f;
        const float32 rest     = MathUtil::max( 0.0f, availWidth - layout._titleX );
        layout._detailX        = layout._titleX + rest * QuickLauncherLayoutInternal::kDetailColumnPos;
        layout._titleClipRight = layout._detailX - spacing;
        layout._rowHeight      = frameHeight * QuickLauncherLayoutInternal::kRowHeightScale;
        layout._textOffsetY    = MathUtil::max( 0.0f, ( layout._rowHeight - textHeight ) * 0.5f );
        return layout;
    }
} // namespace sw::editor
