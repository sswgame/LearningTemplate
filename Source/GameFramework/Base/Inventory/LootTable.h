/**
 * @file LootTable.h
 * @brief 전리품 표 — 가중치 뽑기 · 횟수 범위 · "아무것도 없음" 가중치 · 늘 주는 항목(확률) · 표 안의 표 · 행운 배율입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Data/GameCatalog.h"
#include "GameFramework/Base/Data/XmlCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameRandom;
    class ItemStackList;
    class XmlNode;

    /** @brief 표의 항목 하나 — 아이템이거나 다른 표입니다. */
    struct LootEntry
    {
        hashed_string _itemId{};
        hashed_string _tableId{}; ///< 비지 않으면 이 표를 굴린다
        float32       _weight{ 1.0f };
        float32       _chance{ 1.0f }; ///< 늘 주는 항목의 확률(0..1)
        int32         _minCount{ 1 };
        int32         _maxCount{ 1 };
    };
} // namespace sw

namespace sw
{
    /** @brief 표 하나입니다. */
    struct LootTableDef
    {
        hashed_string     _id{};
        vector<LootEntry> _listEntry{};  ///< 가중치로 하나씩 뽑는다
        vector<LootEntry> _listAlways{}; ///< 굴릴 때마다 확률로 따로
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
     * @details 결과는 `ItemStackList` 에 더합니다(같은 아이템은 합친다). 표 안의 표는 8 단계까지만 따라갑니다(서로 부르는 표가 멈추게).
     *          난수는 부르는 쪽이 넘깁니다 — 씨앗이 같으면 같은 전리품입니다(리플레이 · 시험).
     */
    class SW_GF_API LootCatalog : public XmlCatalog<LootCatalog>
    {
        friend class XmlCatalog<LootCatalog>;

    public:
        static constexpr int32 kMaxDepth = 8;

        void addTable( const LootTableDef& table ) { (void)_catalog.add( table ); }

        /**
         * @brief 표를 굴려 @p outItems 에 더합니다. 표가 없으면 false 입니다.
         * @param luck "없음" 가중치를 나누고 늘 주는 항목의 확률에 곱한다(1 = 그대로).
         */
        bool roll( const hashed_string& tableId, GameRandom& random, ItemStackList& outItems, float32 luck = 1.0f ) const;
        /** @brief 아이템 하나가 나올 확률(한 번 굴릴 때 — 표 안의 표 포함, 늘 주는 항목 포함)의 근삿값입니다(도감 · 툴팁). */
        float32 computeDropChance( const hashed_string& tableId, const hashed_string& itemId ) const;

        const LootTableDef* findTable( const hashed_string& id ) const { return _catalog.find( id ); }

    private:
        static constexpr const utf8* kXmlRootName = "LootCatalog"; ///< 루트 원소(`XmlCatalog`)
        uint32                       loadRoot( const XmlNode& root, string_view sourceName );
        void                         rollTable( const LootTableDef& table, GameRandom& random, ItemStackList& outItems, float32 luck, int32 depth ) const;
        void                         giveEntry( const LootEntry& entry, GameRandom& random, ItemStackList& outItems, float32 luck, int32 depth ) const;
        float32                      computeEntryChance( const LootEntry& entry, const hashed_string& itemId, int32 depth ) const;
        float32                      computeTableChance( const LootTableDef& table, const hashed_string& itemId, int32 depth ) const;

        GameCatalog<LootTableDef> _catalog{};
    };
} // namespace sw
