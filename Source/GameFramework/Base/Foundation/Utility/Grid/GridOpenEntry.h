/**
 * @file GridOpenEntry.h
 * @brief 격자 탐색(A* · 다익스트라)이 쓰는 열린 목록 항목과 최소 힙 비교자를 정의합니다.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @struct GridOpenEntry
     * @brief 열린 목록 항목입니다. 비용이 작은 것이 위에 오는 최소 힙으로 쓴다(`std::push_heap` · `std::pop_heap` 에 `isWorse` 를 넘긴다).
     * @tparam TCost 비용 형식(경로 찾기와 흐름장은 float32, 이동 범위는 int32)
     */
    template <typename TCost>
    struct GridOpenEntry
    {
        TCost _cost;
        int32 _index;

        /** @brief @p lhs 가 @p rhs 보다 비싸면 true 입니다(최소 힙 비교자). */
        static bool isWorse( const GridOpenEntry& lhs, const GridOpenEntry& rhs ) { return lhs._cost > rhs._cost; }
    };
} // namespace sw
