/**
 * @file RenderPassInputSignature.h
 * @brief 풀스크린 패스 타입이 **읽는 입력의 역할**입니다. 로드 시점 검증과 프레임 실행이 같은 테이블을 봅니다.
 * @details 선언이 곧 바인딩이 되려면 "이 패스 타입은 어떤 역할의 입력을 읽는가"가 한 곳에 있어야 합니다. 역할과
 *          계약의 구조는 이 파일에, 타입마다의 계약은 패스 종류 테이블(`RenderPassTypeInfo::_inputContract`)에 있습니다.
 *          계약이 없으면 XML 이 선언한 입력을 아무도 바인딩하지 않아도 오류 없이 지나가고, 그 입력을 만드는 패스(SSAO 등)만 헛돕니다.
 */
#pragma once
#include "Core/Common/Types.h"

#include "Engine/Common/Common.h"
#include "Engine/Renderer/Pipeline/RenderPassAsset.h"

namespace sw
{
    /**
     * @enum RenderPassInputRole
     * @brief 첨부가 패스 안에서 맡는 역할입니다. 셰이더는 역할 이름(`g_<Role>Index`)으로 읽습니다.
     * @details 첨부 **이름**은 파이프라인이 정하고(LitColor, BloomColor …) 역할은 패스가 정합니다. 같은
     *          첨부가 Bloom 에서는 SourceColor 이고 Transparent 에서는 렌더 타깃입니다.
     */
    enum class RenderPassInputRole : uint8
    {
        Invalid = 0,
        SourceColor,      ///< 이 패스가 가공할 컬러 (직전 패스의 출력). 패스당 하나.
        SceneDepth,       ///< 씬 깊이 (깊이 포맷 첨부 전부)
        GBufferAlbedo,    ///< G버퍼 알베도
        GBufferNormal,    ///< G버퍼 노멀
        ShadowMap,        ///< 그림자 깊이
        AmbientOcclusion, ///< SSAO 결과 (AOColor)
        BloomBright,      ///< 반해상도 블룸의 밝은 부분(BloomDownsample 패스 출력). Bloom 패스가 있으면 업샘플해 더하고, 없으면 원본에서 직접 구한다.
        GBufferMaterial,  ///< G버퍼 머티리얼 칸(r 금속도, g 거칠기 — binding.hlsli SurfaceOutput::encodeGbufferMaterial)
        SceneColorCopy,   ///< 반투명 앞에 떠 둔 장면 색 사본(SceneColorCopy 패스의 출력). 물의 굴절이 읽는다(`g_SceneColorCopyIndex`) — 반투명이 그리는 장면 색 자체는 읽으며 쓸 수 없어서다.
        Count,
    };

    /**
     * @enum EmulatedTextureSlot
     * @brief 에뮬 백엔드(DX11 · GL)가 엔진 텍스처를 거는 슬롯입니다. 네이티브 bindless(DX12 · Vulkan)는 쓰지 않습니다.
     * @details 표는 `shaderslot::emulated::kEngineTextureBindings`(ShaderBindingSlots.h) 하나이고 셰이더(컴파일러가 낸
     *          `SW_EMU_SLOT_*` 매크로를 `Bindless::sampleIndex` 가 읽는다)와 실행(`FrameRenderer::commitBindlessTextureBindings`)이 같은 표를 따른다. 한 슬롯을 여러 역할이 나눠 쓰므로 한 패스가 같은
     *          슬롯의 두 역할을 함께 읽으면 하나가 조용히 빠진다 — 파이프라인 로드(`RenderPipelineAsset::validate`)가 오류로 거절한다.
     */
    enum class EmulatedTextureSlot : uint8
    {
        Slot0,           ///< t0: SourceColor · ShadowMap
        Slot1,           ///< t1: GBufferAlbedo · AmbientOcclusion · SceneColorCopy (코드가 거는 Light2DCookieAtlas · TAA 이력도 여기)
        Slot2,           ///< t2: GBufferNormal · BloomBright (코드가 거는 AutoExposure 도 여기)
        Slot3,           ///< t3: SceneDepth
        GBufferMaterial, ///< 자기 슬롯(shaderslot::kEngineGBufferMaterialTexture) — 나눠 쓰지 않는다
        None,            ///< Invalid · Count
    };

    /** @brief 역할이 에뮬 백엔드에서 쓰는 슬롯입니다. */
    EmulatedTextureSlot getEmulatedTextureSlot( RenderPassInputRole role );

    /** @brief 역할의 셰이더 이름을 반환합니다. `g_<Name>Index` 의 `<Name>` 이자 FrameResourceRegistry 의 키입니다. */
    const utf8* getRenderPassInputRoleName( RenderPassInputRole role );

    /** @brief 역할 이름(`getRenderPassInputRoleName` 의 문자열, Invalid 제외)을 역할로 읽습니다. 모르는 이름이면 false 입니다. */
    [[nodiscard]] bool tryParseRenderPassInputRole( string_view roleName, RenderPassInputRole& outRole );

    /**
     * @brief 첨부의 역할을 정합니다. 첨부가 선언한 역할(`RenderPassAttachment::_role`)이 먼저입니다.
     * @details 선언이 없거나 모르는 문자열이면(검증이 오류로 알립니다) 고정 역할 이름(GBufferAlbedo, GBufferNormal, GBufferMaterial, ShadowMap, AOColor, BloomBright, SceneColorCopy)은 그 역할,
     *          그 밖의 깊이 포맷은 SceneDepth, 나머지 컬러는 SourceColor 입니다. ShadowMap 은 깊이 포맷이지만 이름이 먼저입니다.
     */
    RenderPassInputRole resolveRenderPassInputRole( string_view attachmentName, bool bDepthFormat, string_view declaredRole );

    /**
     * @struct RenderPassInputSignature
     * @brief 패스 타입 하나가 읽는 역할 목록입니다. 필수가 빠지면 검증 오류이고, 목록에 없는 역할을 선언해도 오류입니다.
     * @details 패스 타입마다의 계약은 `RenderPassTypeInfo::_inputContract` 에 있습니다.
     */
    struct SW_API RenderPassInputSignature
    {
        static constexpr uint32 kMaxRole = 4;

        RenderPassInputRole _arrRequired[kMaxRole]{};
        uint32              _requiredCount{ 0 };
        RenderPassInputRole _arrOptional[kMaxRole]{};
        uint32              _optionalCount{ 0 };

        /** @brief 이 역할을 읽는지(필수 또는 선택) 확인합니다. */
        bool reads( RenderPassInputRole role ) const;
    };

    /**
     * @brief 풀스크린 패스 타입의 입력 계약을 반환합니다(`RenderPassTypeInfo` 의 항목). 메시 패스(Shadow, GBuffer, ForwardOpaque, Transparent …)는 nullptr 입니다.
     * @details 메시 패스의 입력은 지오메트리 드로우가 정하고(그림자, 인스턴스, 머티리얼), 선언은 그래프 순서용입니다.
     */
    SW_API const RenderPassInputSignature* findRenderPassInputSignature( RenderPassType type );
} // namespace sw
