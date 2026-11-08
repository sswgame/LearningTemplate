/**
 * @file GPUTimestampSlotMask.h
 * @brief GPU 타임스탬프 슬롯마다 한 비트(기록됨 · 준비됨)의 집합입니다. 다섯 백엔드와 공통 해석(`RHITimestampFrame`)이 같은 형식을 쓴다.
 * @details 슬롯 수(`constant::render::kMaxGPUTimestampSlot`)가 64 를 넘어 정수 하나로 담을 수 없어 64 비트 단어 배열로 둔다. 비트 연산은 이 형식의 함수로만 한다
 *          (`1u << slot` 은 32 를 넘으면 정의되지 않고, 단어 하나를 가정한 연산은 슬롯을 늘리면 조용히 잘린다).
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/RHIDescriptors.h"

#include <limits>

namespace sw
{
    /**
     * @struct GPUTimestampSlotMask
     * @brief 슬롯 비트 집합입니다(값 형식). 기본값은 빈 집합이다.
     */
    struct GPUTimestampSlotMask
    {
        static constexpr uint32 kBitsPerWord = static_cast<uint32>( std::numeric_limits<uint64>::digits );
        static constexpr uint32 kWordCount   = ( constant::render::kMaxGPUTimestampSlot + kBitsPerWord - 1u ) / kBitsPerWord;

        uint64 _arrWord[kWordCount]{};

        /** @brief 슬롯 @p slotIndex 의 비트가 섰으면 true 입니다. 범위 밖은 false 다. */
        [[nodiscard]] constexpr bool test( uint32 slotIndex ) const
        {
            return slotIndex < constant::render::kMaxGPUTimestampSlot && ( _arrWord[slotIndex / kBitsPerWord] & ( uint64{ 1 } << ( slotIndex % kBitsPerWord ) ) ) != 0;
        }
        /** @brief 슬롯 @p slotIndex 의 비트를 세웁니다. 범위 밖은 무시한다. */
        constexpr void set( uint32 slotIndex )
        {
            if ( slotIndex < constant::render::kMaxGPUTimestampSlot )
                _arrWord[slotIndex / kBitsPerWord] |= uint64{ 1 } << ( slotIndex % kBitsPerWord );
        }
        /** @brief 선 비트가 하나라도 있으면 true 입니다. */
        [[nodiscard]] constexpr bool any() const
        {
            for ( const uint64 word : _arrWord )
            {
                if ( word != 0 )
                    return true;
            }
            return false;
        }
    };
    static_assert( GPUTimestampSlotMask::kWordCount * GPUTimestampSlotMask::kBitsPerWord >= constant::render::kMaxGPUTimestampSlot, "타임스탬프 슬롯 비트 집합이 슬롯 수보다 좁다" );

    /**
     * @struct GPUTimestampSlotAtomicMask
     * @brief 여러 기록 스레드가 함께 세우는 슬롯 비트 집합입니다(DX11 Deferred Context · DX12 병렬 리스트). 단어마다 원자 연산이다.
     * @details 슬롯 하나는 단어 하나에만 있어 `set` 은 단어 하나의 fetch_or 다. `load` 는 단어별로 읽는다 — 프레임 끝에 기록이 모두 끝난 뒤 부르므로 단어 사이의 순서가 필요 없다.
     */
    struct GPUTimestampSlotAtomicMask
    {
        atomic<uint64> _arrWord[GPUTimestampSlotMask::kWordCount]{};

        void set( uint32 slotIndex )
        {
            if ( slotIndex < constant::render::kMaxGPUTimestampSlot )
                _arrWord[slotIndex / GPUTimestampSlotMask::kBitsPerWord].fetch_or( uint64{ 1 } << ( slotIndex % GPUTimestampSlotMask::kBitsPerWord ), std::memory_order_relaxed );
        }
        [[nodiscard]] GPUTimestampSlotMask load() const
        {
            GPUTimestampSlotMask mask;
            for ( uint32 wordIndex = 0; wordIndex < GPUTimestampSlotMask::kWordCount; ++wordIndex )
            {
                mask._arrWord[wordIndex] = _arrWord[wordIndex].load( std::memory_order_relaxed );
            }
            return mask;
        }
        void clear()
        {
            for ( atomic<uint64>& word : _arrWord )
            {
                word.store( 0, std::memory_order_relaxed );
            }
        }
    };
} // namespace sw
