/**
 * @file flat_map.h
 * @brief 키 순서로 도는 맵 sw::flat_map 입니다(std::map 과 인터페이스 호환). 기본은 정렬 벡터이고 `SW_ENABLE_STL_CONTAINER` 면 std::map 을 멤버로 듭니다.
 * @details 공통 계약은 두 저장 가운데 약한 쪽이다 — 원소 주소 · 반복자는 삽입 · 삭제마다 무효가 될 수 있고(주소를 오래 쥐어야 하면 값을 `unique_ptr` 로 든다), 삽입은 O(n) 으로 본다.
 *          조회 · 삽입 · 삭제 · 무효화 검사 · 저장 고르기는 `flat_set` 과 함께 쓰는 몸통(`internal::SortedContainerBase`)에 있고, 여기에는 키 → 값 접근(`at` · `operator[]` · `try_emplace`)만 있습니다.
 */
#pragma once
#include "Core/Concurrency/DataRaceDetector.h"
#include "Core/Container/SortedContainerBase.h"
#include "Core/Container/pair.h"
#include "Core/Foundation/StdHeaders.h"

namespace sw
{
    /** @brief 키 순서로 도는 맵입니다. 조회는 O(log n) 이고, 삽입 · 삭제 뒤에는 원소 주소가 무효다. */
    template <typename Key, typename T, typename Compare = std::less<void>, typename Allocator = ContainerAllocator<pair<Key, T>>>
    class flat_map : public internal::SortedContainerBase<Key, T, Compare, Allocator>
    {
        using Base = internal::SortedContainerBase<Key, T, Compare, Allocator>;

    public:
        using key_type       = Key;
        using mapped_type    = T;
        using value_type     = typename Base::value_type;
        using iterator       = typename Base::iterator;
        using const_iterator = typename Base::const_iterator;

        // 생성자는 몸통의 것을 그대로 쓴다(기본 · 복사 · 이동은 암시적으로 몸통의 것을 부른다).
        using Base::Base;

        flat_map& operator=( std::initializer_list<typename Base::insert_type> ilist )
        {
            Base::assign( ilist );
            return *this;
        }

        /** @brief 키에 해당하는 값을 반환합니다. 키가 없으면 std::out_of_range 를 던집니다. */
        template <typename K>
        T& at( const K& key )
        {
            SW_SCOPED_RACE_WRITE();
            const auto it = this->_storage.find( key );
            if ( it == this->_storage.end() )
                throw std::out_of_range( "flat_map::at" );
            return it->second;
        }

        /** @brief 키에 해당하는 값을 반환합니다. 키가 없으면 std::out_of_range 를 던집니다. */
        template <typename K>
        const T& at( const K& key ) const
        {
            SW_SCOPED_RACE_READ();
            const auto it = this->_storage.find( key );
            if ( it == this->_storage.end() )
                throw std::out_of_range( "flat_map::at" );
            return it->second;
        }

        T& operator[]( const Key& key ) { return try_emplace( key ).first->second; }
        T& operator[]( Key&& key ) { return try_emplace( std::move( key ) ).first->second; }

        /** @brief 키가 없을 때만 값을 제자리에서 생성해 넣습니다. */
        template <class... Args>
        pair<iterator, bool> try_emplace( const Key& key, Args&&... args )
        {
            SW_SCOPED_RACE_WRITE();
            return this->wrapInserted( this->_storage.tryEmplace( key, std::forward<Args>( args )... ) );
        }

        /** @brief 키가 없을 때만 값을 제자리에서 생성해 넣습니다. */
        template <class... Args>
        pair<iterator, bool> try_emplace( Key&& key, Args&&... args )
        {
            SW_SCOPED_RACE_WRITE();
            return this->wrapInserted( this->_storage.tryEmplace( std::move( key ), std::forward<Args>( args )... ) );
        }
    };

    template <typename Key, typename T, typename Compare, typename Allocator>
    struct is_bitwise_relocatable<flat_map<Key, T, Compare, Allocator>> : std::bool_constant<internal::SortedStorage<Key, T, Compare, Allocator>::kBitwiseRelocatable>
    {
    };
} // namespace sw
