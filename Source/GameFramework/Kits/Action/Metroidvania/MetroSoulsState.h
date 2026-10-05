/**
 * @file MetroSoulsState.h
 * @brief 2D 소울라이크의 죽음과 휴식 — 화톳불 · 기도대(세이브 · 회복 · 물약 충전 · 적 부활 · 되살아날 자리), 죽으면 통화를 그 자리 시체에 떨어뜨리기
 *        (되찾기 전에 또 죽으면 영구 손실), 회복 물약 충전 수(쉬면 충전 · 개수 · 회복량 업그레이드), 적 처치(통화 · 전리품) · 보스 처치 플래그입니다.
 * @details 할로우 나이트의 그림자 · 블라스퍼머스의 죄책감 조각 · 다크 소울의 혈흔이 같은 규칙입니다. 시간이 없고 부른 순서로만 바뀝니다(결정적).
 *          전리품은 기반 `LootCatalog` 를 씨앗이 있는 `GameRandom` 으로 굴립니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Utility/EventBuffer.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct GameStateRefs;

    class GameFlags;
    class GameRandom;
    class ItemBag;
    class LootCatalog;
    class MetroidvaniaCatalog;
    class Vitality;
    class Wallet;

    /** @brief 죽음 · 휴식 알림의 종류입니다. */
    enum class MetroSoulsEventType : uint8
    {
        Rested = 0,       ///< `_id` = 지점
        Died,             ///< `_id` = 되살아날 지점
        CorpseDropped,    ///< `_id` = 방, `_amount` = 떨어뜨린 통화
        CorpseRecovered,  ///< `_amount` = 되찾은 통화
        CurrencyLost,     ///< 시체를 되찾기 전에 또 죽었다 — `_amount` 는 영영 사라졌다
        FlaskUsed,        ///< `_amount` = 남은 충전 수
        EnemyKilled,      ///< `_id` = 적 종류, `_amount` = 얻은 통화
        BossDefeated,     ///< `_id` = 보스 플래그
        EnemiesRespawned, ///< `_amount` = 다시 나온 적 수
    };

    /** @brief 죽음 · 휴식 알림 하나입니다. */
    struct MetroSoulsEvent
    {
        hashed_string       _id{};
        int32               _amount{ 0 };
        MetroSoulsEventType _type{ MetroSoulsEventType::Rested };
    };
} // namespace sw

namespace sw
{
    /** @brief 떨어뜨린 시체(그림자 · 혈흔)입니다. */
    struct MetroCorpse
    {
        hashed_string _area{};
        float2        _position{};
        int32         _currency{ 0 };
        uint8         _bActive{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 처치 기록 하나입니다(놓인 자리 id 단위 — 같은 종류도 자리마다 따로). */
    struct MetroKillRecord
    {
        hashed_string _spawnId{};
        uint8         _bBoss{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /**
     * @class MetroSoulsState
     * @brief 통화 · 시체 · 물약 · 처치 기록 · 되살아날 자리입니다. 체력은 기반 `Vitality` 를 빌려 받습니다.
     */
    class SW_GF_API MetroSoulsState
    {
    public:
        MetroSoulsState();

        /**
         * @brief 카탈로그 규칙으로 처음 상태(물약 가득 · 처치 없음)를 둡니다. 통화는 빌린 지갑(@p refs 의 지갑)의 카탈로그 통화(`MetroRules::_currency`)입니다.
         * @details 죽으면 그 통화의 잔액을 시체로 옮기고(지갑은 0), 되찾으면 지갑으로 돌려줍니다. 지갑이 없으면 처치 보상 · 시체가 없습니다.
         */
        void initialize( const MetroidvaniaCatalog* pCatalog, const GameStateRefs& refs );

        /**
         * @brief 쉬는 지점에서 쉽니다 — 체력 가득(`Vitality::respawn`), 물약 충전, 보스가 아닌 적 부활, 되살아날 자리 갱신.
         * @return 쉬는 지점이 아니면 아무 일 없이 false.
         */
        [[nodiscard]] bool rest( const hashed_string& siteId, Vitality& vitality );
        /**
         * @brief 죽습니다 — 가진 통화를 그 자리에 시체로 떨어뜨리고(이미 시체가 있으면 그 통화는 영영 사라진다), 마지막으로 쉰 자리에서 되살아납니다
         *        (체력 · 물약 가득, 보스가 아닌 적 부활).
         * @return 되살아날 지점 id(쉰 적이 없으면 빈 이름).
         */
        hashed_string die( const hashed_string& areaId, const float2& position, Vitality& vitality );
        /** @brief 시체를 되찾습니다 — 같은 방, `_corpseRecoverRadius` 안이어야 합니다. 되찾았으면 true 입니다. */
        [[nodiscard]] bool tryRecoverCorpse( const hashed_string& areaId, const float2& position );

        /** @brief 물약을 마십니다 — 충전이 남았고 살아 있으면 회복하고 true 입니다. */
        bool drinkFlask( Vitality& vitality );
        /** @brief 물약 충전 수를 하나 늘립니다(상한까지). 늘었으면 true — 늘어난 한 칸은 바로 찬다. */
        bool upgradeFlaskCharges();
        /** @brief 물약 회복량을 한 단계 올립니다. */
        void    upgradeFlaskPotency() { ++_flaskPotencyLevel; }
        float32 computeFlaskHeal() const;

        /**
         * @brief 적을 쓰러뜨립니다. 통화를 더하고, 전리품 표가 있으면 굴려 @p outDrops 에 더하고, 보스면 플래그를 켭니다.
         * @param spawnId 그 적이 놓인 자리(같은 자리는 쉬기 전까지 다시 나오지 않는다)
         * @return 얻은 통화. 모르는 적이거나 이미 쓰러뜨린 자리면 −1.
         */
        int32 registerKill( const hashed_string& spawnId, const hashed_string& enemyId, GameFlags& flags, const LootCatalog* pLoot, GameRandom& random, ItemBag& outDrops );
        /** @brief 그 자리의 적이 지금 살아 있는가(처치 기록이 없다)입니다. */
        bool isSpawnAlive( const hashed_string& spawnId ) const;
        /** @brief 쌓인 알림을 꺼내 갑니다. */
        void drainEvents( vector<MetroSoulsEvent>& outListEvent );

        int32                          getFlaskCharges() const { return _flaskCharges; }
        int32                          getFlaskMaxCharges() const { return _flaskMaxCharges; }
        const MetroCorpse&             getCorpse() const { return _corpse; }
        const hashed_string&           getRespawnSite() const { return _respawnSite; }
        int32                          getLostCurrency() const { return _lostCurrency; }
        const vector<MetroKillRecord>& getKills() const { return _listKill; }

    private:
        void          refreshWorld( Vitality& vitality );
        void          pushEvent( MetroSoulsEventType type, const hashed_string& id, int32 amount );
        hashed_string getCurrencyName() const;

        const MetroidvaniaCatalog*   _pCatalog;
        vector<MetroKillRecord>      _listKill;
        EventBuffer<MetroSoulsEvent> _eventBuffer;
        MetroCorpse                  _corpse;
        hashed_string                _respawnSite;
        Wallet*                      _pWallet;      ///< 빌린 지갑
        int32                        _lostCurrency; ///< 영영 잃은 통화의 합(통계)
        int32                        _flaskCharges;
        int32                        _flaskMaxCharges;
        int32                        _flaskPotencyLevel;
    };
} // namespace sw
