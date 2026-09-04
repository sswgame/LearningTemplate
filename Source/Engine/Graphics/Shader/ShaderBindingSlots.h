/**
 * @file ShaderBindingSlots.h
 * @brief 범용 바인딩 슬롯 용량 상수 (C++ 측). HLSL 미러: Resource/engine/shaders/bindingslots.hlsli
 * @details 상용 엔진 수준 용량으로 잡아 두고, 실제 사용은 리플렉션이 결정한다 (자동 매칭).
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    namespace shaderslot
    {
        inline constexpr uint32 kMaxConstantBuffer    = 16; ///< b0..b15
        inline constexpr uint32 kRootConstantDwords   = 32; ///< 루트 상수 최대 dword
        inline constexpr uint32 kRootConstantRegister = 0;  ///< b0, space1
        inline constexpr uint32 kRootConstantSpace    = 1;
        inline constexpr uint32 kStaticSamplerCount   = 8;  ///< s0..s7
        inline constexpr uint32 kBindlessHeapSpace    = 1;  ///< DX12 SM6.6 ResourceDescriptorHeap 인덱스 CB space
        inline constexpr uint32 kFallbackSrvCount     = 64; ///< 범용 루트시그 용량 (DX11/GL 에뮬 t#)
        inline constexpr uint32 kFallbackUavCount     = 16;

        /// @brief 정적 샘플러 슬롯 (s0..s7) — HLSL bindingslots.hlsli 와 순서 동기.
        enum class StaticSampler : uint32
        {
            LinearWrap,
            LinearClamp,
            PointWrap,
            PointClamp,
            LinearMirror,
            AnisoWrap,
            ShadowCmp,
            PointBorder,
            Count
        };

        /// @brief 엔진 예약 CB 이름 (리플렉션 매칭 키).
        namespace cbname
        {
            inline constexpr const utf8* kFrame    = "FrameCB";
            inline constexpr const utf8* kView     = "ViewCB";
            inline constexpr const utf8* kPass     = "PassCB";
            inline constexpr const utf8* kDraw     = "DrawCB";
            inline constexpr const utf8* kMaterial = "MaterialCB";
        } // namespace cbname
    } // namespace shaderslot
} // namespace sw
