/**
 * @file BrMatch.h
 * @brief 배틀로얄 한 판 — 팀 · 순위(기반 MatchState: 부활 없음 · 마지막 팀 승리), 기절(기반 Vitality) · 팀원 부활 진행(기반 InteractionProgress),
 *        팀 전원 기절 = 전멸, 자기장 피해, 보급 상자, 남은 인원 · 킬 피드 알림입니다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "GameFramework/Base/Combat/Vitality.h"
#include "GameFramework/Base/Interaction/InteractionProgress.h"
#include "GameFramework/Base/Inventory/Inventory.h"
#include "GameFramework/Base/Match/MatchState.h"
#include "GameFramework/Base/Utility/EventBuffer.h"
#include "GameFramework/Base/Utility/FixedStepTimer.h"
#include "GameFramework/Base/Utility/GameRandom.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Action/BattleRoyale/BrGear.h"
#include "GameFramework/Kits/Action/BattleRoyale/BrLoot.h"
#include "GameFramework/Kits/Action/BattleRoyale/BrZone.h"

namespace sw
{
    class Archive;
    class BrCatalog;
    class ItemCatalog;
    class LootCatalog;

    /** @brief 한 사람입니다. 번호는 `MatchState` 참가자 번호와 같습니다. */
    struct BrPlayer
    {
        Vitality            _vitality{};
        InteractionProgress _revive{}; ///< 이 사람을 살리는 진행(팀원이 붙는다)
        Inventory           _inventory{};
        BrLoadout           _loadout{};
        float2              _position{};
        int32               _team{ -1 };
        int32               _kills{ 0 };
        int32               _knocks{ 0 };          ///< 기절시킨 수
        int32               _downedBy{ -1 };       ///< 기절시킨 사람(출혈사 · 전멸 처치의 귀속)
        int32               _revivingTarget{ -1 }; ///< 지금 살리고 있는 팀원

        bool isAlive() const { return _vitality.isAlive(); }
        bool isDowned() const { return _vitality.isDowned(); }
        bool isDead() const { return _vitality.isDead(); }
    };
} // namespace sw

namespace sw
{
    /** @brief 떨어진 보급 상자 하나입니다. */
    struct BrSupplyDrop
    {
        vector<BrGroundItem> _listItem{};
        float2               _position{};
        float32              _time{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 판에서 생긴 일(킬 피드 · 화면 알림)입니다. */
    struct BrEvent
    {
        enum class Kind : uint8
        {
            PlayerDowned = 0,  ///< _player 가 _other 에게 기절(킬 피드 "knocked")
            PlayerKilled,      ///< _player 가 _other(−1 = 자기장 · 환경)에게 죽음, _value = 남은 인원
            ReviveStarted,     ///< _player(기절) 를 _other 가 살리기 시작
            ReviveInterrupted, ///< 맞아서 끊겼다
            PlayerRevived,     ///< _player 를 _other 가 살렸다
            ArmorBroken,       ///< _player 의 방어구가 부서졌다(_value = BrHitZone)
            PlayersRemaining,  ///< _value = 남은 인원(기절 포함)
            TeamEliminated,    ///< _team, _value = 순위
            SupplyDropLanded,  ///< _value = 보급 상자 번호, _position
            MatchEnded         ///< _team = 이긴 팀(−1 = 무승부)
        };
        float2 _position{};
        int32  _player{ -1 };
        int32  _other{ -1 };
        int32  _team{ -1 };
        int32  _value{ 0 };
        Kind   _kind{ Kind::PlayerDowned };
    };
} // namespace sw

namespace sw
{
    /**
     * @class BrMatch
     * @brief 판의 규칙만 돌립니다 — 몸 · 총알 · 화면은 게임이 하고 맞은 피해와 자리를 알려 줍니다. 카탈로그 셋은 빌려 씁니다.
     * @details 고정 걸음(`kFixedStep`)마다: 자기장 → 원 밖 피해(방어구 무시, 귀속 없음) → 체력(출혈) → 부활 진행 → 보급 상자.
     *          - 피해는 방어구(맞은 부위) → `Vitality` 순서. 체력이 0 이면 기절하고, 팀에 서 있는 사람이 없으면 기절한 팀원이 모두 죽는다(팀 전멸).
     *          - 부활은 팀원이 `beginRevive` 로 붙는 `InteractionProgress` 이다 — 여럿이면 빨라지고, 모두 떠나거나 기절한 쪽이 맞으면 처음부터.
     *            살리는 동안 출혈이 멈추고, 다 차면 `Vitality` 가 살아난다.
     *          - 죽음은 `MatchState::reportKill` 로 — 부활이 없어 바로 탈락, 팀이 다 떨어지면 남은 팀 수 + 1 이 순위, 하나 남으면 그 팀이 이긴다.
     *          - 출혈사 · 전멸 · 기절 뒤 자기장 사망은 기절시킨 사람의 처치다.
     *          씨앗이 같고 같은 순서로 부르면 같은 판입니다.
     */
    class SW_GF_API BrMatch
    {
    public:
        static constexpr uint32  kStateTag     = FourCcUtil::make( "BRMT" );
        static constexpr uint32  kStateVersion = 1;
        static constexpr float32 kFixedStep    = 0.1f;

        BrMatch();

        /** @brief 카탈로그 · 아이템 · 전리품(빌림) · 씨앗 · 지형(빌림, nullptr 가능)을 두고 비웁니다. */
        void  initialize( const BrCatalog* pCatalog, const ItemCatalog* pItemCatalog, const LootCatalog* pLootCatalog, uint32 seed, const IBrZoneTerrain* pTerrain );
        int32 addTeam( const hashed_string& name );
        /** @brief 사람을 더합니다(`start` 전). 번호를 돌려줍니다. */
        int32 addPlayer( int32 team );
        void  start();
        void  update( float32 deltaTime );

        void setPlayerPosition( int32 player, const float2& position );
        /**
         * @brief @p attacker(−1 = 환경)가 @p victim 에게 피해를 줍니다. 방어구를 거친 뒤 체력(기절 중이면 출혈 체력)이 받은 양입니다.
         * @details 같은 팀끼리의 피해는 받지 않습니다.
         */
        float32 applyDamage( int32 victim, int32 attacker, float32 damage, BrHitZone hitZone );
        /** @brief @p reviver 가 기절한 팀원 @p target 을 살리기 시작합니다. 같은 팀 · 서 있음 · 상대가 기절 · 자리 남음이어야 합니다. */
        [[nodiscard]] bool beginRevive( int32 reviver, int32 target );
        /** @brief 살리기를 그만둡니다(움직였다 · 떠났다). */
        void endRevive( int32 reviver );
        void drainEvents( vector<BrEvent>& outListEvent );

        const BrPlayer* findPlayer( int32 player ) const;
        BrPlayer*       findPlayerMutable( int32 player );
        int32           getPlayerCount() const { return static_cast<int32>( _listPlayer.size() ); }
        /** @brief 죽지 않은(기절 포함) 사람 수입니다. */
        int32                       countRemainingPlayers() const;
        const BrZone&               getZone() const { return _zone; }
        BrZone&                     getZoneMutable() { return _zone; }
        const MatchState&           getMatchState() const { return _matchState; }
        const vector<BrSupplyDrop>& getSupplyDrops() const { return _listSupplyDrop; }
        float32                     getTime() const { return _time; }

        /**
         * @brief 판(`MatchState`) · 자기장(`BrZone`) · 난수 · 고정 걸음 · 시간 · 다음 보급 자리, 사람마다 체력 · 부활 진행(붙은 팀원 · 진행량) · 가방(`Inventory`) ·
         *        장비 · 자리 · 팀 · 처치 · 기절시킨 수 · 기절시킨 사람 · 살리는 대상, 보급 상자를 씁니다. 카탈로그 셋 · 지형은 `initialize` 의 것이라 싣지 않고,
         *        알림은 읽을 때 비웁니다.
         */
        void writeState( Archive& outArchive ) const;
        /**
         * @brief `writeState` 의 바이트로 바꿉니다 — 사람은 카탈로그 설정으로 다시 세운 뒤 값을 읽습니다(`initialize` 한 판에 읽는다). 사람 수가 판의 참가자 수와
         *        다르거나, 없는 팀 · 아이템 칸 수가 다르거나 깨졌으면 false 이고 그대로입니다.
         */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        bool isValidPlayer( int32 player ) const { return player >= 0 && player < static_cast<int32>( _listPlayer.size() ); }
        /** @brief 카탈로그 설정으로 사람 하나를 세웁니다(체력 · 부활 · 장비 · 가방). */
        void initializePlayer( BrPlayer& outPlayer, int32 team, int32 participant ) const;
        void stepFixed( float32 deltaTime );
        void processVitality( int32 player );
        void handleDowned( int32 player, int32 instigator );
        void handleDeath( int32 player, int32 instigator );
        void interruptRevive( int32 target, bool bNotify );
        void finishRevive( int32 target );
        void resolveTeamWipe( int32 team );
        void dropSupply();
        void collectMatchEvents();
        void pushEvent( BrEvent::Kind kind, int32 player, int32 other, int32 team, int32 value );

        vector<BrPlayer>     _listPlayer;
        vector<BrSupplyDrop> _listSupplyDrop;
        EventBuffer<BrEvent> _eventBuffer;
        vector<MatchEvent>   _listMatchScratch;
        MatchState           _matchState;
        BrZone               _zone;
        GameRandom           _random;
        FixedStepTimer       _stepTimer;
        const BrCatalog*     _pCatalog;
        const ItemCatalog*   _pItemCatalog;
        const LootCatalog*   _pLootCatalog;
        float32              _time;
        int32                _nextSupplyDrop; ///< 다음에 떨어질 보급 상자 시각의 자리
    };
} // namespace sw
