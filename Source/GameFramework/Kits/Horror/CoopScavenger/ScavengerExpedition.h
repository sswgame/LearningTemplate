/**
 * @file ScavengerExpedition.h
 * @brief 협동 수집 공포 한 판 — 위성 고르기(이동 비용) · 착륙 · 하루(기반 WorldClock — 도착 · 해 질 녘 · 자정 자동 이륙) · 날씨(기반 WeatherSystem) ·
 *        시설 · 고철 운반 · 위협(기반 SpawnDirector 실내 · 실외) · 죽음과 시신 회수 · 벌금 · 전멸 손실 · 회사 매입 · 할당량 · 터미널 상점(기반 ShopState/Wallet)입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/AI/SpawnDirector.h"
#include "GameFramework/Combat/Vitality.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Inventory/Inventory.h"
#include "GameFramework/Inventory/Shop.h"
#include "GameFramework/Kits/Horror/CoopScavenger/ScavengerCarry.h"
#include "GameFramework/Kits/Horror/CoopScavenger/ScavengerCatalog.h"
#include "GameFramework/Kits/Horror/CoopScavenger/ScavengerFacility.h"
#include "GameFramework/Kits/Horror/CoopScavenger/ScavengerQuota.h"
#include "GameFramework/Utility/GameRandom.h"
#include "GameFramework/World/WeatherSystem.h"
#include "GameFramework/World/WorldClock.h"

namespace sw
{
    class ItemCatalog;
    class ShopCatalog;
    class SpawnTable;
    class WeatherCatalog;

    /** @brief 판의 단계입니다. */
    enum class ScavengerPhase : uint8
    {
        InOrbit = 0, ///< 궤도 — 위성을 고르고 상점에서 산다
        Landed,      ///< 내렸다 — 하루가 흐른다
        GameOver     ///< 할당량을 못 채웠다
    };

    /** @brief 명령 결과입니다. */
    enum class ScavengerActionResult : uint8
    {
        Ok = 0,
        WrongPhase, ///< 궤도 · 착륙 상태가 맞지 않다
        UnknownMoon,
        NotEnoughCredits,
        InvalidPlayer, ///< 없는 사람 · 죽은 사람
        Blocked,       ///< 이어지지 않았거나 문이 잠겼다
        NotCompany,    ///< 회사 위성에서만 판다
        NotOnShip      ///< 우주선 안에서만 한다
    };

    SW_GF_API const utf8* toString( ScavengerActionResult result );

    /** @brief 사람 하나입니다. */
    struct ScavengerCrewMember
    {
        Vitality       _vitality{};
        ScavengerCarry _carry{};
        hashed_string  _areaId{};                   ///< 있는 곳(`ship` · `outside` · 시설 방)
        uint8          _bBodyRecovered{ SW_FALSE }; ///< 죽었고 시신이 우주선에 실렸다

        bool isDead() const { return _vitality.isDead(); }
    };
} // namespace sw

namespace sw
{
    /** @brief 판에서 생긴 일입니다. */
    struct ScavengerEvent
    {
        enum class Kind : uint8
        {
            Routed = 0,    ///< _id = 위성, _value = 낸 비용
            Landed,        ///< _id = 위성, _value = 고철 가치 합
            Dusk,          ///< 해 질 녘 — 바깥이 위험해진다
            ThreatSpawned, ///< _id = 위협 항목, _value = 생성 번호, _bIndoor
            PlayerDied,    ///< _player, _value = 1 이면 남겨져 죽음
            BodyRecovered, ///< _player 의 시신이 우주선에 실렸다
            ShipDeparted,  ///< _value = 1 이면 자정 · 전멸 자동 이륙
            CrewWiped,     ///< 모두 죽었다 — _value = 잃은 고철 수
            FinePaid,      ///< _value = 벌금
            ScrapSold,     ///< _value = 받은 크레딧
            QuotaMet,      ///< _value = 새 할당량
            GameOver
        };
        hashed_string _id{};
        int32         _player{ -1 };
        int32         _value{ 0 };
        Kind          _kind{ Kind::Routed };
        uint8         _bIndoor{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 빌려 쓰는 데이터 묶음입니다. 카탈로그 말고는 없어도 됩니다(날씨 · 위협 · 상점이 빠진다). */
    struct ScavengerExpeditionData
    {
        const ScavengerCatalog* _pCatalog{ nullptr };
        const WeatherCatalog*   _pWeatherCatalog{ nullptr }; ///< 계절 이름 = 위성 id(`seasons="march:2,rend:1"`)
        const SpawnTable*       _pThreatTable{ nullptr };    ///< 태그 `Indoor` · `Outdoor` 로 나눈다
        const ShopCatalog*      _pShopCatalog{ nullptr };
        const ItemCatalog*      _pItemCatalog{ nullptr };
    };
} // namespace sw

namespace sw
{
    /**
     * @class ScavengerExpedition
     * @brief 리썰 컴퍼니의 규칙입니다 — 화면 · 몸 · 몬스터 행동은 게임이 하고, 여기는 어디에 누가 무엇을 들고 있고 언제 무엇이 나오는가만 정합니다.
     * @details - 날씨 값 `scrapValue`(고철 가치 배율)와 `threat`(위협 배율)를 읽습니다. 0(없음)은 1 로 봅니다.
     *          - 위협은 실내 · 실외 감독 둘이 같은 테이블을 태그로 나눠 씁니다. 예산이 쌓이는 시간은 실제 초 × 위성 위험도 × 날씨 위협입니다.
     *          - 자정(`departHour`)이 되거나 모두 죽으면 우주선이 저절로 뜹니다. 우주선 밖에 남은 사람은 죽고, 우주선 안의 사람이 든 고철은 실립니다.
     *          - 이륙 때 죽은 사람마다 크레딧의 몫을 벌금으로 냅니다(시신을 실었으면 작은 몫). 모두 죽었으면 우주선 고철의 몫을 잃습니다.
     *          - 회사 위성에서만 팔고, 받는 값 = 고철 가치 합 × 매입률(마감에 가까울수록 오른다). 그 값이 할당량을 채운다.
     *          - 날마다 이륙하면 할당량의 남은 날이 줄고, 마감 날(남은 날 0) 이륙에서 판정합니다 — 못 채우면 게임 오버.
     *          씨앗이 같고 같은 순서로 부르면 같은 시설 · 날씨 · 위협 · 손실입니다.
     */
    class SW_GF_API ScavengerExpedition
    {
    public:
        static constexpr const utf8* kShipAreaId    = "ship";
        static constexpr const utf8* kOutsideAreaId = "outside";

        ScavengerExpedition();

        void initialize( const ScavengerExpeditionData& data, uint32 seed, int32 crewCount );

        /** @brief 궤도에서 위성으로 갑니다(비용을 낸다). 이미 그 위성이면 공짜입니다. */
        ScavengerActionResult routeTo( const hashed_string& moonId );
        /** @brief 지금 위성에 내립니다 — 하루가 시작되고 시설 · 날씨 · 위협이 정해집니다. */
        ScavengerActionResult land();
        /** @brief 이륙합니다(사람이 레버를 당겼다). */
        ScavengerActionResult takeOff();
        void                  update( float32 deltaTime );

        /** @brief 이어진 곳으로 옮깁니다(잠긴 문은 막힌다). */
        ScavengerActionResult movePlayer( int32 player, const hashed_string& areaId );
        /** @brief 그 방의 바닥에서 줍습니다. */
        ScavengerPickupResult pickUp( int32 player, int32 uid );
        /** @brief 든 것을 지금 방 바닥에 내려놓습니다(우주선 안이면 우주선에 싣는다). */
        ScavengerActionResult dropItem( int32 player, int32 uid );
        /** @brief 우주선 안에서 든 것을 모두 싣습니다. 실은 수입니다. */
        int32 depositToShip( int32 player );
        /** @brief 잠긴 문을 엽니다(열쇠 · 자물쇠 따개 — 게임이 판단). */
        bool unlockDoor( const hashed_string& roomId ) { return _facility.unlockDoor( roomId ); }
        /** @brief 피해를 줍니다. 체력이 받은 양입니다. */
        float32 applyDamage( int32 player, float32 amount );
        void    killPlayer( int32 player );
        /** @brief 게임 쪽 위협이 사라졌습니다. */
        void notifyThreatDespawned( bool bIndoor, uint32 spawnId );

        /** @brief 회사에서 우주선 고철을 모두 팝니다. 받은 크레딧입니다(회사가 아니면 0 · @p outResult). */
        int32 sellAllShipScrap( ScavengerActionResult& outResult );
        /** @brief 지금 팔면 받는 값입니다. */
        int32 computeSellValue() const;
        /** @brief 터미널 상점에서 삽니다(우주선 창고로). */
        ShopResult buyFromTerminal( const hashed_string& itemId, int32 count );
        void       drainEvents( vector<ScavengerEvent>& outListEvent );

        ScavengerPhase                getPhase() const { return _phase; }
        const ScavengerMoonDef*       getMoon() const { return _pMoon; }
        const ScavengerQuota&         getQuota() const { return _quota; }
        const Wallet&                 getWallet() const { return _wallet; }
        int64                         getCredits() const;
        const ScavengerFacility&      getFacility() const { return _facility; }
        const WorldClock&             getClock() const { return _clock; }
        const WeatherSystem&          getWeather() const { return _weather; }
        const Inventory&              getShipInventory() const { return _shipInventory; }
        const vector<ScavengerScrap>& getShipScrap() const { return _listShipScrap; }
        int32                         computeShipValue() const;
        const ScavengerCrewMember*    findCrewMember( int32 player ) const;
        int32                         getCrewCount() const { return static_cast<int32>( _listCrew.size() ); }
        int32                         countAlive() const;
        int32                         getDayIndex() const { return _dayIndex; }
        /** @brief 내린 뒤 흐른 게임 시간(시)입니다. */
        float32 getHoursOnMoon() const { return _hoursOnMoon; }
        /** @brief 위협 예산 배율(위성 위험도 × 날씨)입니다. */
        float32 computeThreatScale() const;

    private:
        bool    isValidPlayer( int32 player ) const { return player >= 0 && player < static_cast<int32>( _listCrew.size() ); }
        float32 computeWeatherValue( const utf8* pName ) const;
        void    handleDeath( int32 player, bool bLeftBehind );
        void    departShip( bool bAuto );
        void    loseScrapOnWipe();
        void    resetCrew();
        void    collectThreatEvents( SpawnDirector& director, bool bIndoor );
        void    pushEvent( ScavengerEvent::Kind kind, int32 player, int32 value, const hashed_string& id = hashed_string{} );

        vector<ScavengerCrewMember> _listCrew;
        vector<ScavengerScrap>      _listShipScrap;
        vector<ScavengerEvent>      _listEvent;
        vector<SpawnEvent>          _listSpawnScratch;
        ScavengerExpeditionData     _data;
        ScavengerQuota              _quota;
        ScavengerFacility           _facility;
        WorldClock                  _clock;
        WeatherSystem               _weather;
        SpawnDirector               _indoorDirector;
        SpawnDirector               _outdoorDirector;
        Wallet                      _wallet;
        ShopState                   _shop;
        Inventory                   _shipInventory;
        GameRandom                  _random;
        const ScavengerMoonDef*     _pMoon;
        uint32                      _seed;
        float32                     _hoursOnMoon;
        int32                       _dayIndex;
        ScavengerPhase              _phase;
        uint8                       _bDuskAnnounced;
    };
} // namespace sw
