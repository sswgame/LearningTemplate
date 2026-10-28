/**
 * @file KartItems.h
 * @brief 카트 아이템을 정의합니다. 아이템 정의(바나나, 녹색 껍질, 빨간 껍질, 부스터, 방어막, 1등 공격)와, 뒤처질수록 강한 아이템이 나오는 순위별 뽑기 테이블을 다룹니다.
 * @details 뽑기는 기반 `LootCatalog` 를 쓴다. 순위 구간마다 테이블 하나(`<RankTable from="1" to="2"><Entry item="banana" weight="30"/>`)를 등록하고,
 *          상자를 먹은 차의 순위로 테이블을 골라 한 번 굴린다. 난수는 호출하는 쪽의 `GameRandom` 이므로 시드가 같으면 같은 아이템이 나온다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Foundation/GameData/GameCatalog.h"
#include "GameFramework/Base/Foundation/GameData/XMLCatalog.h"
#include "GameFramework/Base/Gameplay/Inventory/LootTable.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameRandom;
    class XMLNode;

    /** @brief 아이템이 하는 일입니다. XML 에는 열거자 이름 그대로 적습니다(대소문자 무시). */
    ENUM()
    enum class KartItemKind : uint8
    {
        Banana = 0, ///< 뒤에 놓는 함정이며 멈춰 있다
        GreenShell, ///< 앞으로 곧게 날아간다
        RedShell,   ///< 바로 앞 순위의 차를 따라간다(없으면 곧게 간다)
        Booster,    ///< 부스트를 건다
        Shield,     ///< 다음 한 번의 피격을 막는다
        LeaderShell ///< 1등을 쫓아가 터진다(파란 껍질). 터질 때 둘레의 차도 맞는다
    };

    /** @brief 이름을 열거자로 읽습니다. 모르는 이름이면 false 를 반환합니다. */
    [[nodiscard]] SW_KIT_API bool parseKartItemKind( string_view text, KartItemKind& outKind );
    SW_KIT_API const utf8*        toString( KartItemKind kind );

    /** @brief 아이템 정의 하나입니다. 종류마다 쓰는 값만 읽습니다. */
    REFLECT()
    struct KartItemDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Min = 0.0 )
        float32 _speed{ 45.0f }; ///< 껍질의 속력(m/초)
        PROPERTY( Min = 0.0 )
        float32 _lifetime{ 6.0f }; ///< 껍질과 바나나가 남는 시간이다(0 이면 바나나는 맞을 때까지 남는다)
        PROPERTY( Min = 0.0 )
        float32 _radius{ 1.2f }; ///< 맞는 반지름이다(차의 반지름을 더해서 판정한다)
        PROPERTY( Min = 0.0 )
        float32 _turnRate{ 3.0f }; ///< 유도 껍질의 회전 속도(라디안/초)
        PROPERTY( Name = "spin", Min = 0.0 )
        float32 _spinTime{ 1.2f }; ///< 맞은 차가 도는 시간이다(조작 불가)
        PROPERTY( Min = 0.0 )
        float32 _hitSpeedScale{ 0.3f }; ///< 맞은 순간의 속도에 곱하는 배율
        PROPERTY( Name = "boost", Min = 0.0 )
        float32 _boostTime{ 1.5f }; ///< Booster 의 부스트 시간
        PROPERTY( Name = "shield", Min = 0.0 )
        float32 _shieldTime{ 8.0f }; ///< Shield 가 남는 시간
        PROPERTY( Min = 0.0 )
        float32 _blastRadius{ 0.0f }; ///< 터질 때 함께 맞는 반지름이다(LeaderShell)
        PROPERTY( Required )
        KartItemKind _kind{ KartItemKind::Banana };
        PROPERTY()
        bool _bIgnoresShield{ false }; ///< 방어막을 뚫는다
    };
} // namespace sw

namespace sw
{
    /** @brief `<KartItemCatalog places="8">` 루트 속성의 스키마입니다. */
    REFLECT()
    struct KartItemsRootDef
    {
        REFLECT_BODY();

        PROPERTY( Name = "places", Min = 0 )
        int32 _referencePlaceCount{ 0 }; ///< 순위 표가 기준으로 삼는 참가 수(0 이면 참가 수에 맞춰 순위를 늘이지 않는다)
    };
} // namespace sw

namespace sw
{
    /** @brief `<RankTable from="2" to="4">` 의 범위입니다(둘 다 적는다). */
    REFLECT()
    struct KartRankRangeDef
    {
        REFLECT_BODY();

        PROPERTY( Name = "from", Required, Min = 1 )
        int32 _fromPlace{ 1 };
        PROPERTY( Name = "to", Required, Min = "_fromPlace" )
        int32 _toPlace{ 1 };
    };
} // namespace sw

namespace sw
{
    /** @brief `<Entry item="green" weight="40"/>` 하나입니다. */
    REFLECT()
    struct KartRankEntryDef
    {
        REFLECT_BODY();

        PROPERTY( Name = "item", Required )
        hashed_string _itemID{};
        PROPERTY( Min = 0.0 )
        float32 _weight{ 1.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 순위 구간과 뽑기 테이블의 대응입니다. */
    struct KartRankTable
    {
        hashed_string _tableID{};
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
     * @details `places` 를 적으면 테이블은 그 인원 기준이다. 실제 인원이 다르면 순위를 그 인원에 맞춰 늘려서 고른다(4명 경기의 4등은 8명 테이블의 8등).
     *          어느 구간에도 들지 않는 순위는 가장 가까운 구간의 테이블을 쓴다.
     */
    class SW_KIT_API KartItemCatalog : public GameCatalog<KartItemDef, XMLCatalog<KartItemCatalog>>
    {
        friend class XMLCatalog<KartItemCatalog>;

    public:
        KartItemCatalog();

        /** @brief 순위 구간의 테이블을 추가합니다. @p listEntry 는 기반 전리품 항목(아이템과 가중치)입니다. */
        void addRankTable( int32 fromPlace, int32 toPlace, const vector<LootEntry>& listEntry );
        /** @brief 테이블이 기준으로 삼는 인원을 정합니다. 0 이면 순위를 늘리지 않습니다. */
        void setReferencePlaceCount( int32 placeCount ) { _referencePlaceCount = placeCount; }

        /** @brief @p racerCount 명 중 @p place 등이 상자에서 받을 아이템을 반환합니다. 테이블이 없거나 비어 있으면 nullptr 입니다. */
        const KartItemDef* rollItem( int32 place, int32 racerCount, GameRandom& random ) const;
        /** @brief @p place 등이 @p itemID 를 받을 확률을 계산합니다(도움말과 밸런스 테스트용). */
        float32 computeItemChance( int32 place, int32 racerCount, const hashed_string& itemID ) const;
        /** @brief 그 순위가 쓰는 테이블을 반환합니다. 없으면 nullptr 입니다. */
        const KartRankTable* findRankTable( int32 place, int32 racerCount ) const;

        const LootCatalog& getLootCatalog() const { return _lootCatalog; }

    private:
        static constexpr const utf8* kXMLRootName = "KartItemCatalog"; ///< XML 루트 원소 이름이다(`XMLCatalog` 이 사용한다)
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );
        /** @brief `<RankTable from to>` 하나를 읽습니다. 모르는 아이템 · 빈 표는 오류이고 표를 더하지 않습니다. */
        [[nodiscard]] bool loadRankTable( const XMLNode& node, string_view sourceName );

        LootCatalog           _lootCatalog;
        vector<KartRankTable> _listRankTable;
        int32                 _referencePlaceCount;
    };
} // namespace sw
