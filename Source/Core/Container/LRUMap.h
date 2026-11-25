/**
 * @file LRUMap.h
 * @brief 쓴 순서(앞이 가장 오래 안 씀)를 함께 드는 해시 맵입니다 — 예산 캐시의 퇴출 순서(LRU) 한 벌.
 * @details 언리얼 `TLruCache`(키 집합 + 엔트리의 앞 · 뒤 번호) · Java `LinkedHashMap( accessOrder )` 와 같은 자리다. 얼마를 남길지(바이트 · 개수 · 지금 쓰는 중인지)는 주인이
 *          정하고, 이 형식은 순서와 조회만 든다. 예전에는 쓰는 쪽마다 `std::list` 와 그 반복자를 든 `unordered_map` 을 손으로 짝지었다 — 항목마다 노드 할당이고, 한쪽만 지우면
 *          해제된 노드를 가리켰다.
 *
 *          엔트리는 밀집 배열 하나에 있고 앞 · 뒤는 4 바이트 번호다 — 순서를 새로 해도 할당이 없다. 순회(`begin()`)는 쓴 순서다.
 *          주의: `erase` 는 마지막 엔트리를 빈 자리로 옮긴다(`unordered_map` 과 같은 규칙). 값의 주소 · 반복자는 삽입 · 삭제를 넘겨 쥐지 않는다 — 걸으며 지울 때는
 *          `erase( it )` 가 돌려준 다음 반복자를 쓴다.
 */
#pragma once
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"
#include "Core/Hash/HashUtil.h"

namespace sw
{
    /**
     * @class LRUMap
     * @brief 키 → 값 맵이면서 쓴 순서를 듭니다. 넣기 · 찾기 · 순서 새로 하기가 해시 조회 하나, 지우기가 둘(지운 자리로 옮겨 온 엔트리의 조회까지)입니다.
     */
    template <typename Key, typename T, typename Hash = DefaultHash<Key>>
    class LRUMap
    {
    public:
        /** @brief 순회가 내주는 엔트리 하나입니다. */
        struct Entry
        {
            Key _key;
            T   _value;

        private:
            friend LRUMap;

            Entry( const Key& key, T&& value )
                : _key( key )
                , _value( std::move( value ) )
                , _older{ kNoEntry }
                , _newer{ kNoEntry }
            {
            }

        private:
            uint32 _older; ///< 이보다 먼저 쓴 엔트리의 번호(가장 오래면 `kNoEntry`)
            uint32 _newer; ///< 이보다 나중에 쓴 엔트리의 번호(가장 최근이면 `kNoEntry`)
        };

        /** @brief 쓴 순서(가장 오래 안 쓴 것부터)로 도는 상수 반복자입니다. */
        class const_iterator
        {
        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type        = Entry;
            using difference_type   = ptrdiff_t;
            using pointer           = const Entry*;
            using reference         = const Entry&;

            const_iterator( const LRUMap* pMap, uint32 index )
                : _pMap{ pMap }
                , _index{ index }
            {
            }

            const_iterator& operator++()
            {
                _index = _pMap->_listEntry[_index]._newer;
                return *this;
            }

            reference operator*() const { return _pMap->_listEntry[_index]; }
            pointer   operator->() const { return &_pMap->_listEntry[_index]; }
            bool      operator==( const const_iterator& other ) const { return _index == other._index; }
            bool      operator!=( const const_iterator& other ) const { return _index != other._index; }

        private:
            friend LRUMap;

            const LRUMap* _pMap;
            uint32        _index; ///< 엔트리 번호. 끝이면 `kNoEntry`
        };

    public:
        LRUMap()
            : _listEntry{}
            , _mapKeyToIndex{}
            , _oldest{ kNoEntry }
            , _newest{ kNoEntry }
        {
        }
        LRUMap( const LRUMap& )            = default;
        LRUMap& operator=( const LRUMap& ) = default;
        /** @brief 옮겨 온 쪽은 빈 맵이 됩니다(순서의 처음 · 끝이 사라진 엔트리를 가리키지 않게). */
        LRUMap( LRUMap&& other ) noexcept
            : _listEntry{ std::move( other._listEntry ) }
            , _mapKeyToIndex{ std::move( other._mapKeyToIndex ) }
            , _oldest{ std::exchange( other._oldest, kNoEntry ) }
            , _newest{ std::exchange( other._newest, kNoEntry ) }
        {
        }
        LRUMap& operator=( LRUMap&& other ) noexcept
        {
            if ( this != &other )
            {
                _listEntry     = std::move( other._listEntry );
                _mapKeyToIndex = std::move( other._mapKeyToIndex );
                _oldest        = std::exchange( other._oldest, kNoEntry );
                _newest        = std::exchange( other._newest, kNoEntry );
            }
            return *this;
        }

        bool   empty() const { return _listEntry.empty(); }
        size_t size() const { return _listEntry.size(); }

        /** @brief 쓴 순서의 처음(가장 오래 안 쓴 것)입니다. */
        const_iterator begin() const { return const_iterator( this, _oldest ); }
        const_iterator end() const { return const_iterator( this, kNoEntry ); }

        /** @brief 키의 값을 찾습니다(순서는 그대로). 없으면 nullptr 입니다. */
        T* find( const Key& key )
        {
            const auto found = _mapKeyToIndex.find( key );
            return found != _mapKeyToIndex.end() ? &_listEntry[found->second]._value : nullptr;
        }

        /** @brief 키의 값을 찾습니다(순서는 그대로). 없으면 nullptr 입니다. */
        const T* find( const Key& key ) const
        {
            const auto found = _mapKeyToIndex.find( key );
            return found != _mapKeyToIndex.end() ? &_listEntry[found->second]._value : nullptr;
        }

        /** @brief 키의 값을 찾아 가장 최근에 쓴 것으로 옮깁니다. 없으면 nullptr 입니다. */
        T* touch( const Key& key )
        {
            const auto found = _mapKeyToIndex.find( key );
            if ( found == _mapKeyToIndex.end() )
                return nullptr;
            const uint32 index = found->second;
            markNewest( index );
            return &_listEntry[index]._value;
        }

        /** @brief 키가 없으면 @p value 를 넣고, 있으면 값을 바꿉니다. 어느 쪽이든 가장 최근에 쓴 것이 됩니다. */
        T& insertOrAssign( const Key& key, T value )
        {
            const auto inserted = _mapKeyToIndex.try_emplace( key, static_cast<uint32>( _listEntry.size() ) );
            if ( inserted.second )
            {
                _listEntry.push_back( Entry( key, std::move( value ) ) );
                linkNewest( static_cast<uint32>( _listEntry.size() - 1 ) );
                return _listEntry.back()._value;
            }
            const uint32 index = inserted.first->second;
            markNewest( index );
            _listEntry[index]._value = std::move( value );
            return _listEntry[index]._value;
        }

        /** @brief 키의 엔트리를 지웁니다. 있었으면 true 입니다. */
        bool erase( const Key& key )
        {
            const auto found = _mapKeyToIndex.find( key );
            if ( found == _mapKeyToIndex.end() )
                return false;
            eraseAt( found->second );
            return true;
        }

        /** @brief 반복자가 가리키는 값입니다 — 걸으며 고치거나, 지우기 전에 꺼내 잠금 밖에서 놓을 때 씁니다(순서는 그대로). */
        T& valueAt( const_iterator it ) { return _listEntry[it._index]._value; }

        /** @brief 반복자가 가리키는 엔트리를 지우고 쓴 순서의 다음을 돌려줍니다. */
        const_iterator erase( const_iterator it )
        {
            const uint32 index     = it._index;
            const uint32 lastIndex = static_cast<uint32>( _listEntry.size() - 1 );
            uint32       newer     = _listEntry[index]._newer;
            eraseAt( index );
            if ( newer == lastIndex )
                newer = index; // 다음 엔트리가 배열 끝에 있었다 — 지운 자리로 옮겨 왔다
            return const_iterator( this, newer );
        }

        /** @brief 모두 지웁니다. 용량은 남깁니다. */
        void clear()
        {
            _listEntry.clear();
            _mapKeyToIndex.clear();
            _oldest = kNoEntry;
            _newest = kNoEntry;
        }

    private:
        static constexpr uint32 kNoEntry = constant::invalid_index::kUint32;

        /** @brief 순서에서 뗍니다(배열 자리는 그대로). */
        void unlink( uint32 index )
        {
            const uint32 older = _listEntry[index]._older;
            const uint32 newer = _listEntry[index]._newer;
            relinkNeighbors( older, newer, newer, older );
        }

        /** @brief 순서의 끝(가장 최근)에 겁니다. */
        void linkNewest( uint32 index )
        {
            _listEntry[index]._older = _newest;
            _listEntry[index]._newer = kNoEntry;
            relinkNeighbors( _newest, kNoEntry, index, index );
        }

        void markNewest( uint32 index )
        {
            if ( index == _newest )
                return;
            unlink( index );
            linkNewest( index );
        }

        /** @brief @p older 의 다음을 @p nextOfOlder 로, @p newer 의 앞을 @p previousOfNewer 로 잇습니다. 이웃이 없으면 순서의 처음 · 끝 칸을 바꿉니다. */
        void relinkNeighbors( uint32 older, uint32 newer, uint32 nextOfOlder, uint32 previousOfNewer )
        {
            if ( older != kNoEntry )
                _listEntry[older]._newer = nextOfOlder;
            else
                _oldest = nextOfOlder;
            if ( newer != kNoEntry )
                _listEntry[newer]._older = previousOfNewer;
            else
                _newest = previousOfNewer;
        }

        /** @brief 엔트리를 순서 · 맵 · 배열에서 지우고, 배열 끝 엔트리를 빈 자리로 옮깁니다. */
        void eraseAt( uint32 index )
        {
            unlink( index );
            (void)_mapKeyToIndex.erase( _listEntry[index]._key );
            const uint32 lastIndex = static_cast<uint32>( _listEntry.size() - 1 );
            if ( index != lastIndex )
            {
                _listEntry[index]   = std::move( _listEntry[lastIndex] );
                const Entry& moved  = _listEntry[index];
                const auto   mapped = _mapKeyToIndex.find( moved._key );
                SW_ASSERT( mapped != _mapKeyToIndex.end() );
                mapped->second = index;
                relinkNeighbors( moved._older, moved._newer, index, index );
            }
            _listEntry.pop_back();
        }

    private:
        vector<Entry>                    _listEntry;     ///< 밀집 엔트리(자리 순서는 뜻이 없다)
        unordered_map<Key, uint32, Hash> _mapKeyToIndex; ///< 키 → 엔트리 번호
        uint32                           _oldest;        ///< 가장 오래 안 쓴 엔트리
        uint32                           _newest;        ///< 가장 최근에 쓴 엔트리
    };
} // namespace sw
