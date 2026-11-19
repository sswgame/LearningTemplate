/**
 * @file GameCatalog.h
 * @brief id 로 찾는 정의 목록입니다. 읽은 순서를 지키면서 id 조회는 해시 한 번으로 합니다.
 */
#pragma once
#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Log/Logger.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Serialization/Serializer/CatalogKey.h"

namespace sw
{
    /**
     * @class GameCatalog
     * @brief `_id`(`hashed_string`) 필드를 가진 정의(`TDef`)의 목록입니다. 작물, 무기, 블록, 코스터 레이아웃 카탈로그가 저마다 적던
     *        "같은 id 면 바꾸고 아니면 뒤에 붙인다" 와 선형 조회를 한 곳에 둡니다.
     * @details 순서는 읽은 순서 그대로입니다(가게 진열, 블록 번호, 고르기 순환이 그 순서를 쓴다). 조회는 id 에서 인덱스로 가는 해시 맵이라 정의가 많아도
     *          `find` 가 선형이 아닙니다. 인덱스는 바뀌지 않으므로(지우기는 `clear` 뿐) 돌려준 포인터는 다음 `add` 까지 유효합니다.
     * @tparam TBase 이 목록 아래에 쌓을 기반입니다(XML 로더 `XMLCatalog<TCatalog>` 등). 두 기반을 나란히 상속(다중 상속)하지 않고 한 줄로 쌓는다 —
     *         `class CropCatalog : public GameCatalog<CropDef, XMLCatalog<CropCatalog>>`. 기반이 필요 없으면 비운다.
     */
    struct GameCatalogNoBase
    {
    };

    template <typename TDef, typename TBase = GameCatalogNoBase>
    class GameCatalog : public TBase
    {
    public:
        /**
         * @brief 정의를 더하고 인덱스를 돌려줍니다. id 가 비거나 이미 있으면 더하지 않고 -1 입니다(겹침은 오류를 남깁니다).
         * @details 앞 정의를 바꾸지 않는다. 데이터의 오타 하나로 다른 정의가 말없이 사라지면 찾을 길이 없다. 다시 읽을 때는 `clear` 뒤에 더한다.
         */
        int32 add( const TDef& def )
        {
            if ( def._id.empty() )
                return constant::invalid_index::kInt32;
            if ( _mapIndex.find( def._id ) != _mapIndex.end() )
            {
                SW_LOG_ERROR_ID( "GameCatalog.DuplicateID", "id '%#' is defined twice - the later definition is ignored", def._id.c_str() );
                return constant::invalid_index::kInt32;
            }
            const uint32 index = static_cast<uint32>( _listDef.size() );
            _listDef.push_back( def );
            _mapIndex[def._id] = index;
            return static_cast<int32>( index );
        }

        /** @brief 이미 있는 정의를 @p def 로 바꿉니다(읽은 뒤 해석한 값을 채울 때 쓴다). 없는 id 면 false 입니다. */
        [[nodiscard]] bool replace( const TDef& def )
        {
            const auto mapIter = _mapIndex.find( def._id );
            if ( mapIter == _mapIndex.end() )
                return false;
            _listDef[mapIter->second] = def;
            return true;
        }

        /** @brief id 의 정의입니다. 없으면 nullptr 입니다. */
        const TDef* find( const hashed_string& id ) const
        {
            const optional<int32> index = findIndex( id );
            return index.has_value() ? &_listDef[static_cast<size_t>( *index )] : nullptr;
        }

        /** @brief 카탈로그 키 칸(`CatalogKey<TDef>`)의 정의입니다. 없으면 nullptr 입니다. 다른 카탈로그의 키는 컴파일되지 않는다. */
        const TDef* find( const CatalogKey<TDef>& key ) const { return find( key.key() ); }

        /**
         * @brief 반드시 있어야 하는 카탈로그 키의 정의입니다 — 시작할 때 칸을 푸는 쪽이 부른다. 비었거나 없으면 오류(`NotFound`, 상세는 id)입니다.
         * @details 쿠커는 "그 종류의 어느 카탈로그에든 있다" 만 본다(`DataCatalogKeyCheck`). 쓰는 쪽이 고른 이 한 벌에 없는 키는 여기서 멈춘다 — 다른 항목으로 대신하지 않는다.
         */
        [[nodiscard]] expected<const TDef*> require( const CatalogKey<TDef>& key ) const
        {
            const TDef* pDef = key.empty() ? nullptr : find( key.key() );
            if ( pDef == nullptr )
                return unexpected( Error{ ErrorKind::NotFound, "the catalog key names no entry in this catalog", key.c_str() } );
            return pDef;
        }

        /** @brief id 의 인덱스입니다. 없으면 비어 있습니다. */
        [[nodiscard]] optional<int32> findIndex( const hashed_string& id ) const
        {
            const auto mapIter = _mapIndex.find( id );
            if ( mapIter == _mapIndex.end() )
                return nullopt;
            return static_cast<int32>( mapIter->second );
        }

        /** @brief 조건에 맞는 첫 정의입니다. 씨앗 아이템으로 작물 찾기처럼 보조 키로 찾을 때 씁니다. */
        template <typename TPredicate>
        const TDef* findIf( TPredicate&& predicate ) const
        {
            return VectorUtil::findIf( _listDef, predicate );
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
        unordered_map<hashed_string, uint32> _mapIndex{}; ///< id 에서 인덱스로 가는 맵
    };
} // namespace sw
