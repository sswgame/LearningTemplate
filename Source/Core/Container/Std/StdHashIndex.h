/**
 * @file StdHashIndex.h
 * @brief 밀집 해시 표(`internal::DenseHashTable`)의 std 색인입니다(`SW_ENABLE_STL_CONTAINER`) — std::unordered_multimap( 해시 태그 → 밀집 배열 번호 )를 멤버로 듭니다.
 *        쓰는 쪽은 `Core/Container/DenseHashTable.h` 를 include 합니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

namespace sw::internal
{
    /** @brief 해시 태그로 밀집 배열 번호를 찾는 std::unordered_multimap 입니다. 키를 들지 않아 이종 키로 찾아도 키를 만들지 않습니다. 노드의 `_next` 칸은 쓰지 않습니다. */
    template <typename Node>
    class DenseHashIndex
    {
    public:
        static constexpr bool kBitwiseRelocatable = false; // 노드 컨테이너는 자기 안을 가리킬 수 있다(libstdc++ 의 단일 버킷)

        DenseHashIndex() = default;
        DenseHashIndex( const DenseHashIndex& other )
            : _mapTagToIndex( other._mapTagToIndex )
        {
        }
        DenseHashIndex( DenseHashIndex&& other ) noexcept
            : _mapTagToIndex( std::move( other._mapTagToIndex ) )
        {
            other._mapTagToIndex.clear();
        }
        DenseHashIndex& operator=( const DenseHashIndex& other )
        {
            _mapTagToIndex = other._mapTagToIndex;
            return *this;
        }
        DenseHashIndex& operator=( DenseHashIndex&& other ) noexcept
        {
            if ( this != &other )
            {
                _mapTagToIndex = std::move( other._mapTagToIndex );
                other._mapTagToIndex.clear();
            }
            return *this;
        }

        void clear() noexcept { _mapTagToIndex.clear(); }

        template <typename TMatch>
        uint32 find( const vector<Node>& listNode, uint32 tag, TMatch&& isMatch ) const
        {
            const auto range = _mapTagToIndex.equal_range( tag );
            for ( auto it = range.first; it != range.second; ++it )
            {
                if ( isMatch( listNode.data()[it->second], it->second ) )
                    return it->second;
            }
            return kEmptySlot;
        }

        void reserveForInsert( vector<Node>& ) {}
        void link( vector<Node>& listNode, uint32 index ) { _mapTagToIndex.emplace( listNode[index]._tag, index ); }

        template <typename TMatch>
        uint32 unlink( vector<Node>& listNode, uint32 tag, TMatch&& isMatch )
        {
            const auto range = _mapTagToIndex.equal_range( tag );
            for ( auto it = range.first; it != range.second; ++it )
            {
                const uint32 index = it->second;
                if ( isMatch( std::as_const( listNode[index] ), index ) )
                {
                    _mapTagToIndex.erase( it );
                    return index;
                }
            }
            return kEmptySlot;
        }

        void relocate( vector<Node>&, uint32 tag, uint32 oldIndex, uint32 newIndex )
        {
            const auto range = _mapTagToIndex.equal_range( tag );
            for ( auto it = range.first; it != range.second; ++it )
            {
                if ( it->second == oldIndex )
                {
                    it->second = newIndex;
                    return;
                }
            }
        }

        void rehash( vector<Node>&, size_t count ) { _mapTagToIndex.reserve( count ); }
        void shrink( vector<Node>& ) { _mapTagToIndex.rehash( 0 ); }

    private:
        static constexpr uint32 kEmptySlot = constant::invalid_index::kUint32;

        std::unordered_multimap<uint32, uint32, std::hash<uint32>, std::equal_to<uint32>, ContainerAllocator<std::pair<const uint32, uint32>>> _mapTagToIndex;
    };
} // namespace sw::internal
