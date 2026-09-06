/**
 * @file ShaderBindingSlots.h
 * @brief 리플렉션 매칭용 예약 CB 이름
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    namespace shaderslot
    {
        /**
         * @brief 엔진 예약 상수 버퍼 슬롯 (space0).
         * @details `Resource/engine/shaders/bindingslots.hlsli` 의 SW_SLOT_* 와 **같은 값이어야
         *          한다.** 예전엔 이 번호가 HLSL 매크로와 Vulkan 백엔드에 각각 박혀 있었고, 그
         *          어긋남이 파이프라인 레이아웃에 없는 디스크립터를 참조하는 크래시로 나타났다.
         */
        inline constexpr uint32 kPassConstantBuffer     = 0;
        inline constexpr uint32 kMaterialConstantBuffer = 1;

        /**
         * @brief 비네이티브 bindless 백엔드(DX11/GL)의 머티리얼 텍스처 슬롯 t5..t8.
         * @details 그 두 백엔드는 머티리얼이 준 전역 인덱스를 셰이더에서 풀 수 없다 — DX11 은 SM5.0 이라
         *          리소스 배열 동적 인덱싱이 없고(그건 SM5.1=D3D12), GL 은 SPIR-V 로 먹이므로
         *          ARB_bindless_texture 를 쓸 수 없다. 그래서 엔진이 머티리얼 텍스처를 이 고정 슬롯에
         *          바인딩하고 MaterialCB 에는 서수를 넣는다. bindingslots.hlsli 의 SW_SLOT_MATERIAL_TEX0 /
         *          SW_MATERIAL_TEXTURE_SLOT_COUNT 와 **같은 값이어야 한다.**
         */
        inline constexpr uint32 kMaterialTexture0     = 5;
        inline constexpr uint32 kMaterialTextureCount = 4;

        /// @brief 엔진 예약 CB 이름 (리플렉션 매칭 키).
        namespace cbname
        {
            inline constexpr const utf8* kMaterial = "MaterialCB";
        } // namespace cbname
    } // namespace shaderslot
} // namespace sw
