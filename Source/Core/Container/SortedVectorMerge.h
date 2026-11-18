/**
 * @file SortedVectorMerge.h
 * @brief 정렬 벡터(`map` · `set`)의 범위 삽입 공용 길입니다. 꼬리에 붙인 새 원소를 안정 정렬 · 겹침 거르기 · 한 번 병합합니다(감사 C-9).
 * @details 임시 버퍼는 컨테이너 자신의 할당자로 잡는다 — `std::stable_sort` · `std::inplace_merge` 는 `operator new` 로 버퍼를 잡아
 *          sw 할당자와 할당 추적을 비켜 간다. 할당은 버퍼 하나(원소 수만큼)뿐이고, 병합 결과가 그 버퍼째 저장소가 된다(언리얼 `TArray::StableSort` 도 자기 할당자로 버퍼를 잡는다).
 */
#pragma once
#include "Core/Foundation/StdHeaders.h"

namespace sw::internal
{
    /**
     * @brief data[oldSize, end) 에 붙인 원소를 [0, oldSize) 의 정렬 순서에 합칩니다. 같은 키는 먼저 온 것(있던 원소, 그다음 붙인 순서)만 남습니다.
     * @param data   std::vector 꼴 저장소. [0, oldSize) 는 정렬되어 있고 키가 겹치지 않는다.
     * @param less   원소 둘을 받는 엄격 약순서 비교자.
     */
    template <typename Container, typename Less>
    void mergeAppendedRange( Container& data, const size_t oldSize, const Less& less )
    {
        using value_type       = typename Container::value_type;
        using difference_type  = typename Container::difference_type;
        const size_t tailCount = data.size() - oldSize;
        if ( tailCount == 0 )
            return;

        const auto middle = data.begin() + static_cast<difference_type>( oldSize );

        // 1) 꼬리 안정 정렬 — 짧은 구간은 삽입 정렬(제자리), 그 위는 아래에서 위로 병합 정렬(버퍼는 컨테이너 할당자).
        static constexpr size_t kRunLength = 16;
        for ( size_t runStart = 0; runStart < tailCount; runStart += kRunLength )
        {
            const auto runFirst = middle + static_cast<difference_type>( runStart );
            const auto runLast  = middle + static_cast<difference_type>( std::min( runStart + kRunLength, tailCount ) );
            for ( auto it = runFirst; it != runLast; ++it )
                std::rotate( std::upper_bound( runFirst, it, *it, less ), it, it + 1 ); // upper_bound — 같은 키 뒤에 둬 안정
        }

        Container buffer( data.get_allocator() ); // 필요할 때만 잡는다 — 짧은 꼬리를 정렬된 채 붙이면 할당이 없다
        for ( size_t width = kRunLength; width < tailCount; width *= 2 )
        {
            buffer.clear();
            buffer.reserve( data.size() ); // 병합 결과(3)까지 담을 크기 — 두 번째부터는 아무것도 하지 않는다
            for ( size_t leftStart = 0; leftStart < tailCount; leftStart += width * 2 )
            {
                const auto leftFirst  = middle + static_cast<difference_type>( leftStart );
                const auto rightFirst = middle + static_cast<difference_type>( std::min( leftStart + width, tailCount ) );
                const auto rightLast  = middle + static_cast<difference_type>( std::min( leftStart + width * 2, tailCount ) );
                std::merge( std::make_move_iterator( leftFirst ), std::make_move_iterator( rightFirst ), std::make_move_iterator( rightFirst ),
                            std::make_move_iterator( rightLast ), std::back_inserter( buffer ), less ); // 같으면 왼쪽 먼저 — 안정
            }
            std::move( buffer.begin(), buffer.end(), middle );
        }

        // 2) 정렬된 꼬리에서 같은 키의 첫 원소만 남기고, 앞쪽(있던 원소)에 있는 키를 뺀다.
        auto tailEnd = std::unique( middle, data.end(), [&less]( const value_type& kept, const value_type& next )
        { return less( kept, next ) == false; } );
        tailEnd      = std::remove_if( middle, tailEnd, [&]( const value_type& value )
             { return std::binary_search( data.begin(), middle, value, less ); } );
        data.erase( tailEnd, data.end() );

        // 3) 앞쪽과 꼬리를 버퍼로 한 번 병합하고 버퍼를 저장소로 삼는다. 꼬리가 모두 앞쪽 뒤면(정렬된 입력 붙이기) 병합하지 않는다.
        const auto merged = data.begin() + static_cast<difference_type>( oldSize );
        if ( oldSize == 0 || merged == data.end() || less( *( merged - 1 ), *merged ) )
            return;
        buffer.clear();
        buffer.reserve( data.size() );
        std::merge( std::make_move_iterator( data.begin() ), std::make_move_iterator( merged ), std::make_move_iterator( merged ), std::make_move_iterator( data.end() ),
                    std::back_inserter( buffer ), less );
        data.swap( buffer );
    }
} // namespace sw::internal
