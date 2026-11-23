/**
 * @file flat_map.h
 * @brief 정렬 맵(flat_map)입니다. 정렬된 벡터와 이진 검색입니다 — 이름이 `std::map`(노드 트리 · 주소 안정)이 아님을 알린다(C++23 `std::flat_map` 과 같은 보장). 두 구성(`SW_ENABLE_STL_CONTAINER`)에서 같은 구현이고 기본 할당자(`ContainerAllocator`)만 다릅니다.
 * @details 키 순서로 돕니다. 원소 주소 · 반복자는 삽입 · 삭제마다 무효가 될 수 있습니다(주소를 오래 쥐어야 하면 값을 `unique_ptr` 로 든다).
 *          저장소 · 조회 · 삽입 · 삭제는 `flat_set` 과 함께 쓰는 몸통(`internal::SortedVectorBase`)에 있고, 여기에는 키 → 값 접근(`at` · `operator[]` · `try_emplace`)만 있습니다.
 */
#pragma once
#include "Core/Concurrency/DataRaceDetector.h"
#include "Core/Container/SortedVectorBase.h"
#include "Core/Container/pair.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/StdHeaders.h"

namespace sw
{
    /** @brief 정렬된 벡터로 구현한 맵입니다. 조회는 이진 검색이고, 삽입할 때 정렬을 유지합니다. */
    template <typename Key, typename T, typename Compare = std::less<void>, typename Allocator = ContainerAllocator<pair<Key, T>>>
    class flat_map : public internal::SortedVectorBase<pair<Key, T>, Compare, internal::PairKeyLess<pair<Key, T>, Compare>, Allocator>
    {
        using Base      = internal::SortedVectorBase<pair<Key, T>, Compare, internal::PairKeyLess<pair<Key, T>, Compare>, Allocator>;
        using ValueLess = internal::PairKeyLess<pair<Key, T>, Compare>;

    public:
        using key_type       = Key;
        using mapped_type    = T;
        using value_type     = typename Base::value_type;
        using iterator       = typename Base::iterator;
        using const_iterator = typename Base::const_iterator;

        // 생성자는 몸통의 것을 그대로 쓴다(기본 · 복사 · 이동은 암시적으로 몸통의 것을 부른다).
        using Base::Base;

        flat_map& operator=( std::initializer_list<value_type> ilist )
        {
            Base::assign( ilist );
            return *this;
        }

        /** @brief 키에 해당하는 값을 반환합니다. 키가 없으면 std::out_of_range 를 던집니다. */
        template <typename K>
        T& at( const K& key )
        {
            SW_SCOPED_RACE_WRITE();
            auto it = this->lower_bound( key );
            if ( it != this->end() && this->_comp( key, it->first ) == false )
                return it->second;
            throw std::out_of_range( "flat_map::at" );
        }

        /** @brief 키에 해당하는 값을 반환합니다. 키가 없으면 std::out_of_range 를 던집니다. */
        template <typename K>
        const T& at( const K& key ) const
        {
            SW_SCOPED_RACE_READ();
            auto it = this->lower_bound( key );
            if ( it != this->end() && this->_comp( key, it->first ) == false )
                return it->second;
            throw std::out_of_range( "flat_map::at" );
        }

        T& operator[]( const Key& key )
        {
            SW_SCOPED_RACE_WRITE();
            auto it = std::lower_bound( this->_data.begin(), this->_data.end(), key, ValueLess{ this->_comp } );
            if ( it != this->_data.end() && this->_comp( key, it->first ) == false )
                return it->second;
            it = this->_data.insert( it, value_type( key, T() ) );
            return it->second;
        }

        T& operator[]( Key&& key )
        {
            SW_SCOPED_RACE_WRITE();
            auto it = std::lower_bound( this->_data.begin(), this->_data.end(), key, ValueLess{ this->_comp } );
            if ( it != this->_data.end() && this->_comp( key, it->first ) == false )
                return it->second;
            it = this->_data.insert( it, value_type( std::move( key ), T() ) );
            return it->second;
        }

        /** @brief 키가 없을 때만 값을 제자리에서 생성해 넣습니다. */
        template <class... Args>
        pair<iterator, bool> try_emplace( const Key& k, Args&&... args )
        {
            SW_SCOPED_RACE_WRITE();
            auto it = std::lower_bound( this->_data.begin(), this->_data.end(), k, ValueLess{ this->_comp } );
            if ( it != this->_data.end() && this->_comp( k, it->first ) == false )
                return { it, false };
            it = this->_data.emplace( it, std::piecewise_construct, std::forward_as_tuple( k ), std::forward_as_tuple( std::forward<Args>( args )... ) );
            return { it, true };
        }

        /** @brief 키가 없을 때만 값을 제자리에서 생성해 넣습니다. */
        template <class... Args>
        pair<iterator, bool> try_emplace( Key&& k, Args&&... args )
        {
            SW_SCOPED_RACE_WRITE();
            auto it = std::lower_bound( this->_data.begin(), this->_data.end(), k, ValueLess{ this->_comp } );
            if ( it != this->_data.end() && this->_comp( k, it->first ) == false )
                return { it, false };
            it = this->_data.emplace( it, std::piecewise_construct, std::forward_as_tuple( std::move( k ) ), std::forward_as_tuple( std::forward<Args>( args )... ) );
            return { it, true };
        }
    };

    /** @brief 저장소(`vector`)와 비교자뿐이다. */
    template <typename Key, typename T, typename Compare, typename Allocator>
    struct is_bitwise_relocatable<flat_map<Key, T, Compare, Allocator>> : std::bool_constant<is_bitwise_relocatable_v<vector<pair<Key, T>, Allocator>> && std::is_empty_v<Compare>>
    {
    };
} // namespace sw
