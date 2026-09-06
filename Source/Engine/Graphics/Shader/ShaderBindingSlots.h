/**
 * @file ShaderBindingSlots.h
 * @brief 셰이더 바인딩 계약의 C++ 쪽 — `Resource/engine/shaders/bindingslots.hlsli` 를 **그대로 include** 한다.
 * @details 번호는 이 파일에 없다. HLSL 과 C++ 가 같은 파일을 읽으므로 "수동 동기" 가 사라진다.
 *          백엔드 4개는 여기 constexpr 로만 바인딩 위치를 정하고, ShaderBindingContract 가 구운 바이너리의
 *          리플렉션을 이 값과 대조한다 (런타임 로드 시 + 테스트).
 */
#pragma once
#include "Core/Common/Types.h"

#include "bindingslots.hlsli"

namespace sw
{
    namespace shaderslot
    {
        // ------------------------------------------------------------------------------
        // 1) 상수버퍼 (b#, space0)
        // ------------------------------------------------------------------------------
        inline constexpr uint32 kPassConstantBuffer     = SW_SLOT_PASS_CB;
        inline constexpr uint32 kMaterialConstantBuffer = SW_SLOT_MATERIAL_CB;
        inline constexpr uint32 kComputeConstantBuffer  = SW_SLOT_COMPUTE_CB;
        inline constexpr uint32 kMaxConstantBuffer      = SW_MAX_CONSTANT_BUFFER;
        /// @brief DX12 루트 상수 / Vulkan 푸시 상수 — b0 space1.
        inline constexpr uint32 kBindlessConstantRegister = SW_SLOT_BINDLESS_CB;
        inline constexpr uint32 kBindlessConstantSpace    = SW_SPACE_BINDLESS_CB;

        // ------------------------------------------------------------------------------
        // 2) SRV (t#, space0) — 에뮬 백엔드(DX11/GL)의 고정 슬롯
        // ------------------------------------------------------------------------------
        inline constexpr uint32 kEngineTexture0     = SW_SLOT_ENGINE_TEX0;
        inline constexpr uint32 kEngineTextureCount = SW_ENGINE_TEXTURE_SLOT_COUNT;
        inline constexpr uint32 kInstanceBuffer     = SW_SLOT_INSTANCE_SRV;
        /**
         * @brief 비네이티브 bindless 백엔드(DX11/GL)의 머티리얼 텍스처 슬롯 t5..t8.
         * @details 그 두 백엔드는 머티리얼이 준 전역 인덱스를 셰이더에서 풀 수 없다 — DX11 은 SM5.0 이라
         *          리소스 배열 동적 인덱싱이 없고(그건 SM5.1=D3D12), GL 은 SPIR-V 로 먹이므로
         *          ARB_bindless_texture 를 쓸 수 없다. 그래서 엔진이 머티리얼 텍스처를 이 고정 슬롯에
         *          바인딩하고 MaterialCB 에는 서수를 넣는다.
         */
        inline constexpr uint32 kMaterialTexture0     = SW_SLOT_MATERIAL_TEX0;
        inline constexpr uint32 kMaterialTextureCount = SW_MATERIAL_TEXTURE_SLOT_COUNT;
        inline constexpr uint32 kSrvSlotCount         = SW_SRV_SLOT_COUNT;
        inline constexpr uint32 kBindlessTextureSpace = SW_SPACE_BINDLESS_TEX;

        // ------------------------------------------------------------------------------
        // 3) 컴퓨트 / 샘플러
        // ------------------------------------------------------------------------------
        inline constexpr uint32 kComputeSrvSlotCount = SW_COMPUTE_SRV_SLOT_COUNT;
        inline constexpr uint32 kComputeUavSlotCount = SW_COMPUTE_UAV_SLOT_COUNT;
        inline constexpr uint32 kStaticSamplerSpace  = SW_SPACE_STATIC_SAMPLER;
        inline constexpr uint32 kStaticSamplerCount  = SW_STATIC_SAMPLER_COUNT;

        /// @brief Vulkan 디스크립터 세트 번호 — 파이프라인 레이아웃과 HLSL `[[vk::binding(slot, set)]]` 의 정본.
        namespace vk
        {
            inline constexpr uint32 kSetPassCb          = SW_VK_SET_PASS_CB;
            inline constexpr uint32 kSetBindlessTexture = SW_VK_SET_BINDLESS_TEX;
            inline constexpr uint32 kSetStaticSampler   = SW_VK_SET_STATIC_SAMPLER;
            inline constexpr uint32 kSetStorage0        = SW_VK_SET_STORAGE0;
            inline constexpr uint32 kStorageSetCount    = SW_VK_STORAGE_SET_COUNT;
            inline constexpr uint32 kSetUav0            = SW_VK_SET_UAV0;
            inline constexpr uint32 kUavSetCount        = SW_VK_UAV_SET_COUNT;
            inline constexpr uint32 kSetMaterialCb      = SW_VK_SET_MATERIAL_CB;
            inline constexpr uint32 kBoundSetCount      = SW_VK_BOUND_SET_COUNT;
        } // namespace vk

        /// @brief OpenGL SSBO 번호 — u# 는 t# 와 겹치지 않게 SW_GL_UAV_BINDING0 부터 (DXC -fvk-u-shift 값이기도 하다).
        namespace gl
        {
            inline constexpr uint32 kUavBinding0 = SW_GL_UAV_BINDING0;
        } // namespace gl

        /// @brief 엔진 예약 CB 이름 (리플렉션 매칭 키).
        namespace cbname
        {
            inline constexpr const utf8* kPass     = "PassCB";
            inline constexpr const utf8* kMaterial = "MaterialCB";
            inline constexpr const utf8* kCull     = "CullParams";
        } // namespace cbname

        /// @brief 엔진 예약 리소스 이름 (binding.hlsli / gpucull.hlsl 선언과 같아야 한다 — 계약 검증 키).
        namespace resname
        {
            inline constexpr const utf8* kInstances         = "g_SwInstances";
            inline constexpr const utf8* kEngineTexture     = "g_SwSlot";        ///< + 0..3
            inline constexpr const utf8* kMaterialTexture   = "g_SwMaterialTex"; ///< + 0..3
            inline constexpr const utf8* kBindlessTextures  = "g_SwBindlessTex2D";
            inline constexpr const utf8* kLinearWrapSampler = "g_SwSamplerLinearWrap";
            inline constexpr const utf8* kCullInstances     = "g_Instances";
            inline constexpr const utf8* kCullIndirectArgs  = "g_IndirectArgs";
        } // namespace resname

        // 계약 내부 일관성 — 값을 바꾸면 여기서 먼저 걸린다.
        static_assert( kInstanceBuffer == kEngineTexture0 + kEngineTextureCount, "인스턴스 버퍼는 엔진 텍스처 슬롯 바로 다음이어야 한다" );
        static_assert( kMaterialTexture0 == kInstanceBuffer + 1, "머티리얼 텍스처 슬롯은 인스턴스 버퍼 바로 다음이어야 한다" );
        static_assert( SW_SLOT_MATERIAL_TEX1 == SW_SLOT_MATERIAL_TEX0 + 1 && SW_SLOT_MATERIAL_TEX2 == SW_SLOT_MATERIAL_TEX0 + 2 &&
                           SW_SLOT_MATERIAL_TEX3 == SW_SLOT_MATERIAL_TEX0 + 3,
                       "머티리얼 텍스처 슬롯은 연속이어야 한다 (셰이더가 서수로 고른다)" );
        static_assert( kMaterialTexture0 + kMaterialTextureCount == kSrvSlotCount, "SRV 슬롯 총수는 머티리얼 텍스처 마지막 슬롯 + 1 이다" );
        static_assert( SW_SLOT_ENGINE_TEX3 == kEngineTexture0 + kEngineTextureCount - 1, "엔진 텍스처 슬롯은 연속이어야 한다" );
        static_assert( vk::kSetUav0 == vk::kSetStorage0 + 1 && SW_VK_SET_UAV2 == vk::kSetUav0 + vk::kUavSetCount - 1,
                       "Vulkan UAV 세트는 읽기 세트의 뒤쪽을 공유한다" );
        static_assert( vk::kSetStorage0 + vk::kStorageSetCount <= vk::kSetMaterialCb, "Vulkan 스토리지 세트가 MaterialCB 세트와 겹친다" );
        static_assert( vk::kSetMaterialCb + 1 == vk::kBoundSetCount, "MaterialCB 세트는 마지막 세트다" );
        static_assert( SW_SPACE_INSTANCE_SRV == vk::kSetStorage0 && kBindlessTextureSpace == vk::kSetBindlessTexture &&
                           kStaticSamplerSpace == vk::kSetStaticSampler,
                       "HLSL space 와 Vulkan set 은 같은 값이어야 한다" );
        static_assert( kPassConstantBuffer != kMaterialConstantBuffer && kMaterialConstantBuffer < kMaxConstantBuffer, "예약 CB 슬롯 충돌" );
        static_assert( SW_VK_SET_STORAGE3 == vk::kSetStorage0 + vk::kStorageSetCount - 1, "Vulkan 스토리지 세트는 연속이어야 한다" );
        static_assert( gl::kUavBinding0 >= kSrvSlotCount && gl::kUavBinding0 >= kComputeSrvSlotCount, "GL UAV SSBO 번호가 SRV 번호와 겹친다" );
    } // namespace shaderslot
} // namespace sw
