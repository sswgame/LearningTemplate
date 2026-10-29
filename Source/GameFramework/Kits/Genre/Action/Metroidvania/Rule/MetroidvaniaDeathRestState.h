/**
 * @file MetroidvaniaDeathRestState.h
 * @brief 2D 소울라이크의 죽음과 휴식을 다룹니다. 화톳불과 기도대(세이브, 회복, 물약 충전, 적 부활, 되살아날 위치), 죽으면 통화를 그 자리의 시체에 떨어뜨리기(되찾기 전에 또 죽으면 영구 손실),
 *        회복 물약 충전 수(쉬면 충전되고 개수와 회복량을 업그레이드할 수 있음), 적 처치(통화와 전리품), 보스 처치 플래그를 관리합니다.
 * @details 할로우 나이트의 그림자, 블라스퍼머스의 죄책감 조각, 다크 소울의 혈흔이 같은 규칙이다. 시간 개념이 없고 호출한 순서로만 바뀐다(결정적이다).
 *          전리품은 기반 `LootCatalog` 를 시드가 있는 `GameRandom` 으로 굴린다.
 */
#pragma once
#include "Core/Container/EventBuffer.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/String/FourCcUtil.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct SessionStateRefs;

    class Archive;
    class GameFlags;
    class GameRandom;
    class ItemStackList;
    class LootCatalog;
    class MetroidvaniaCatalog;
    class Vitality;
    class Wallet;

    /** @brief 죽음과 휴식 알림의 종류입니다. */
    enum class MetroidvaniaDeathRestEventType : uint8
    {
        Rested = 0,       ///< `_id` 는 지점
        Died,             ///< `_id` 는 되살아날 지점
        CorpseDropped,    ///< `_id` 는 방, `_amount` 는 떨어뜨린 통화
        CorpseRecovered,  ///< `_amount` 는 되찾은 통화
        CurrencyLost,     ///< 시체를 되찾기 전에 또 죽어서 `_amount` 만큼 영영 사라졌다
        FlaskUsed,        ///< `_amount` 는 남은 충전 수
        EnemyKilled,      ///< `_id` 는 적 종류, `_amount` 는 얻은 통화
        BossDefeated,     ///< `_id` 는 보스 플래그
        EnemiesRespawned, ///< `_amount` 는 다시 나온 적 수
    };

    /** @brief 죽음과 휴식 알림 하나입니다. */
    struct MetroidvaniaDeathRestEvent
    {
        hashed_string                  _id{};
        int32                          _amount{ 0 };
        MetroidvaniaDeathRestEventType _type{ MetroidvaniaDeathRestEventType::Rested };
    };
} // namespace sw

namespace sw
{
    /** @brief 떨어뜨린 시체(그림자, 혈흔)입니다. */
    struct MetroidvaniaCorpse
    {
        hashed_string _area{};
        float2        _position{};
        int32         _currency{ 0 };
        uint8         _bActive{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 처치 기록 하나입니다. 놓인 위치 id 단위로 기록하므로 같은 종류도 위치마다 따로 기록합니다. */
    struct MetroidvaniaKillRecord
    {
        hashed_string _spawnID{};
        uint8         _bBoss{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /**
     * @class MetroidvaniaDeathRestState
     * @brief 통화, 시체, 물약, 처치 기록, 되살아날 위치를 관리합니다. 체력은 기반 `Vitality` 를 빌려 받습니다.
     */
    class SW_KIT_API MetroidvaniaDeathRestState
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "MSOL" );
        static constexpr uint32 kStateVersion = 1;

        MetroidvaniaDeathRestState();

        /**
         * @brief 카탈로그 규칙으로 처음 상태(물약 가득, 처치 없음)를 설정합니다. 통화는 빌려 온 지갑(@p refs 의 지갑)에 있는 카탈로그 통화(`MetroidvaniaRules::_currency`)입니다.
         * @details 죽으면 그 통화의 잔액을 시체로 옮기고(지갑은 0이 된다), 되찾으면 지갑으로 돌려준다. 지갑이 없으면 처치 보상과 시체도 없다.
         */
        void initialize( const MetroidvaniaCatalog* pCatalog, const SessionStateRefs& refs );

        /**
         * @brief 쉬는 지점에서 쉽니다. 체력을 가득 채우고(`Vitality::respawn`), 물약을 충전하고, 보스가 아닌 적을 부활시키고, 되살아날 위치를 갱신합니다.
         * @return 쉬는 지점이 아니면 아무 일도 하지 않고 false 를 반환합니다.
         */
        [[nodiscard]] bool rest( const hashed_string& siteID, Vitality& vitality );
        /**
         * @brief 죽습니다. 가진 통화를 그 자리에 시체로 떨어뜨리고(이미 시체가 있으면 그 통화는 영영 사라집니다), 마지막으로 쉰 위치에서 되살아납니다
         *        (체력과 물약은 가득 차고, 보스가 아닌 적은 부활합니다).
         * @return 되살아날 지점 id 입니다(쉰 적이 없으면 빈 이름).
         */
        hashed_string die( const hashed_string& areaID, const float2& position, Vitality& vitality );
        /** @brief 시체를 되찾습니다. 같은 방이고 `_corpseRecoverRadius` 안이어야 합니다. 되찾았으면 true 를 반환합니다. */
        [[nodiscard]] bool tryRecoverCorpse( const hashed_string& areaID, const float2& position );

        /** @brief 물약을 마십니다. 충전이 남았고 살아 있으면 회복하고 true 를 반환합니다. */
        bool drinkFlask( Vitality& vitality );
        /** @brief 물약 충전 수를 하나 늘립니다(상한까지). 늘었으면 true 를 반환하며, 늘어난 충전은 바로 찹니다. */
        bool upgradeFlaskCharges();
        /** @brief 물약 회복량을 한 단계 올립니다. */
        void    upgradeFlaskPotency() { ++_flaskPotencyLevel; }
        float32 computeFlaskHeal() const;

        /**
         * @brief 적을 쓰러뜨립니다. 통화를 더하고, 전리품 테이블이 있으면 굴려서 @p outDrops 에 더하며, 보스면 플래그를 켭니다.
         * @param spawnID 그 적이 놓인 위치(같은 위치는 쉬기 전까지 다시 나오지 않는다)
         * @return 얻은 통화입니다. 모르는 적이거나 이미 쓰러뜨린 위치면 −1 을 반환합니다.
         */
        int32 registerKill( const hashed_string& spawnID, const hashed_string& enemyID, GameFlags& flags, const LootCatalog* pLoot, GameRandom& random, ItemStackList& outDrops );
        /** @brief 그 위치의 적이 지금 살아 있는지(처치 기록이 없는지) 반환합니다. */
        bool isSpawnAlive( const hashed_string& spawnID ) const;
        /** @brief 쌓인 알림을 꺼내 갑니다. */
        void drainEvents( vector<MetroidvaniaDeathRestEvent>& outListEvent );

        int32                                 getFlaskCharges() const { return _flaskCharges; }
        int32                                 getFlaskMaxCharges() const { return _flaskMaxCharges; }
        const MetroidvaniaCorpse&             getCorpse() const { return _corpse; }
        const hashed_string&                  getRespawnSite() const { return _respawnSite; }
        int32                                 getLostCurrency() const { return _lostCurrency; }
        const vector<MetroidvaniaKillRecord>& getKills() const { return _listKill; }

        /**
         * @brief 처치 기록, 시체, 되살아날 지점, 잃은 통화, 물약(충전, 최대, 회복 단계)을 씁니다. 통화 잔액은 빌려 온 지갑의 것이고 카탈로그는 `initialize` 에서 받은 것이라
         *        싣지 않으며, 알림은 읽을 때 비웁니다.
         */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 가 쓴 바이트로 상태를 바꿉니다. 카탈로그에 없는 되살아날 지점이거나 바이트가 깨졌으면 상태를 바꾸지 않고 false 를 반환합니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        void          refreshWorld( Vitality& vitality );
        void          pushEvent( MetroidvaniaDeathRestEventType type, const hashed_string& id, int32 amount );
        hashed_string getCurrencyName() const;

        const MetroidvaniaCatalog*              _pCatalog;
        vector<MetroidvaniaKillRecord>          _listKill;
        EventBuffer<MetroidvaniaDeathRestEvent> _eventBuffer;
        MetroidvaniaCorpse                      _corpse;
        hashed_string                           _respawnSite;
        Wallet*                                 _pWallet;      ///< 빌려 온 지갑
        int32                                   _lostCurrency; ///< 영영 잃은 통화의 합(통계)
        int32                                   _flaskCharges;
        int32                                   _flaskMaxCharges;
        int32                                   _flaskPotencyLevel;
    };
} // namespace sw
