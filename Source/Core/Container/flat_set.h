/**
 * @file flat_set.h
 * @brief 키 순서로 도는 집합 sw::flat_set 입니다(std::set 과 인터페이스 호환). 기본은 정렬 벡터이고 `SW_ENABLE_STL_CONTAINER` 면 std::set 을 멤버로 듭니다.
 * @details 공통 계약은 두 저장 가운데 약한 쪽이다 — 원소 주소 · 반복자는 삽입 · 삭제마다 무효가 될 수 있고, 삽입은 O(n) 으로 본다.
 *          조회 · 삽입 · 삭제 · 무효화 검사 · 저장 고르기는 `flat_map` 과 함께 쓰는 몸통(`internal::SortedContainerBase`)에 있습니다.
 */
#pragma once
#include "Core/Container/SortedContainerBase.h"
#include "Core/Foundation/StdHeaders.h"

namespace sw
{
    /** @brief 키 순서로 도는 집합입니다. 조회는 O(log n) 이고, 삽입 · 삭제 뒤에는 원소 주소가 무효다. */
    template <typename Key, typename Compare = std::less<void>, typename Allocator = ContainerAllocator<Key>>
    class flat_set : public internal::SortedContainerBase<Key, void, Compare, Allocator>
    {
        using Base = internal::SortedContainerBase<Key, void, Compare, Allocator>;

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

    template <typename Key, typename Compare, typename Allocator>
    struct is_bitwise_relocatable<flat_set<Key, Compare, Allocator>> : std::bool_constant<internal::SortedStorage<Key, void, Compare, Allocator>::kBitwiseRelocatable>
    {
    };
} // namespace sw
