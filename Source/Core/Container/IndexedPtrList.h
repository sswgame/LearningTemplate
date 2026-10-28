/**
 * @file IndexedPtrList.h
 * @brief 포인터를 중복 없이 모으는 목록입니다. 넣기 · 빼기 · 포함 검사가 모두 O(1) 이고, 순회는 연속 배열을 그대로 돕니다.
 */
#pragma once
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @class IndexedPtrList
     * @brief `vector<T*>` 에 "이미 있으면 넣지 않기" 와 "찾아서 빼기" 를 O(N) 으로 하던 자리를 대신하는 목록입니다.
     * @details `contains` + `push_back` 모양은 원소가 많아지면(씬 로드 때 수천 개) 전체가 O(N^2) 가 됩니다. 이 형식은 위치 색인을 같이 들고 있어
     *          그 모양을 쓸 수 없게 합니다. 빼기는 마지막 원소를 빈 자리로 옮기므로(swap-remove) **순회 순서가 보존되지 않습니다**.
     *          순서가 의미 있는 목록에는 쓰지 않습니다. 잠금은 갖지 않으니 쓰는 쪽이 지킵니다.
     */
    template <typename T>
    class IndexedPtrList
    {
    public:
        /** @brief @p pItem 을 넣습니다. 이미 있으면 아무 일도 없고 false 를 돌려줍니다. */
        [[nodiscard]] bool add( T* pItem )
        {
            if ( _mapIndex.contains( pItem ) )
                return false;
            _mapIndex.try_emplace( pItem, static_cast<uint32>( _listItem.size() ) );
            _listItem.push_back( pItem );
            return true;
        }

        /** @brief @p pItem 을 뺍니다. 없었으면 false 입니다. 마지막 원소가 빈 자리로 옮겨 옵니다. */
        [[nodiscard]] bool remove( T* pItem )
        {
            const typename unordered_map<T*, uint32>::iterator iter = _mapIndex.find( pItem );
            if ( iter == _mapIndex.end() )
                return false;
            const uint32 index = iter->second;
            _mapIndex.erase( pItem );
            T* const pLast = _listItem.back();
            _listItem.pop_back();
            if ( pLast != pItem )
            {
                _listItem[index] = pLast;
                _mapIndex[pLast] = index;
            }
            return true;
        }

        bool contains( T* pItem ) const { return _mapIndex.contains( pItem ); }

        const vector<T*>& items() const { return _listItem; }
        size_t            size() const { return _listItem.size(); }
        bool              empty() const { return _listItem.empty(); }

        void clear()
        {
            _listItem.clear();
            _mapIndex.clear();
        }

    private:
        vector<T*>                _listItem; ///< 순회용 연속 배열
        unordered_map<T*, uint32> _mapIndex; ///< `_listItem` 안의 위치. 두 곳은 항상 같이 바꾼다
    };
} // namespace sw
