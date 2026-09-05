/**
 * @file RHICommandListDefaults.h
 * @brief IRHICommandList / 디바이스 리플레이 공유 기본 구현
 */
#pragma once
#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) 멀티 인디렉트 — countBuffer 없이 drawIndirect 루프
    // ------------------------------------------------------------------------------
    /**
     * @brief 기본 멀티 드로우: drawIndirect를 반복합니다 (countBuffer 무시).
     * @tparam DrawFn void(RHIBufferHandle, uint32)
     */
    template <typename DrawFn>
    void defaultMultiDrawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset, uint32 maxCommandCount,
                                   RHIBufferHandle countBuffer, uint32 countBufferOffset, DrawFn&& drawIndirectFn )
    {
        (void)countBuffer;
        (void)countBufferOffset;
        for ( uint32 commandIndex = 0; commandIndex < maxCommandCount; ++commandIndex )
        {
            drawIndirectFn( argumentBuffer, argumentBufferOffset + commandIndex * static_cast<uint32>( sizeof( RHIDrawIndirectCommand ) ) );
        }
    }

    // ------------------------------------------------------------------------------
    // 2) PassCB/MaterialCB 슬롯 바인딩 — invalid/out-of-range면 skip
    // ------------------------------------------------------------------------------
    /**
     * @brief PassCB/MaterialCB 두 디스크립터 인덱스를 각자의 슬롯에 바인딩합니다 (무효/범위초과면 skip).
     * @details 슬롯 번호 의미와 실제 바인딩 API 호출은 백엔드마다 다르므로 bindFn 으로 주입받습니다 —
     *          여기서는 "바인딩할지 말지"의 가드-디스패치 모양만 공유합니다.
     * @tparam BindFn void(RHIDescriptorIndex index, uint32 slot)
     */
    template <typename BindFn>
    void defaultBindPassAndMaterialCb( RHIDescriptorIndex passCbDescriptorIndex, RHIDescriptorIndex materialCbDescriptorIndex,
                                       size_t registeredCount, uint32 passSlot, uint32 materialSlot, BindFn&& bindFn )
    {
        auto bindSlot = [&]( RHIDescriptorIndex index, uint32 slot )
        {
            if ( index == kInvalidDescriptorIndex || index >= static_cast<RHIDescriptorIndex>( registeredCount ) )
                return;
            bindFn( index, slot );
        };
        bindSlot( passCbDescriptorIndex, passSlot );
        bindSlot( materialCbDescriptorIndex, materialSlot );
    }
} // namespace sw
