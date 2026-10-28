/**
 * @file BrCatalog.h
 * @brief 배틀로얄의 데이터입니다. 자기장 단계, 비행기, 낙하, 체력과 부활, 방어구 등급, 가방 등급, 지점별 전리품 테이블, 보급 상자를 담습니다.
 * @details 정의 구조체는 리플렉션 스키마다 — 기본값은 초기값, 범위는 `PROPERTY( Min · Max )`, 필수는 `Required` 이고 `GameDataXML::readFields` 가 읽는다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Foundation/GameData/GameCatalog.h"
#include "GameFramework/Base/Foundation/GameData/XMLCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XMLNode;

    /** @brief 자기장 한 단계입니다. 단계가 시작되면 다음 원이 드러나고, 기다린 뒤 줄어듭니다. 시간 단위는 초입니다. */
    REFLECT()
    struct BrZonePhaseDef
    {
        REFLECT_BODY();

        PROPERTY( Name = "wait", Min = 0.0 )
        float32 _waitTime{ 60.0f }; ///< 다음 원이 드러난 뒤 줄기 시작할 때까지의 시간
        PROPERTY( Name = "shrink", Min = 0.0 )
        float32 _shrinkTime{ 30.0f }; ///< 지금 원에서 다음 원으로 보간하는 시간
        PROPERTY( Name = "ratio", Min = 0.0, Max = 1.0 )
        float32 _radiusRatio{ 0.5f }; ///< 다음 원 반지름은 지금 원 반지름에 이 비율을 곱한 값이다
        PROPERTY( Name = "damage", Min = 0.0 )
        float32 _damagePerSecond{ 1.0f }; ///< 이 단계 동안 원 밖에서 받는 초당 피해(방어구를 무시한다)
    };
} // namespace sw

namespace sw
{
    /** @brief 자기장 설정입니다. */
    REFLECT()
    struct BrZoneDef
    {
        REFLECT_BODY();

        vector<BrZonePhaseDef> _listPhase{};
        PROPERTY( Min = 0.0 )
        float32 _startRadius{ 0.0f }; ///< 첫 원 반지름(0이면 맵을 덮는 반 대각선)
        PROPERTY( Min = 1 )
        int32 _centerAttempts{ 32 }; ///< 다음 원 중심을 고르는 시도 횟수(모두 실패하면 지금 중심을 쓴다)
    };
} // namespace sw

namespace sw
{
    /** @brief 비행기 설정입니다. 거리는 m, 속도는 m/s 입니다. */
    REFLECT()
    struct BrFlightDef
    {
        REFLECT_BODY();

        PROPERTY( Min = 1.0 )
        float32 _speed{ 100.0f };
        PROPERTY( Min = 1.0 )
        float32 _altitude{ 600.0f };
        PROPERTY( Name = "jumpStart", Min = 0.0, Max = 1.0 )
        float32 _jumpStartRatio{ 0.05f }; ///< 경로 길이의 이 비율부터 뛰어내릴 수 있다
        PROPERTY( Name = "jumpEnd", Min = "_jumpStartRatio", Max = 1.0 )
        float32 _jumpEndRatio{ 0.95f }; ///< 이 비율에서 모두 강제로 내린다
        PROPERTY( Name = "offset", Min = 0.0, Max = 0.95 )
        float32 _offsetRatio{ 0.6f }; ///< 경로가 맵 중심에서 비켜나는 최대 비율(반 변 기준)
    };
} // namespace sw

namespace sw
{
    /** @brief 낙하 설정입니다. 속도는 m/s 입니다. */
    REFLECT()
    struct BrFallDef
    {
        REFLECT_BODY();

        PROPERTY( Min = 0.1 )
        float32 _freeFallSpeed{ 50.0f }; ///< 자유 낙하의 수직 속도
        PROPERTY( Name = "freeFallHorizontal", Min = 0.0 )
        float32 _freeFallHorizontalSpeed{ 20.0f }; ///< 자유 낙하 중 최대 수평 속도
        PROPERTY( Min = 0.1 )
        float32 _parachuteSpeed{ 5.0f }; ///< 낙하산의 수직 속도
        PROPERTY( Name = "parachuteHorizontal", Min = 0.0 )
        float32 _parachuteHorizontalSpeed{ 8.0f }; ///< 낙하산의 최대 수평 속도
        PROPERTY( Min = 0.0 )
        float32 _autoOpenHeight{ 100.0f }; ///< 땅 위 이 높이에서 낙하산이 저절로 펴진다
    };
} // namespace sw

namespace sw
{
    /** @brief 체력, 기절, 부활 설정입니다. */
    REFLECT()
    struct BrPlayerDef
    {
        REFLECT_BODY();

        PROPERTY( Name = "health", Min = 1.0 )
        float32 _maxHealth{ 100.0f };
        PROPERTY( Min = 0.0 )
        float32 _downedHealth{ 100.0f };
        PROPERTY( Name = "bleedout", Min = 0.0 )
        float32 _bleedoutRate{ 10.0f };
        PROPERTY( Min = 0.1 )
        float32 _reviveTime{ 10.0f };
        PROPERTY( Min = 0.01, Max = 1.0 )
        float32 _reviveHealthRatio{ 0.1f };
        PROPERTY( Name = "carryWeight", Min = 0.0 )
        float32 _baseCarryWeight{ 20.0f }; ///< 가방 없이 들 수 있는 무게
        PROPERTY( Name = "slots", Min = 1 )
        int32 _slotCount{ 40 }; ///< 인벤토리 슬롯 수
        PROPERTY( Name = "revivers", Min = 1 )
        int32 _maxRevivers{ 2 }; ///< 한 사람을 함께 살릴 수 있는 인원 수
    };
} // namespace sw

namespace sw
{
    /** @brief 방어구(헬멧, 조끼) 한 종류입니다. id 는 아이템 id 와 같습니다. */
    REFLECT()
    struct BrArmorDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Required )
        hashed_string _slot{}; ///< "Helmet"(머리) 또는 "Vest"(몸)
        PROPERTY( Min = 0.0, Max = 0.95 )
        float32 _reduction{ 0.3f };
        PROPERTY( Min = 1.0 )
        float32 _durability{ 100.0f };
        PROPERTY( Min = 1, Max = 9 )
        int32 _tier{ 1 };
    };
} // namespace sw

namespace sw
{
    /** @brief 가방 한 종류입니다. id 는 아이템 id 와 같습니다. */
    REFLECT()
    struct BrBackpackDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Min = 0.0 )
        float32 _capacity{ 100.0f }; ///< 기본 무게 한도에 더하는 양
        PROPERTY( Min = 1, Max = 9 )
        int32 _tier{ 1 };
    };
} // namespace sw

namespace sw
{
    /** @brief 지점 종류(건물, 창고, 군 기지)마다의 전리품 규칙입니다. */
    REFLECT()
    struct BrLootSpotDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Name = "table", Required )
        hashed_string _tableID{}; ///< `LootCatalog` 의 테이블
        PROPERTY( Min = 0.0, Max = 1.0 )
        float32 _chance{ 1.0f };
        PROPERTY( Name = "rolls", Min = 0 )
        int32 _minRolls{ 1 };
        PROPERTY( Name = "rollsMax", Min = "_minRolls" )
        int32 _maxRolls{ 1 }; ///< `rolls` 보다 크게 굴리려면 함께 적는다(빠지면 1 이라 `rolls` 가 1 보다 크면 범위 오류다)
    };
} // namespace sw

namespace sw
{
    /** @brief 보급 상자 설정입니다. */
    REFLECT()
    struct BrSupplyDropDef
    {
        REFLECT_BODY();

        vector<float32> _listTime{}; ///< 판이 시작된 뒤 상자가 떨어지는 시각(초, 오름차순, 속성 `times`)
        PROPERTY( Name = "table", Required )
        hashed_string _tableID{};
    };
} // namespace sw

namespace sw
{
    /** @brief 루트 `<BattleRoyaleCatalog>` 의 속성입니다. */
    REFLECT()
    struct BrCatalogRootDef
    {
        REFLECT_BODY();

        PROPERTY( Min = 1.0 )
        float32 _mapSize{ 1000.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @class BrCatalog
     * @brief 다음 형식의 XML 을 읽는 카탈로그입니다. `<BattleRoyaleCatalog mapSize="1000"><Player health="100" .../><Flight .../><Fall .../><Zone startRadius="0"><Phase wait="60" shrink="30" ratio="0.5" damage="1"/></Zone>
     *        <Armor id="helmet1" slot="Helmet" tier="1" reduction="0.3" durability="80"/><Backpack id="bag1" tier="1" capacity="150"/>
     *        <LootSpot id="house" table="house" chance="0.8" rolls="1" rollsMax="3"/><SupplyDrop table="airdrop" times="90,180"/></BattleRoyaleCatalog>`
     * @details 스키마를 어긴 원소는 넣지 않고 오류로 남긴다. 설정 원소(Player · Flight · Fall · Zone · SupplyDrop)는 오류가 없을 때만 바꾼다.
     */
    class SW_KIT_API BrCatalog : public XMLCatalog<BrCatalog>
    {
        friend class XMLCatalog<BrCatalog>;

    public:
        BrCatalog();

        const BrArmorDef*      findArmor( const hashed_string& id ) const { return _armorCatalog.find( id ); }
        const BrBackpackDef*   findBackpack( const hashed_string& id ) const { return _backpackCatalog.find( id ); }
        const BrLootSpotDef*   findLootSpot( const hashed_string& id ) const { return _lootSpotCatalog.find( id ); }
        const BrZoneDef&       getZoneSettings() const { return _zone; }
        const BrFlightDef&     getFlightSettings() const { return _flight; }
        const BrFallDef&       getFallSettings() const { return _fall; }
        const BrPlayerDef&     getPlayerSettings() const { return _player; }
        const BrSupplyDropDef& getSupplyDropSettings() const { return _supplyDrop; }
        float32                getMapSize() const { return _mapSize; }

    private:
        static constexpr const utf8* kXMLRootName = "BattleRoyaleCatalog"; ///< 루트 원소(`XMLCatalog`)
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );
        [[nodiscard]] bool           loadElement( const XMLNode& node, string_view name, string_view sourceName );
        [[nodiscard]] bool           loadZone( const XMLNode& node, string_view sourceName );
        [[nodiscard]] bool           loadSupplyDrop( const XMLNode& node, string_view sourceName );

        GameCatalog<BrArmorDef>    _armorCatalog;
        GameCatalog<BrBackpackDef> _backpackCatalog;
        GameCatalog<BrLootSpotDef> _lootSpotCatalog;
        BrZoneDef                  _zone;
        BrFlightDef                _flight;
        BrFallDef                  _fall;
        BrPlayerDef                _player;
        BrSupplyDropDef            _supplyDrop;
        float32                    _mapSize;
    };
} // namespace sw
