#include "pch.h"

#include "Core/Math/VectorMath.h"

#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorIconGlyphs.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/Asset/EditorAssetTypeActions.h"

#include "Engine/Audio/AudioClip.h"
#include "Engine/Audio/Graph/AudioGraphDef.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct AudioAssetTypeActionsInternal
        {
            static constexpr Color kWaveform{ Color::fromBytes( 80, 210, 220, 240 ) };
        };

        /** @brief 오디오 — 이퀄라이저 막대 썸네일. */
        class AudioAssetTypeActions final : public IEditorAssetTypeActions
        {
        public:
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
                                              EditorColorUtil::toImU32( AudioAssetTypeActionsInternal::kWaveform ), 1.0f );
                }
                return true;
            }
        };

        const AudioAssetTypeActions s_audioActions{};

        /** @brief 소리 클립과 소리 그래프입니다. 그래프는 클립 캐시가 읽어 둔 것을 오디오 서비스가 다시 굳힌다(엔진 캐시 `AudioData`). */
        const EditorAssetMatchRule kArrAudioRule[] = {
            EditorAssetMatchRule::engineType( AudioClipData::kAssetType ),
            EditorAssetMatchRule::engineType( AudioGraphDef::kAssetType, { AssetReloadRoute::kAnyCacheHoldingPath } ),
        };
    } // namespace

    SW_EDITOR_ASSET_TYPE( Audio, AudioClipData::kAssetType._name.data(), 600, "Audio", "Audio", nullptr, editoricon::kAudio, { 0.95f, 0.85f, 0.25f, 1.0f }, false, true, true, kArrAudioRule,
                          SW_COUNT_OF( kArrAudioRule ), nullptr, &s_audioActions );
} // namespace sw::editor
