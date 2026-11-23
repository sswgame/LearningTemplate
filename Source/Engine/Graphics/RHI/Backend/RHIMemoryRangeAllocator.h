/**
 * @file RHIMemoryRangeAllocator.h
 * @brief 장치 메모리 블록 하나를 구간으로 나눠 주는 장부입니다(오프셋만 — 장치를 모른다). 장치 없이 테스트합니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @class RHIMemoryRangeAllocator
     * @brief 빈 구간을 오프셋 순으로 들고 앞에서부터 맞는 것을 나눠 주며, 놓으면 이웃과 합칩니다(first-fit).
     * @details 잠금은 갖지 않는다 — 블록을 든 쪽이 자기 잠금 아래에서 부른다. 정렬로 남은 앞 조각은 빈 구간으로 남긴다.
     */
    class SW_API RHIMemoryRangeAllocator
    {
    public:
        /** @brief 크기 @p sizeBytes 의 빈 블록으로 되돌립니다. */
        void reset( uint64 sizeBytes );
        /** @brief @p alignment(1 이상)에 맞춘 @p sizeBytes 구간을 떼어 줍니다. 맞는 빈 구간이 없으면 false 입니다. */
        [[nodiscard]] bool allocate( uint64 sizeBytes, uint64 alignment, uint64& outOffset );
        /** @brief 떼어 간 구간을 돌려놓습니다(`allocate` 가 준 오프셋과 같은 크기). */
        void release( uint64 offset, uint64 sizeBytes );
        /** @brief 떼어 간 것이 하나도 없으면 true 입니다. */
        bool isEmpty() const { return _usedBytes == 0; }
        /** @brief 떼어 간 바이트 합입니다. */
        uint64 getUsedBytes() const { return _usedBytes; }
        /** @brief 빈 구간 수입니다(조각남 — 테스트 · 진단). */
        uint32 getFreeRangeCount() const { return static_cast<uint32>( _listFree.size() ); }

    private:
        /** @brief 빈 구간 하나입니다. */
        struct FreeRange
        {
            uint64 _offset{ 0 };
            uint64 _sizeBytes{ 0 };
        };

        vector<FreeRange> _listFree; ///< 오프셋 오름차순, 이웃한 둘은 없다(놓을 때 합친다)
        uint64            _usedBytes{ 0 };
    };
} // namespace sw
