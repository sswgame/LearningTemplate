/**
 * @file EngineData.h
 * @brief enginedata.xml에서 로드하는 엔진 셸 경로 (Scene / App / FrameRenderer / RHI)
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) EngineData — 씬 폴백 머티리얼 · 셸 InputMap · 파이프라인 · 셰이더 폴백
    //    게임플레이 경로는 GameData, 에디터 도구 경로는 EditorData
    // ------------------------------------------------------------------------------
    /**
     * @brief enginedata.xml 엔진 셸 경로
     * @details 로드는 `XmlSerializer` 가 PROPERTY 그래프로 한다 — 필드를 하나 추가하면 읽기가 저절로
     *          따라온다. 예전엔 필드마다 `takeChildText` 를 손으로 나열해서, 추가할 때 그 줄을
     *          빠뜨리면 값이 조용히 기본값으로 남았다.
     */
    REFLECT()
    struct SW_API EngineData
    {
        REFLECT_BODY();
        PROPERTY()
        string _defaultMaterial{ "engine/materials/defaultmaterial.material" }; ///< 씬 폴백 머티리얼
        PROPERTY()
        string _shellInputMap{ "engine/input/default.input.xml" }; ///< App 셸 InputMap

        PROPERTY()
        string _defaultForwardPipeline{ "engine/pipeline/forwardpipeline.xml" };
        PROPERTY()
        string _defaultDeferredPipeline{ "engine/pipeline/deferredpipeline.xml" };
        PROPERTY()
        string _defaultRenderPass{ "engine/renderpass/defaultrenderpass.xml" };

        PROPERTY()
        string _shaderShadowDepth{ "engine/shaders/shadowdepth.hlsl" };
        PROPERTY()
        string _shaderForwardLit{ "engine/shaders/forwardlit.hlsl" };
        PROPERTY()
        string _shaderGBuffer{ "engine/shaders/gbuffer.hlsl" };
        PROPERTY()
        string _shaderGBufferAlbedo{ "engine/shaders/gbufferalbedo.hlsl" };
        PROPERTY()
        string _shaderGBufferNormal{ "engine/shaders/gbuffernormal.hlsl" };
        PROPERTY()
        string _shaderDeferredLighting{ "engine/shaders/deferredlighting.hlsl" };
        PROPERTY()
        string _shaderPostBloom{ "engine/shaders/postbloom.hlsl" };
        PROPERTY()
        string _shaderPostOutlineCommon{ "common/shaders/postoutline.hlsl" };
        PROPERTY()
        string _shaderPostOutlineEngine{ "engine/shaders/postoutline.hlsl" };
        PROPERTY()
        string _shaderFullscreenBlit{ "engine/shaders/fullscreenblit.hlsl" };
        PROPERTY()
        string _shaderGpuCull{ "engine/shaders/gpucull.hlsl" };
        PROPERTY()
        string _shaderFullscreenTriangle{ "engine/shaders/fullscreentriangle.hlsl" };
        PROPERTY()
        string _shaderSsao{ "engine/shaders/ssao.hlsl" };
        PROPERTY()
        string _shaderTaa{ "engine/shaders/taa.hlsl" };
        PROPERTY()
        string _shaderTonemap{ "engine/shaders/tonemap.hlsl" };

        /** @brief 리소스 경로(XML)에서 엔진 테이블을 로드합니다. 빈 경로는 path::kEngineData입니다. */

        bool loadFromResource( string_view assetRelativePath = {} );
    };
} // namespace sw
