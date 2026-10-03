/**
 * @file BrCatalog.h
 * @brief 배틀로얄의 데이터 — 자기장 단계 · 비행기 · 낙하 · 체력/부활 · 방어구 등급 · 가방 등급 · 지점별 전리품 표 · 보급 상자입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Data/GameCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XmlNode;

    /** @brief 자기장 한 단계입니다. 단계가 시작되면 다음 원이 드러나고, 기다린 뒤 줄어듭니다. 시간은 초입니다. */
    struct BrZonePhaseDef
    {
        float32 _waitTime{ 60.0f };       ///< 다음 원이 드러난 뒤 줄기 시작할 때까지
        float32 _shrinkTime{ 30.0f };     ///< 지금 원 → 다음 원으로 보간하는 시간
        float32 _radiusRatio{ 0.5f };     ///< 다음 원 반지름 = 지금 원 반지름 × 이 비율
        float32 _damagePerSecond{ 1.0f }; ///< 이 단계 동안 원 밖에서 받는 초당 피해(방어구를 무시한다)
    };

    /** @brief 자기장 설정입니다. */
    struct BrZoneSettings
    {
        vector<BrZonePhaseDef> _listPhase{};
        float32                _startRadius{ 0.0f };  ///< 첫 원 반지름(0 = 맵을 덮는 반 대각선)
        int32                  _centerAttempts{ 32 }; ///< 다음 원 중심을 고르는 시도 수(모두 실패하면 지금 중심)
    };

    /** @brief 비행기 설정입니다. 거리는 m, 속도는 m/s 입니다. */
    struct BrFlightSettings
    {
        float32 _speed{ 100.0f };
        float32 _altitude{ 600.0f };
        float32 _jumpStartRatio{ 0.05f }; ///< 경로 길이의 이 몫부터 뛰어내릴 수 있다
        float32 _jumpEndRatio{ 0.95f };   ///< 이 몫에서 모두 강제로 내린다
        float32 _offsetRatio{ 0.6f };     ///< 경로가 맵 중심에서 비켜나는 최대 몫(반 변 기준)
    };

    /** @brief 낙하 설정입니다. 속도는 m/s 입니다. */
    struct BrFallSettings
    {
        float32 _freeFallSpeed{ 50.0f };           ///< 자유 낙하 수직 속도
        float32 _freeFallHorizontalSpeed{ 20.0f }; ///< 자유 낙하 중 최대 수평 속도
        float32 _parachuteSpeed{ 5.0f };           ///< 낙하산 수직 속도
        float32 _parachuteHorizontalSpeed{ 8.0f }; ///< 낙하산 최대 수평 속도
        float32 _autoOpenHeight{ 100.0f };         ///< 땅 위 이 높이에서 낙하산이 저절로 펴진다
    };

    /** @brief 체력 · 기절 · 부활 설정입니다. */
    struct BrPlayerSettings
    {
        float32 _maxHealth{ 100.0f };
        float32 _downedHealth{ 100.0f };
        float32 _bleedoutRate{ 10.0f };
        float32 _reviveTime{ 10.0f };
        float32 _reviveHealthRatio{ 0.1f };
        float32 _baseCarryWeight{ 20.0f }; ///< 가방 없이 들 수 있는 무게
        int32   _slotCount{ 40 };          ///< 인벤토리 칸 수
        int32   _maxRevivers{ 2 };         ///< 한 사람을 함께 살릴 수 있는 수
    };

    /** @brief 방어구(헬멧 · 조끼) 한 종류입니다. id 는 아이템 id 와 같습니다. */
    struct BrArmorDef
    {
        hashed_string _id{};
        hashed_string _slot{}; ///< "Helmet"(머리) · "Vest"(몸)
        float32       _reduction{ 0.3f };
        float32       _durability{ 100.0f };
        int32         _tier{ 1 };
    };

    /** @brief 가방 한 종류입니다. id 는 아이템 id 와 같습니다. */
    struct BrBackpackDef
    {
        hashed_string _id{};
        float32       _capacity{ 100.0f }; ///< 기본 무게 한도에 더하는 양
        int32         _tier{ 1 };
    };

    /** @brief 지점 종류(건물 · 창고 · 군 기지)마다의 전리품 규칙입니다. */
    struct BrLootSpotDef
    {
        hashed_string _id{};
        hashed_string _tableId{}; ///< `LootCatalog` 의 표
        float32       _chance{ 1.0f };
        int32         _minRolls{ 1 };
        int32         _maxRolls{ 1 };
    };

    /** @brief 보급 상자 설정입니다. */
    struct BrSupplyDropSettings
    {
        vector<float32> _listTime{}; ///< 판 시작 뒤 떨어지는 시각(초, 오름차순)
        hashed_string   _tableId{};
    };

    /**
     * @class BrCatalog
     * @brief `<BattleRoyaleCatalog mapSize="1000"><Player health="100" .../><Flight .../><Fall .../><Zone startRadius="0"><Phase wait="60" shrink="30" ratio="0.5" damage="1"/></Zone>
     *        <Armor id="helmet1" slot="Helmet" tier="1" reduction="0.3" durability="80"/><Backpack id="bag1" tier="1" capacity="150"/>
     *        <LootSpot id="house" table="house" chance="0.8" rolls="1" rollsMax="3"/><SupplyDrop table="airdrop" times="90,180"/></BattleRoyaleCatalog>` 를 읽습니다.
     */
    class SW_GF_API BrCatalog
    {
    public:
        BrCatalog();

        [[nodiscard]] bool loadFromResource( string_view path );
        [[nodiscard]] bool loadFromXmlText( string_view xmlText, string_view sourceName = {} );

        const BrArmorDef*           findArmor( const hashed_string& id ) const { return _armorCatalog.find( id ); }
        const BrBackpackDef*        findBackpack( const hashed_string& id ) const { return _backpackCatalog.find( id ); }
        const BrLootSpotDef*        findLootSpot( const hashed_string& id ) const { return _lootSpotCatalog.find( id ); }
        const BrZoneSettings&       getZoneSettings() const { return _zone; }
        const BrFlightSettings&     getFlightSettings() const { return _flight; }
        const BrFallSettings&       getFallSettings() const { return _fall; }
        const BrPlayerSettings&     getPlayerSettings() const { return _player; }
        const BrSupplyDropSettings& getSupplyDropSettings() const { return _supplyDrop; }
        float32                     getMapSize() const { return _mapSize; }

    private:
        uint32 loadRoot( const XmlNode& root, string_view sourceName );

        GameCatalog<BrArmorDef>    _armorCatalog;
        GameCatalog<BrBackpackDef> _backpackCatalog;
        GameCatalog<BrLootSpotDef> _lootSpotCatalog;
        BrZoneSettings             _zone;
        BrFlightSettings           _flight;
        BrFallSettings             _fall;
        BrPlayerSettings           _player;
        BrSupplyDropSettings       _supplyDrop;
        float32                    _mapSize;
    };
} // namespace sw
