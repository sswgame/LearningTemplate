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

        /// @brief 엔진 예약 CB 이름 (리플렉션 매칭 키).
        namespace cbname
        {
            inline constexpr const utf8* kMaterial = "MaterialCB";
        } // namespace cbname
    } // namespace shaderslot
} // namespace sw
