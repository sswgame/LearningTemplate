/**
 * @file RenderQualityWatches.cpp
 * @brief 렌더 품질 단계(`gv_renderQuality` — `Config/Engine/RenderQuality.json`)가 닿은 값의 자동화 감시 값입니다(`RenderQuality.*`).
 * @details 그림자 · 텍스처 스트리밍은 프레임마다 엔진 설정의 이 칸을 바로 읽는다(사본이 없다) — 여기서 읽는 값이 다음 프레임이 쓰는 값이다. 개발 장치라 DevTools 에 둔다.
 */
#include "pch.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Foundation/EngineSubsystems.h"

namespace sw
{
    namespace
    {
        [[nodiscard]] bool readTexturePoolMegabytes( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            outValue = static_cast<float64>( engine::getSubsystem<EngineConfig>()._render._textureStreaming._poolMegabytes );
            return true;
        }

        [[nodiscard]] bool readShadowLocalMinTileResolution( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            outValue = static_cast<float64>( engine::getSubsystem<EngineConfig>()._render._shadowLocalMinTileResolution );
            return true;
        }

        [[nodiscard]] bool readShadowLocalCullSlotLimit( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            outValue = static_cast<float64>( engine::getSubsystem<EngineConfig>()._render._shadowLocalCullSlotLimit );
            return true;
        }
    } // namespace

    SW_AUTOMATION_WATCH( renderQualityTexturePoolMegabytes, "RenderQuality.TexturePoolMegabytes", "Texture streaming pool budget (MB) the renderer reads each frame", &readTexturePoolMegabytes );
    SW_AUTOMATION_WATCH( renderQualityShadowLocalMinTileResolution, "RenderQuality.ShadowLocalMinTileResolution",
                         "Smallest local shadow atlas tile side (texels) the renderer reads each frame", &readShadowLocalMinTileResolution );
    SW_AUTOMATION_WATCH( renderQualityShadowLocalCullSlotLimit, "RenderQuality.ShadowLocalCullSlotLimit", "Local shadow lights that get their own culling slot each frame",
                         &readShadowLocalCullSlotLimit );
} // namespace sw
