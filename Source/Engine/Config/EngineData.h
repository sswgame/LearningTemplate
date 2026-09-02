/**
 * @file EngineData.h
 * @brief enginedata.xml에서 로드하는 엔진 셸 경로 (Scene / App / FrameRenderer / RHI)
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) EngineData — 씬 폴백 머티리얼 · 셸 InputMap · 파이프라인 · 셰이더 폴백
    //    게임플레이 경로는 GameData, 에디터 도구 경로는 EditorData
    // ------------------------------------------------------------------------------
    /** @brief enginedata.xml 엔진 셸 경로 */
    struct SW_API EngineData
    {
        string _defaultMaterial{ "engine/materials/defaultmaterial.material" }; ///< 씬 폴백 머티리얼
        string _shellInputMap{ "engine/input/default.input.xml" };              ///< App 셸 InputMap

        string _defaultForwardPipeline{ "engine/pipeline/forwardpipeline.xml" };
        string _defaultDeferredPipeline{ "engine/pipeline/deferredpipeline.xml" };
        string _defaultRenderPass{ "engine/renderpass/defaultrenderpass.xml" };

        string _shaderShadowDepth{ "engine/shaders/shadowdepth.hlsl" };
        string _shaderForwardLit{ "engine/shaders/forwardlit.hlsl" };
        string _shaderGBuffer{ "engine/shaders/gbuffer.hlsl" };
        string _shaderGBufferAlbedo{ "engine/shaders/gbufferalbedo.hlsl" };
        string _shaderGBufferNormal{ "engine/shaders/gbuffernormal.hlsl" };
        string _shaderDeferredLighting{ "engine/shaders/deferredlighting.hlsl" };
        string _shaderPostBloom{ "engine/shaders/postbloom.hlsl" };
        string _shaderPostOutlineCommon{ "common/shaders/postoutline.hlsl" };
        string _shaderPostOutlineEngine{ "engine/shaders/postoutline.hlsl" };
        string _shaderFullscreenBlit{ "engine/shaders/fullscreenblit.hlsl" };
        string _shaderGpuCull{ "engine/shaders/gpucull.hlsl" };
        string _shaderFullscreenTriangle{ "engine/shaders/fullscreentriangle.hlsl" };
        string _shaderSsao{ "engine/shaders/ssao.hlsl" };
        string _shaderTaa{ "engine/shaders/taa.hlsl" };
        string _shaderTonemap{ "engine/shaders/tonemap.hlsl" };

        /** @brief 리소스 경로(XML)에서 엔진 테이블을 로드합니다. 빈 경로는 path::kEngineData입니다. */

        bool loadFromResource( string_view assetRelativePath = {} );
    };
} // namespace sw
