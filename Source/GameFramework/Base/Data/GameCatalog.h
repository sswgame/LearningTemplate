/**
 * @file GameCatalog.h
 * @brief id 로 찾는 정의 목록 — 읽은 순서를 지키면서 id 조회는 해시 한 번입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    /**
     * @class GameCatalog
     * @brief `_id`(`hashed_string`) 칸을 가진 정의(`TDef`)의 목록입니다. 작물 · 무기 · 블록 · 코스터 레이아웃 카탈로그가 저마다 적던
     *        "같은 id 면 바꾸고 아니면 뒤에 붙인다" 와 선형 조회를 한 곳에 둡니다.
     * @details 순서는 읽은 순서 그대로입니다(가게 진열 · 블록 번호 · 고르기 순환이 그 순서를 쓴다). 조회는 id → 자리 해시 맵이라 정의가 많아도
     *          `find` 가 선형이 아닙니다. 자리는 바뀌지 않으므로(지우기는 `clear` 뿐) 돌려준 포인터는 다음 `add` 까지 유효합니다.
     */
    template <typename TDef>
    class GameCatalog
    {
    public:
        /** @brief 정의를 더하거나(새 id) 바꿉니다(같은 id). 자리 번호를 돌려줍니다. id 가 비면 −1 입니다. */
        int32 add( const TDef& def )
        {
            if ( def._id.empty() )
                return -1;
            const auto mapIter = _mapIndex.find( def._id );
            if ( mapIter != _mapIndex.end() )
            {
                _listDef[mapIter->second] = def;
                return static_cast<int32>( mapIter->second );
            }
            const uint32 index = static_cast<uint32>( _listDef.size() );
            _listDef.push_back( def );
            _mapIndex[def._id] = index;
            return static_cast<int32>( index );
        }

        /** @brief id 의 정의입니다. 없으면 nullptr 입니다. */
        const TDef* find( const hashed_string& id ) const
        {
            const int32 index = findIndex( id );
            return index >= 0 ? &_listDef[static_cast<size_t>( index )] : nullptr;
        }

        /** @brief id 의 자리입니다. 없으면 −1 입니다. */
        int32 findIndex( const hashed_string& id ) const
        {
            const auto mapIter = _mapIndex.find( id );
            return mapIter != _mapIndex.end() ? static_cast<int32>( mapIter->second ) : -1;
        }

        /** @brief 조건에 맞는 첫 정의입니다(보조 키 — 씨앗 아이템으로 작물 찾기 등). */
        template <typename TPredicate>
        const TDef* findIf( TPredicate&& predicate ) const
        {
            for ( const TDef& def : _listDef )
            {
                if ( predicate( def ) )
                    return &def;
            }
            return nullptr;
        }

        void clear()
        {
            _listDef.clear();
            _mapIndex.clear();
        }

        const vector<TDef>& getAll() const { return _listDef; }
        size_t              getCount() const { return _listDef.size(); }
        bool                isEmpty() const { return _listDef.empty(); }
        const TDef&         getAt( size_t index ) const { return _listDef[index]; }

    private:
        vector<TDef>                         _listDef{};  ///< 읽은 순서
        unordered_map<hashed_string, uint32> _mapIndex{}; ///< id → 자리
    };
} // namespace sw
