/**
 * @file KartItems.h
 * @brief 카트 아이템 — 아이템 정의(바나나 · 녹색 껍질 · 빨간 껍질 · 부스터 · 방어막 · 1 등 공격)와 순위별 뽑기 표(뒤처질수록 강한 아이템)입니다.
 * @details 뽑기는 기반 `LootCatalog` 를 씁니다 — 순위 구간마다 표 하나(`<RankTable from="1" to="2"><Entry item="banana" weight="30"/>`)를 등록하고,
 *          상자를 먹은 차의 순위로 표를 골라 한 번 굴립니다. 난수는 부르는 쪽의 `GameRandom` 이라 씨앗이 같으면 같은 아이템입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Data/GameCatalog.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Inventory/LootTable.h"

namespace sw
{
    class GameRandom;
    class XmlNode;

    /** @brief 아이템이 하는 일입니다. XML 은 열거자 이름 그대로 적습니다(대소문자 무시). */
    enum class KartItemKind : uint8
    {
        Banana = 0, ///< 뒤에 놓는 함정(멈춰 있다)
        GreenShell, ///< 앞으로 곧게 날아간다
        RedShell,   ///< 바로 앞 순위의 차를 따라간다(없으면 곧게)
        Booster,    ///< 부스트
        Shield,     ///< 다음 한 번의 맞음을 막는다
        LeaderShell ///< 1 등을 쫓아가 터진다(파란 껍질) — 터질 때 둘레도 맞는다
    };

    /** @brief 이름을 열거로 읽습니다. 모르는 이름이면 false 입니다. */
    [[nodiscard]] SW_GF_API bool parseKartItemKind( string_view text, KartItemKind& outKind );
    SW_GF_API const utf8*        toString( KartItemKind kind );

    /** @brief 아이템 정의 하나입니다. 종류마다 쓰는 값만 읽습니다. */
    struct KartItemDef
    {
        hashed_string _id{};
        float32       _speed{ 45.0f };        ///< 껍질의 속력(m/초)
        float32       _lifetime{ 6.0f };      ///< 껍질 · 바나나가 남는 시간(0 = 바나나는 맞을 때까지)
        float32       _radius{ 1.2f };        ///< 맞는 반지름(차의 반지름을 더해 본다)
        float32       _turnRate{ 3.0f };      ///< 유도 껍질의 회전 속도(라디안/초)
        float32       _spinTime{ 1.2f };      ///< 맞은 차가 도는 시간(조작 불가)
        float32       _hitSpeedScale{ 0.3f }; ///< 맞은 순간 속도에 곱한다
        float32       _boostTime{ 1.5f };     ///< Booster 의 부스트 시간
        float32       _shieldTime{ 8.0f };    ///< Shield 가 남는 시간
        float32       _blastRadius{ 0.0f };   ///< 터질 때 함께 맞는 반지름(LeaderShell)
        KartItemKind  _kind{ KartItemKind::Banana };
        uint8         _bIgnoresShield{ SW_FALSE }; ///< 방어막을 뚫는다
    };
} // namespace sw

namespace sw
{
    /** @brief 순위 구간 → 뽑기 표입니다. */
    struct KartRankTable
    {
        hashed_string _tableId{};
        int32         _fromPlace{ 1 };
        int32         _toPlace{ 1 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class KartItemCatalog
     * @brief `<KartItemCatalog places="8"><Item id="banana" kind="Banana" spin="1.2"/>...<RankTable from="1" to="1"><Entry item="banana" weight="40"/></RankTable>
     *        </KartItemCatalog>` 를 읽습니다.
     * @details `places` 를 적으면 표는 그 인원 기준이고, 실제 인원이 다르면 순위를 그 인원으로 늘려 고릅니다(4 명 경기의 4 등 = 8 명 표의 8 등).
     *          어느 구간에도 들지 않는 순위는 가장 가까운 구간의 표를 씁니다.
     */
    class SW_GF_API KartItemCatalog
    {
    public:
        KartItemCatalog();

        [[nodiscard]] bool loadFromResource( string_view path );
        [[nodiscard]] bool loadFromXmlText( string_view xmlText, string_view sourceName = {} );
        void               addItem( const KartItemDef& def ) { (void)_catalog.add( def ); }
        /** @brief 순위 구간의 표를 더합니다. @p listEntry 는 기반 전리품 항목(아이템 · 가중치)입니다. */
        void addRankTable( int32 fromPlace, int32 toPlace, const vector<LootEntry>& listEntry );
        /** @brief 표가 기준으로 삼는 인원입니다(0 = 늘리지 않는다). */
        void setReferencePlaceCount( int32 placeCount ) { _referencePlaceCount = placeCount; }

        /** @brief @p racerCount 명 중 @p place 등이 상자에서 받을 아이템입니다. 표가 없거나 비면 nullptr 입니다. */
        const KartItemDef* rollItem( int32 place, int32 racerCount, GameRandom& random ) const;
        /** @brief @p place 등이 @p itemId 를 받을 확률입니다(도움말 · 밸런스 시험). */
        float32 computeItemChance( int32 place, int32 racerCount, const hashed_string& itemId ) const;
        /** @brief 순위가 쓰는 표입니다. 없으면 nullptr 입니다. */
        const KartRankTable* findRankTable( int32 place, int32 racerCount ) const;

        const KartItemDef*              findItem( const hashed_string& id ) const { return _catalog.find( id ); }
        const GameCatalog<KartItemDef>& getItems() const { return _catalog; }
        const LootCatalog&              getLootCatalog() const { return _lootCatalog; }

    private:
        uint32 loadRoot( const XmlNode& root, string_view sourceName );

        GameCatalog<KartItemDef> _catalog;
        LootCatalog              _lootCatalog;
        vector<KartRankTable>    _listRankTable;
        int32                    _referencePlaceCount;
    };
} // namespace sw
