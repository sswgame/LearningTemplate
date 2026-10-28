/**
 * @file SpanAllocator.h
 * @brief 한 버퍼 안의 연속 구간(시작, 개수)을 내주고 돌려받는 구간 할당기입니다. 언리얼 `FSpanAllocator`(GPUScene 의 인스턴스 · 프리미티브 구간)에 해당합니다.
 *
 * [무엇을 푸는가]
 * 정점 풀처럼 "여러 메시의 데이터를 GPU 버퍼 하나에 이어 붙이는" 자료는 집합이 조금만 바뀌어도 전부를 다시 이어 붙이면 버퍼 전체를
 * 다시 올려야 한다(셀 하나를 스트리밍해 들이면 월드 전체가 올라간다). 구간을 따로 내주고 돌려받으면 바뀐 메시의 구간만 올리면 된다.
 *
 * [어떻게]
 * 빈 구간 목록을 시작 오름차순으로 들고 있다. 할당은 가장 잘 맞는 빈 구간(크기가 같거나 가장 작게 남는 것)에서 앞쪽을 잘라 주고,
 * 반납은 그 위치에 끼워 넣으며 앞뒤 빈 구간과 합친다. 그래서 빈 구간끼리 붙어 있는 일이 없다.
 *
 * [미뤄 돌려받기]
 * CPU 가 GPU 버퍼에 직접 쓰는 곳(업로드 힙 정점 버퍼)은 반납한 구간을 곧바로 다시 주면 아직 끝나지 않은 앞 프레임이 읽는 중인 구간을
 * 덮어쓴다. `freeDeferred` 는 구간을 번호(프레임)와 함께 대기열에 두고, `retireDeferred( 번호 )` 가 그 번호 이하를 빈 목록으로 옮긴다.
 * 번호는 부르는 쪽이 정한다(정점 풀은 제출한 프레임 수).
 *
 * @note 잠금은 없다 — 한 스레드(정점 풀은 렌더 스레드)가 소유한다. 같은 구간을 두 번 반납하거나 범위 밖을 반납하는 것은 부르는 쪽의
 *       결함이라 단언으로 알리고 무시한다(Container 는 Log 아래 티어라 로그를 쓰지 않는다).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Defines.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @class SpanAllocator
     * @brief 용량 안에서 연속 구간을 내주고 돌려받습니다. 단위(정점, 원소, 바이트)는 부르는 쪽이 정합니다.
     */
    class SW_API SpanAllocator final
    {
    public:
        /** @brief 할당할 수 없을 때 `allocate` 가 돌려주는 값입니다. */
        static constexpr uint32 kInvalidOffset = invalid_index::kUint32;

        /** @brief 빈 구간 하나입니다. */
        struct Span
        {
            uint32 _offset{ 0 };
            uint32 _count{ 0 };
        };

        /** @brief 용량 0 으로 시작합니다. `reset` 이나 `grow` 로 용량을 줍니다. */
        SpanAllocator();
        /** @brief 용량 전체가 비어 있는 할당기를 만듭니다. */
        explicit SpanAllocator( uint32 capacity );

        /** @brief 내준 구간과 대기 중인 반납을 모두 버리고 용량 전체를 빈 구간 하나로 둡니다. */
        void reset( uint32 capacity );
        /** @brief 용량을 늘려 꼬리에 빈 구간을 더합니다. 줄이지 않습니다(작거나 같으면 아무것도 하지 않습니다). */
        void grow( uint32 newCapacity );

        /**
         * @brief `count` 개짜리 연속 구간을 내줍니다.
         * @return 구간 시작입니다. 맞는 빈 구간이 없으면 `kInvalidOffset` 이며, 그때는 용량을 늘리거나(`grow`) 다시 채워야(`reset`) 합니다.
         */
        [[nodiscard]] uint32 allocate( uint32 count );
        /** @brief 구간을 곧바로 돌려받습니다. 앞뒤 빈 구간과 합칩니다. */
        void free( uint32 offset, uint32 count );
        /** @brief 구간을 `stamp` 번호와 함께 대기열에 둡니다. `retireDeferred( stamp 이상 )` 이 불릴 때까지 다시 내주지 않습니다. */
        void freeDeferred( uint32 offset, uint32 count, uint64 stamp );
        /** @brief 번호가 `stamp` 이하인 대기 구간을 모두 빈 목록으로 옮깁니다. */
        void retireDeferred( uint64 stamp );

        /** @brief 용량(단위 수)을 반환합니다. */
        uint32 getCapacity() const { return _capacity; }
        /** @brief 내준 단위 수의 합을 반환합니다. 대기 중인 반납도 아직 내준 것으로 셉니다. */
        uint32 getAllocatedCount() const { return _allocatedCount; }
        /** @brief 대기 중인 반납 구간 수를 반환합니다. */
        uint32 getDeferredSpanCount() const { return static_cast<uint32>( _listDeferredSpan.size() ); }
        /** @brief 빈 구간 수를 반환합니다. 조각난 정도를 봅니다. */
        uint32 getFreeSpanCount() const { return static_cast<uint32>( _listFreeSpan.size() ); }
        /** @brief 가장 큰 빈 구간의 크기를 반환합니다. 이보다 큰 할당은 실패합니다. */
        uint32 getLargestFreeCount() const;
        /** @brief 내준 구간(대기 중 포함)이 닿는 끝, 곧 마지막 내준 단위 다음 위치를 반환합니다. 비어 있으면 0 입니다. */
        uint32 getUsedEnd() const;

    private:
        /** @brief 대기 중인 반납 구간입니다. */
        struct DeferredSpan
        {
            Span   _span{};
            uint64 _stamp{ 0 };
        };

        /** @brief 빈 목록에 구간을 끼워 넣고 이웃과 합칩니다. 겹치면 단언하고 거절합니다. */
        [[nodiscard]] bool insertFreeSpan( uint32 offset, uint32 count );

        /// @brief 빈 구간들입니다. 시작 오름차순이고 서로 겹치거나 맞닿지 않습니다.
        vector<Span> _listFreeSpan;
        /// @brief 대기 중인 반납입니다. 넣은 순서(번호가 늘어나는 순서라고 가정하지 않습니다).
        vector<DeferredSpan> _listDeferredSpan;
        uint32               _capacity;
        uint32               _allocatedCount;
    };
} // namespace sw
