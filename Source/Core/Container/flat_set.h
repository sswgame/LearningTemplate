/**
 * @file flat_set.h
 * @brief 정렬 집합(flat_set)입니다. 정렬된 벡터와 이진 검색입니다 — 이름이 `std::set`(노드 트리 · 주소 안정)이 아님을 알린다(C++23 `std::flat_set` 과 같은 보장). 두 구성(`SW_ENABLE_STL_CONTAINER`)에서 같은 구현이고 기본 할당자(`ContainerAllocator`)만 다릅니다.
 * @details 키 순서로 돕니다. 원소 주소 · 반복자는 삽입 · 삭제마다 무효가 될 수 있습니다.
 *          저장소 · 조회 · 삽입 · 삭제는 `flat_map` 과 함께 쓰는 몸통(`internal::SortedVectorBase`)에 있습니다 — 원소 자신이 키라 비교자를 그대로 넘깁니다.
 */
#pragma once
#include "Core/Container/SortedVectorBase.h"
#include "Core/Container/pair.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/StdHeaders.h"

namespace sw
{
    /** @brief 정렬된 벡터로 구현한 집합입니다. 조회는 이진 검색이고, 삽입할 때 정렬을 유지합니다. */
    template <typename Key, typename Compare = std::less<void>, typename Allocator = ContainerAllocator<Key>>
    class flat_set : public internal::SortedVectorBase<Key, Compare, Compare, Allocator>
    {
        using Base = internal::SortedVectorBase<Key, Compare, Compare, Allocator>;

    public:
        using key_type      = Key;
        using value_type    = typename Base::value_type;
        using value_compare = Compare;

        // 생성자는 몸통의 것을 그대로 쓴다(기본 · 복사 · 이동은 암시적으로 몸통의 것을 부른다).
        using Base::Base;

        flat_set& operator=( std::initializer_list<value_type> ilist )
        {
            Base::assign( ilist );
            return *this;
        }
    };
} // namespace sw
