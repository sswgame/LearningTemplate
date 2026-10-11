/**
 * @file LootTable.h
 * @brief 전리품 테이블입니다. 가중치 뽑기, 횟수 범위, "아무것도 없음" 가중치, 항상 주는 항목(확률), 테이블 안의 테이블, 행운 배율을 지원합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Foundation/Data/GameCatalog.h"
#include "GameFramework/Base/Foundation/Data/XMLCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameRandom;
    class ItemStackList;
    class XMLNode;

    /** @brief 테이블의 항목 하나입니다. 아이템이거나 다른 테이블입니다. */
    struct LootEntry
    {
        hashed_string _itemID{};
        hashed_string _tableID{}; ///< 비어 있지 않으면 이 테이블을 굴린다
        float32       _weight{ 1.0f };
        float32       _chance{ 1.0f }; ///< 항상 주는 항목의 확률(0..1)
        int32         _minCount{ 1 };
        int32         _maxCount{ 1 };
    };
} // namespace sw

namespace sw
{
    /** @brief 테이블 하나입니다. */
    struct LootTableDef
    {
        hashed_string     _id{};
        vector<LootEntry> _listEntry{};  ///< 가중치로 하나씩 뽑는다
        vector<LootEntry> _listAlways{}; ///< 굴릴 때마다 확률로 따로 판정한다
        float32           _noneWeight{ 0.0f };
        int32             _minRolls{ 1 };
        int32             _maxRolls{ 1 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class LootCatalog
     * @brief `<LootCatalog><Table id="wolf" rolls="1" rollsMax="2" none="10"><Entry item="pelt" weight="10" min="1" max="2"/><Entry table="gems" weight="1"/>
     *        <Always item="meat" chance="0.75"/></Table></LootCatalog>` 를 읽고 굴립니다.
     * @details 결과는 `ItemStackList` 에 추가하며 같은 아이템은 합친다. 테이블 안의 테이블은 8 단계까지만 따라간다. 서로를 참조하는 테이블에서 멈추기 위해서다.
     *          난수는 부르는 쪽이 넘긴다. 시드가 같으면 같은 전리품이 나오므로 리플레이와 테스트에 쓸 수 있다.
     */
    class SW_GF_API LootCatalog : public XMLCatalog<LootCatalog>
    {
        friend class XMLCatalog<LootCatalog>;

    public:
        static constexpr int32 kMaxDepth = 8;

        void addTable( const LootTableDef& table ) { (void)_catalog.add( table ); }

        /**
         * @brief 테이블을 굴려 @p outItems 에 추가합니다. 테이블이 없으면 false 입니다.
         * @param luck "없음" 가중치를 나누고, 항상 주는 항목의 확률에 곱한다(1 이면 그대로)
         */
        bool roll( const hashed_string& tableID, GameRandom& random, ItemStackList& outItems, float32 luck = 1.0f ) const;
        /** @brief 아이템 하나가 나올 확률의 근삿값입니다. 한 번 굴릴 때를 기준으로 하고, 테이블 안의 테이블과 항상 주는 항목을 포함합니다. 도감과 툴팁에 씁니다. */
        float32 computeDropChance( const hashed_string& tableID, const hashed_string& itemID ) const;

        const LootTableDef* findTable( const hashed_string& id ) const { return _catalog.find( id ); }

    private:
        static constexpr const utf8* kXMLRootName = "LootCatalog"; ///< 루트 원소(`XMLCatalog`)
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );
        void                         rollTable( const LootTableDef& table, GameRandom& random, ItemStackList& outItems, float32 luck, int32 depth ) const;
        void                         giveEntry( const LootEntry& entry, GameRandom& random, ItemStackList& outItems, float32 luck, int32 depth ) const;
        float32                      computeEntryChance( const LootEntry& entry, const hashed_string& itemID, int32 depth ) const;
        float32                      computeTableChance( const LootTableDef& table, const hashed_string& itemID, int32 depth ) const;

        GameCatalog<LootTableDef> _catalog{};
    };
} // namespace sw
