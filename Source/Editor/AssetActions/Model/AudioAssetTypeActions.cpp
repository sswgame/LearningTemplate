#include "pch.h"

#include "Editor/AssetActions/EditorAssetThumbnailRenderer.h"
#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/Config/EditorThemeKey.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/Asset/EditorAssetTypeActions.h"

#include "Engine/Audio/AudioClip.h"
#include "Engine/Audio/Graph/AudioGraphDef.h"

namespace sw::editor
{
    namespace
    {
        /** @brief 오디오 — 이퀄라이저 막대 썸네일. */
        class AudioAssetTypeActions final : public IEditorAssetTypeActions
        {
        public:
            virtual bool drawThumbnail( ImDrawList* pDrawList, const float2& minPos, const float2& maxPos ) const override
            {
                return EditorAssetThumbnailRenderer::drawAudio( pDrawList, minPos, maxPos );
            }
        };

        const AudioAssetTypeActions s_audioActions{};

        /** @brief 소리 클립과 소리 그래프입니다. 그래프는 클립 캐시가 읽어 둔 것을 오디오 서비스가 다시 굳힌다(엔진 캐시 `AudioData`). */
        const EditorAssetMatchRule kArrAudioRule[] = {
            EditorAssetMatchRule::engineType( AudioClipData::kAssetType ),
            EditorAssetMatchRule::engineType( AudioGraphDef::kAssetType, { AssetReloadRoute::kAnyCacheHoldingPath } ),
        };
    } // namespace

    SW_EDITOR_ASSET_TYPE( Audio, AudioClipData::kAssetType._name.data(), 600, "Audio", "Audio", nullptr, SW_EDITOR_ICON_NAME( "Audio" ), SW_THEME_COLOR_KEY( "Asset", "Audio" ), true, true, kArrAudioRule,
                          SW_COUNT_OF( kArrAudioRule ), nullptr, &s_audioActions );
} // namespace sw::editor
