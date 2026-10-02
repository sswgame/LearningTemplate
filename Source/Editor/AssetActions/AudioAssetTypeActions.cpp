#include "pch.h"

#include "Core/Math/VectorMath.h"

#include "Editor/Common/Workspace/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorAssetTypeActions.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        /** @brief 오디오 — 이퀄라이저 막대 썸네일. */
        class AudioAssetTypeActions final : public IEditorAssetTypeActions
        {
        public:
            virtual EditorAssetKind getKind() const override { return EditorAssetKind::Audio; }

            virtual bool drawThumbnail( ImDrawList* pDrawList, const float2& minPos, const float2& maxPos ) const override
            {
                constexpr int32   kBarCount               = 5;
                constexpr float32 arrBarHeight[kBarCount] = { 0.25f, 0.55f, 0.95f, 0.65f, 0.35f };
                constexpr float32 kBarWidth               = 3.0f;
                constexpr float32 kBarGap                 = 3.0f;
                constexpr float32 kTotalWidth             = kBarCount * kBarWidth + ( kBarCount - 1 ) * kBarGap;

                const float32 height  = maxPos._y - minPos._y;
                const float32 centerX = minPos._x + ( maxPos._x - minPos._x ) * 0.5f;
                const float32 centerY = minPos._y + height * 0.5f;
                const float32 startX  = centerX - kTotalWidth * 0.5f;
                for ( int32 barIndex = 0; barIndex < kBarCount; ++barIndex )
                {
                    const float32 barX      = startX + static_cast<float32>( barIndex ) * ( kBarWidth + kBarGap );
                    const float32 barHeight = height * 0.45f * arrBarHeight[barIndex];
                    pDrawList->AddRectFilled( ImVec2( barX, centerY - barHeight * 0.5f ), ImVec2( barX + kBarWidth, centerY + barHeight * 0.5f ),
                                              IM_COL32( 80, 210, 220, 240 ), 1.0f );
                }
                return true;
            }
        };

        const EditorAssetTypeActionsRegistrar<AudioAssetTypeActions> s_audioAssetTypeActionsRegistrar{};
    } // namespace
} // namespace sw::editor
