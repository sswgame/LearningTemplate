/**
 * @file ScavengerExpedition.h
 * @brief 협동 수집 공포 한 판을 진행합니다. 위성 고르기(이동 비용), 착륙, 하루(기반 WorldClock, 도착과 해 질 녘과 자정 자동 이륙), 날씨(기반 WeatherSystem),
 *        시설, 고철 운반, 위협(기반 SpawnDirector 의 실내와 실외), 죽음과 시신 회수, 벌금, 전멸 손실, 회사 매입, 할당량, 터미널 상점(기반 ShopState 와 Wallet)을 다룹니다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/EventBuffer.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Actor/AI/SpawnDirector.h"
#include "GameFramework/Base/Actor/Combat/Health/Vitality.h"
#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"
#include "GameFramework/Base/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Base/Gameplay/Inventory/Shop.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Horror/CoopScavenger/Catalog/ScavengerCatalog.h"
#include "GameFramework/Kits/Genre/Horror/CoopScavenger/Rule/ScavengerCarry.h"
#include "GameFramework/Kits/Genre/Horror/CoopScavenger/Rule/ScavengerFacility.h"
#include "GameFramework/Kits/Genre/Horror/CoopScavenger/Rule/ScavengerQuota.h"

namespace sw
{
    struct SessionStateRefs;

    class Archive;
    class ItemCatalog;
    class ShopCatalog;
    class SpawnTable;
    class WeatherCatalog;
    class WeatherSystem;
    class WorldClock;

    /** @brief 판의 단계입니다. */
    enum class ScavengerPhase : uint8
    {
        InOrbit = 0, ///< 궤도에 있다. 위성을 고르고 상점에서 산다
        Landed,      ///< 위성에 내렸다. 하루가 흐른다
        GameOver     ///< 할당량을 못 채웠다
    };

    /** @brief 명령의 결과입니다. */
    enum class ScavengerActionResult : uint8
    {
        Ok = 0,
        WrongPhase, ///< 궤도 상태와 착륙 상태가 맞지 않다
        UnknownMoon,
        NotEnoughCredits,
        InvalidPlayer, ///< 없는 사람이거나 죽은 사람이다
        Blocked,       ///< 이어지지 않았거나 문이 잠겼다
        NotCompany,    ///< 회사 위성에서만 팔 수 있다
        NotOnShip      ///< 우주선 안에서만 할 수 있다
    };

    SW_GF_API const utf8* toString( ScavengerActionResult result );

    /** @brief 사람 하나입니다. */
    struct ScavengerCrewMember
    {
        Vitality       _vitality{};
        ScavengerCarry _carry{};
        hashed_string  _areaID{};                   ///< 있는 곳이다(`ship`, `outside`, 시설 방)
        uint8          _bBodyRecovered{ SW_FALSE }; ///< 죽었고 시신이 우주선에 실렸다

        bool isDead() const { return _vitality.isDead(); }
    };
} // namespace sw

namespace sw
{
    /** @brief 판에서 일어난 일을 알리는 이벤트입니다. */
    struct ScavengerEvent
    {
        enum class Kind : uint8
        {
            Routed = 0,    ///< `_id` 는 위성, `_value` 는 낸 비용이다
            Landed,        ///< `_id` 는 위성, `_value` 는 고철 가치 합이다
            Dusk,          ///< 해 질 녘이며 바깥이 위험해진다
            ThreatSpawned, ///< `_id` 는 위협 항목, `_value` 는 생성 번호이고 `_bIndoor` 를 쓴다
            PlayerDied,    ///< `_player` 가 죽었고, `_value` 가 1 이면 남겨져서 죽은 것이다
            BodyRecovered, ///< `_player` 의 시신이 우주선에 실렸다
            ShipDeparted,  ///< `_value` 가 1 이면 자정이나 전멸로 인한 자동 이륙이다
            CrewWiped,     ///< 모두 죽었다. `_value` 는 잃은 고철 수이다
            FinePaid,      ///< `_value` 는 벌금이다
            ScrapSold,     ///< `_value` 는 받은 크레딧이다
            QuotaMet,      ///< `_value` 는 새 할당량이다
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
    /** @brief 빌려 쓰는 데이터 번들입니다. 카탈로그 말고는 없어도 되며, 없으면 날씨, 위협, 상점이 빠집니다. */
    struct ScavengerExpeditionData
    {
        const ScavengerCatalog* _pCatalog{ nullptr };
        const WeatherCatalog*   _pWeatherCatalog{ nullptr }; ///< 계절 이름이 위성 id 이다(`seasons="march:2,rend:1"`)
        const SpawnTable*       _pThreatTable{ nullptr };    ///< 태그 `Indoor` 와 `Outdoor` 로 나눈다
        const ShopCatalog*      _pShopCatalog{ nullptr };
        const ItemCatalog*      _pItemCatalog{ nullptr };
    };
} // namespace sw

namespace sw
{
    /**
     * @class ScavengerExpedition
     * @brief 리썰 컴퍼니의 규칙을 구현합니다. 화면, 몸, 몬스터 행동은 게임이 맡고, 이 클래스는 어디에 누가 무엇을 들고 있는지와 언제 무엇이 나오는지만 정합니다.
     * @details - 날씨 값 `scrapValue`(고철 가치 배율)와 `threat`(위협 배율)를 읽는다. 0(없음)은 1 로 본다.
     *          - 위협은 실내 감독과 실외 감독이 같은 테이블을 태그로 나눠 쓴다. 예산이 쌓이는 시간은 실제 초 x 위성 위험도 x 날씨 위협이다.
     *          - 자정(`departHour`)이 되거나 모두 죽으면 우주선이 저절로 뜬다. 우주선 밖에 남은 사람은 죽고, 우주선 안의 사람이 든 고철은 실린다.
     *          - 이륙할 때 죽은 사람마다 크레딧의 일정 비율을 벌금으로 낸다(시신을 실었으면 더 적은 비율). 모두 죽었으면 우주선 고철의 일정 비율을 잃는다.
     *          - 회사 위성에서만 팔 수 있고, 받는 값은 고철 가치 합 x 매입률(마감에 가까울수록 오른다)이다. 그 값이 할당량을 채운다.
     *          - 날마다 이륙하면 할당량의 남은 날이 줄고, 마감 날(남은 날 0)의 이륙에서 판정한다. 못 채우면 게임 오버이다.
     *          시드가 같고 같은 순서로 호출하면 같은 시설, 날씨, 위협, 손실이 나온다.
     */
    class SW_GF_API ScavengerExpedition
    {
    public:
        static constexpr const utf8* kShipAreaID    = "ship";
        static constexpr const utf8* kOutsideAreaID = "outside";
        static constexpr uint32      kStateTag      = FourCcUtil::make( "SCEX" );
        static constexpr uint32      kStateVersion  = 1;

        ScavengerExpedition();

        /**
         * @brief 새 원정을 엽니다. 회사 돈, 시계, 날씨, 플래그(시설 문 잠금 `unlocked.<방>`)는 @p refs 에서 빌립니다. 지갑은 시작 크레딧으로 맞추고, 시계와 날씨는 직접 흘리지 않습니다
         *        (공유 상태의 주인이 흘립니다). 내리면 그 위성의 날씨로 바꾸고, 떠나면 시계를 다음 날 도착 시각으로 넘깁니다.
         *        터미널에서 산 물건은 빌린 우주선 창고(@p shipStorage)에 들어가며, 창고는 원정보다 오래 살아야 합니다.
         */
        void initialize( const ScavengerExpeditionData& data, const SessionStateRefs& refs, Inventory& shipStorage, uint32 seed, int32 crewCount );

        /** @brief 궤도에서 위성으로 이동합니다(비용을 냅니다). 이미 그 위성이면 공짜입니다. */
        ScavengerActionResult routeTo( const hashed_string& moonID );
        /** @brief 지금 위성에 내립니다. 하루가 시작되고 시설, 날씨, 위협이 정해집니다. */
        ScavengerActionResult land();
        /** @brief 이륙합니다(사람이 레버를 당겼을 때). */
        ScavengerActionResult takeOff();
        void                  update( float32 deltaTime );

        /** @brief 이어진 곳으로 옮깁니다(잠긴 문은 막힙니다). */
        ScavengerActionResult movePlayer( int32 player, const hashed_string& areaID );
        /** @brief 그 방의 바닥에서 줍습니다. */
        ScavengerPickupResult pickUp( int32 player, int32 uid );
        /** @brief 든 것을 지금 방 바닥에 내려놓습니다(우주선 안이면 우주선에 싣습니다). */
        ScavengerActionResult dropItem( int32 player, int32 uid );
        /** @brief 우주선 안에서 든 것을 모두 싣고, 실은 수를 반환합니다. */
        int32 depositToShip( int32 player );
        /** @brief 잠긴 문을 엽니다(열쇠와 자물쇠 따개를 쓸지는 게임이 판단합니다). */
        bool unlockDoor( const hashed_string& roomID ) { return _facility.unlockDoor( roomID ); }
        /** @brief 피해를 주고, 체력이 실제로 받은 양을 반환합니다. */
        float32 applyDamage( int32 player, float32 amount );
        void    killPlayer( int32 player );
        /** @brief 게임 쪽 위협이 사라졌음을 알립니다. */
        void notifyThreatDespawned( bool bIndoor, uint32 spawnID );

        /** @brief 회사에서 우주선 고철을 모두 팔고 받은 크레딧을 반환합니다(회사가 아니면 0 이고 @p outResult 에 사유를 담습니다). */
        int32 sellAllShipScrap( ScavengerActionResult& outResult );
        /** @brief 지금 팔면 받는 값을 계산합니다. */
        int32 computeSellValue() const;
        /** @brief 터미널 상점에서 삽니다(우주선 창고로 들어갑니다). */
        ShopResult buyFromTerminal( const hashed_string& itemID, int32 count );
        void       drainEvents( vector<ScavengerEvent>& outListEvent );

        /**
         * @brief 사람(체력, 든 것, 위치, 시신), 우주선 고철, 할당량, 시설, 위협 감독 둘, 상점 시세, 난수, 위성 id, 시드, 내린 시간, 단계, 해 질 녘 알림을 씁니다.
         *        카탈로그, 빌린 지갑, 시계, 날씨, 플래그, 우주선 창고는 저장하지 않습니다(주인이 저장합니다). 이벤트는 읽을 때 비웁니다.
         */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 가 쓴 바이트를 읽어 상태를 복원합니다. 같은 데이터와 같은 사람 수로 `initialize` 한 뒤에 호출해야 합니다. 데이터가 깨졌거나 없는 위성이면 false 를 반환하고 상태는 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

        ScavengerPhase                getPhase() const { return _phase; }
        const ScavengerMoonDef*       getMoon() const { return _pMoon; }
        const ScavengerQuota&         getQuota() const { return _quota; }
        int64                         getCredits() const;
        const ScavengerFacility&      getFacility() const { return _facility; }
        const Inventory&              getShipInventory() const { return *_pShipStorage; }
        const vector<ScavengerScrap>& getShipScrap() const { return _listShipScrap; }
        int32                         computeShipValue() const;
        const ScavengerCrewMember*    findCrewMember( int32 player ) const;
        int32                         getCrewCount() const { return static_cast<int32>( _listCrew.size() ); }
        int32                         countAlive() const;
        /** @brief 빌린 시계의 날을 반환합니다(없으면 0). 그날의 배치, 위협, 날씨 시드로 씁니다. */
        int32 getDayIndex() const;
        /** @brief 내린 뒤 흐른 게임 시간(시)을 반환합니다. */
        float32 getHoursOnMoon() const { return _hoursOnMoon; }
        /** @brief 위협 예산 배율(위성 위험도 x 날씨)을 반환합니다. */
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
        EventBuffer<ScavengerEvent> _eventBuffer;
        vector<SpawnEvent>          _listSpawnScratch;
        ScavengerExpeditionData     _data;
        ScavengerQuota              _quota;
        ScavengerFacility           _facility;
        SpawnDirector               _indoorDirector;
        SpawnDirector               _outdoorDirector;
        ShopState                   _shop;
        GameRandom                  _random;
        Inventory*                  _pShipStorage; ///< 우주선 창고(빌림)
        Wallet*                     _pWallet;      ///< 빌린 지갑(회사 돈)
        WorldClock*                 _pClock;       ///< 빌린 시계이다. 떠나면 다음 날 도착 시각으로 넘긴다
        WeatherSystem*              _pWeather;     ///< 빌린 날씨이다. 내리면 그 위성의 날씨로 바꾼다
        const ScavengerMoonDef*     _pMoon;
        uint32                      _seed;
        float32                     _hoursOnMoon;
        ScavengerPhase              _phase;
        uint8                       _bDuskAnnounced;
    };
} // namespace sw
