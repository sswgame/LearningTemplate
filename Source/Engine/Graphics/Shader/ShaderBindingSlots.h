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
        /// @brief 엔진 예약 CB 이름 (리플렉션 매칭 키).
        namespace cbname
        {
            inline constexpr const utf8* kMaterial = "MaterialCB";
        } // namespace cbname
    } // namespace shaderslot
} // namespace sw
