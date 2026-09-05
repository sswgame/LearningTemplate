/**
 * @file FrameRendererUtil.h
 * @brief FrameRenderer 번역 단위 공유 상수·헬퍼
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/String/hashed_string.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    /** @brief FrameRenderer TU 공유 패스/어태치먼트 이름과 헬퍼 */
    struct FrameRendererUtil
    {
        struct PassType
        {
            static constexpr auto kShadow        = "Shadow";
            static constexpr auto kForwardOpaque = "ForwardOpaque";
            static constexpr auto kTransparent   = "Transparent";
            static constexpr auto kGBuffer       = "GBuffer";
            static constexpr auto kGBufferAlbedo = "GBufferAlbedo";
            static constexpr auto kGBufferNormal = "GBufferNormal";
            static constexpr auto kLighting      = "Lighting";
            static constexpr auto kShading       = "Shading";
            static constexpr auto kPostBloom     = "PostBloom";
            static constexpr auto kOutline       = "Outline";
            static constexpr auto kPostOutline   = "PostOutline";
            static constexpr auto kPresent       = "Present";
            static constexpr auto kTaa           = "TAA";
            static constexpr auto kSsao          = "SSAO";
            static constexpr auto kHbao          = "HBAO";
            static constexpr auto kTonemap       = "Tonemap";
            static constexpr auto kDepthPrepass  = "DepthPrepass";
        };

        struct Attachment
        {
            static constexpr auto kSwapchain        = "Swapchain";
            static constexpr auto kSceneColor       = "SceneColor";
            static constexpr auto kSceneDepth       = "SceneDepth";
            static constexpr auto kShadowMap        = "ShadowMap";
            static constexpr auto kGBufferAlbedo    = "GBufferAlbedo";
            static constexpr auto kGBufferNormal    = "GBufferNormal";
            static constexpr auto kLitColor         = "LitColor";
            static constexpr auto kBloomColor       = "BloomColor";
            static constexpr auto kBloomBright      = "BloomBright";
            static constexpr auto kOutlineColor     = "OutlineColor";
            static constexpr auto kTransparentColor = "TransparentColor";
            static constexpr auto kTaaColor         = "TaaColor";
        };

        struct Entry
        {
            static constexpr auto kVSMain = "VSMain";
            static constexpr auto kPSMain = "PSMain";
            static constexpr auto kCSMain = "CSMain";
        };

        static constexpr uint32  kDefaultTransientSize = 1280;
        static constexpr auto    kDefaultMainPassName  = "DefaultMainPass";
        static constexpr float4  kBlackClear           = { 0.0f, 0.0f, 0.0f, 1.0f };
        static constexpr float4  kSceneClear           = { 0.12f, 0.15f, 0.18f, 1.0f };
        static constexpr float4  kDepthClear           = { 1.0f, 0.0f, 0.0f, 0.0f };
        static constexpr float4  kBloomClear           = { 0.0f, 0.0f, 0.0f, 1.0f };
        static constexpr float4  kNormalClear          = { 0.5f, 0.5f, 1.0f, 1.0f };
        static constexpr float32 kDefaultCameraPos[3]  = { 0.0f, 1.2f, 3.2f };

        static bool isDepthFormat( RHIFormat format ) { return format == RHIFormat::D24_UNORM_S8_UINT; }

        static const utf8* pickFirstExisting( const unordered_map<string, RHITextureHandle>& mapAttachment,
                                              std::initializer_list<const utf8*>             listName )
        {
            for ( const utf8* pName : listName )
            {
                if ( mapAttachment.find( pName ) != mapAttachment.end() )
                    return pName;
            }
            return nullptr;
        }
    };

    /**
     * @struct PassConstantNames
     * @brief PassCB/리소스 이름의 hashed_string 캐시.
     * @details hashed_string 생성은 전역 문자열 레지스트리에 intern 하는 작업이다(FNV 해시 →
     *          샤드 공유락 → 조회). 리터럴은 값이 고정이므로 매번 만들 이유가 없는데, 예전엔
     *          `g_World` 를 드로우 호출마다 새로 만들고 있었다. 한 번만 만들어 재사용한다.
     */
    struct PassConstantNames
    {
        hashed_string _lightViewProj{ "g_LightViewProj" };
        hashed_string _viewProj{ "g_ViewProj" };
        hashed_string _world{ "g_World" };
        hashed_string _keyLightDirIntensity{ "g_KeyLightDirIntensity" };
        hashed_string _keyLightColor{ "g_KeyLightColor" };
        hashed_string _shadowParams{ "g_ShadowParams" };
        hashed_string _bloomParams{ "g_BloomParams" };
        hashed_string _outlineColor{ "g_OutlineColor" };
        hashed_string _outlineParams{ "g_OutlineParams" };
        hashed_string _flags{ "g_Flags" };
        hashed_string _instanceBase{ "g_InstanceBase" };
        hashed_string _swInstances{ "SwInstances" };
    };

    /**
     * @brief 프로세스 전역 PassConstantNames 를 돌려줍니다.
     * @details 함수 지역 static 이라 첫 호출 때 한 번만, 스레드 안전하게 만들어진다 — 전역 정적
     *          객체로 두면 문자열 레지스트리보다 먼저 초기화될 수 있다.
     */
    inline const PassConstantNames& passConstantNames()
    {
        static const PassConstantNames s_names{};
        return s_names;
    }
} // namespace sw
