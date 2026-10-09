/**
 * @file MonsterBattle.h
 * @brief 몬스터 1:1 라운드 전투 — 우선도 → 스피드 순 행동(기반 TurnOrder 라운드제), 피해 공식(레벨 · 공격/방어 · 위력 · 자속 1.5 · 상성 · 급소 · 난수 0.85~1),
 *        명중, 상태이상(독 · 맹독 · 화상 · 마비 · 수면 · 얼음), 능력 변화 단계(−6..+6), 날씨, 교체, 포획(흔들림), 도망, 경험치 · 노력치 지급입니다.
 * @details 모든 난수는 씨앗 하나의 `GameRandom` 에서 나옵니다 — 씨앗과 명령이 같으면 같은 전투입니다(리플레이 · 시험 · 넷 턴 릴레이).
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Actor/Combat/TurnOrder.h"
#include "GameFramework/Base/Foundation/Utility/EventBuffer.h"
#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Rpg/MonsterCollector/MonsterInstance.h"

namespace sw
{
    class Archive;
    class ElementChart;

    /** @brief 한 라운드의 행동 종류입니다. */
    enum class MonsterActionKind : uint8
    {
        None = 0,
        Move,   ///< `_index` = 기술 칸
        Switch, ///< `_index` = 파티 자리
        Ball,   ///< 야생전만 — `_ballMultiplier` = 볼 배율
        Run     ///< 야생전만
    };

    /** @brief 한 라운드의 행동입니다. */
    struct MonsterAction
    {
        float32           _ballMultiplier{ 1.0f };
        int32             _index{ 0 };
        MonsterActionKind _kind{ MonsterActionKind::None };

        static MonsterAction makeMove( int32 slot ) { return MonsterAction{ 1.0f, slot, MonsterActionKind::Move }; }
        static MonsterAction makeSwitch( int32 partyIndex ) { return MonsterAction{ 1.0f, partyIndex, MonsterActionKind::Switch }; }
        static MonsterAction makeBall( float32 ballMultiplier ) { return MonsterAction{ ballMultiplier, 0, MonsterActionKind::Ball }; }
        static MonsterAction makeRun() { return MonsterAction{ 1.0f, 0, MonsterActionKind::Run }; }
    };
} // namespace sw

namespace sw
{
    /** @brief 전투의 끝입니다. */
    enum class MonsterBattleOutcome : uint8
    {
        Ongoing = 0,
        Won,
        Lost,
        Captured,
        Escaped
    };

    /** @brief 피해 공식의 입력 하나입니다(능력 변화 · 성격은 이미 반영한 실수치). */
    struct MonsterDamageInput
    {
        float32 _typeMultiplier{ 1.0f };
        float32 _weatherMultiplier{ 1.0f };
        int32   _level{ 1 };
        int32   _attack{ 1 };
        int32   _defense{ 1 };
        int32   _power{ 0 };
        int32   _randomPercent{ 100 }; ///< 85..100
        bool    _bStab{ false };
        bool    _bCritical{ false };
        bool    _bBurned{ false }; ///< 화상 + 물리 기술이면 절반
    };
} // namespace sw

namespace sw
{
    /** @brief 포획 판정 결과입니다. */
    struct MonsterCaptureResult
    {
        int32 _catchValue{ 0 };  ///< 보정 포획률 a(255 이상이면 반드시 잡힌다)
        int32 _shakeChance{ 0 }; ///< 흔들림 한 번의 기준 b(0..65535 와 비교)
        int32 _shakes{ 0 };      ///< 흔들린 횟수(0..3 — 화면 연출)
        bool  _bCaught{ false };
    };
} // namespace sw

namespace sw
{
    /** @brief 전투에서 일어난 일입니다. */
    struct MonsterBattleEvent
    {
        enum class Kind : uint8
        {
            MoveUsed = 0,
            NoPp,
            Missed,
            Immune, ///< 상성 0 배
            Damage, ///< `_value` = 피해, `_multiplier` = 상성
            Critical,
            StatusApplied,  ///< `_value` = MonsterStatus
            StatusDamage,   ///< 독 · 맹독 · 화상의 턴 끝 피해
            StatChanged,    ///< `_value` = 실제 바뀐 단계(0 = 더 바뀌지 않는다), `_id` = 능력치 이름
            FullyParalyzed, ///< 몸이 저려 움직일 수 없다
            Asleep,
            Woke,
            Frozen,
            Thawed,
            WeatherStarted,
            WeatherDamage,
            WeatherEnded,
            Switched, ///< `_value` = 파티 자리
            Fainted,
            NeedsSwitch, ///< 쓰러진 쪽에 남은 개체가 있다 — `switchFainted` 를 기다린다
            ExpGained,   ///< `_value` = 경험치
            LevelUp,     ///< `_value` = 새 레벨
            LearnedMove,
            MoveLearnBlocked,
            CanEvolve,
            CaptureShake, ///< `_value` = 몇 번째 흔들림
            Captured,
            BrokeFree,
            Escaped,
            EscapeFailed,
            BattleEnded ///< `_value` = MonsterBattleOutcome
        };
        hashed_string _id{}; ///< 기술 · 종 · 날씨
        float32       _multiplier{ 1.0f };
        int32         _side{ 0 };
        int32         _value{ 0 };
        Kind          _kind{ Kind::MoveUsed };
    };
} // namespace sw

namespace sw
{
    /**
     * @class MonsterBattle
     * @brief 두 편(0 = 플레이어, 1 = 상대)의 파티를 복사해 들고 싸웁니다. 끝나면 `getParty` 로 HP · PP · 경험치를 돌려받습니다.
     * @details 한 라운드: 양쪽 `setAction` → `resolveRound`. 교체 · 볼 · 도망은 기술보다 먼저(우선도 +7), 기술끼리는 우선도 → 스피드(능력 변화 · 마비 반영) → 씨앗 난수 순입니다.
     *          쓰러진 개체의 남은 행동은 버립니다. 라운드 끝에 날씨 피해 → 상태이상 피해 → 날씨 턴 감소입니다.
     */
    class SW_GF_API MonsterBattle
    {
    public:
        static constexpr uint32 kStateTag        = FourCcUtil::make( "MCBT" );
        static constexpr uint32 kStateVersion    = 1;
        static constexpr int32  kPlayerSide      = 0;
        static constexpr int32  kFoeSide         = 1;
        static constexpr int32  kSideCount       = 2;
        static constexpr int32  kMinStage        = -6;
        static constexpr int32  kMaxStage        = 6;
        static constexpr int32  kNonMovePriority = 7; ///< 교체 · 볼 · 도망
        static constexpr int32  kParalysisChance = 25;
        static constexpr int32  kThawChance      = 20;
        static constexpr int32  kMaxSleepTurns   = 3;

        MonsterBattle();

        /** @brief 카탈로그와 상성표를 빌립니다(이 전투보다 오래 살아야 한다). */
        void initialize( const MonsterCollectorCatalog* pCatalog, const ElementChart* pChart, uint32 seed );
        /** @brief 양쪽 파티로 전투를 시작합니다. 맨 앞의 싸울 수 있는 개체가 나섭니다. @p bWild 면 볼 · 도망이 됩니다. */
        void start( const vector<MonsterInstance>& listPlayer, const vector<MonsterInstance>& listFoe, bool bWild );
        /** @brief 날씨를 겁니다. @p turns 가 0 이하면 끝없이(필드 날씨)입니다. 빈 id 면 날씨를 걷습니다. */
        void setWeather( const hashed_string& weatherId, int32 turns );
        /** @brief 이번 라운드 행동을 둡니다. 쓸 수 없는 행동(없는 칸 · 기절한 자리 · 트레이너전의 볼)이면 false 입니다. */
        [[nodiscard]] bool setAction( int32 side, const MonsterAction& action );
        /** @brief 라운드를 풉니다. 행동을 두지 않은 쪽은 아무것도 하지 않습니다. */
        void resolveRound();
        /** @brief 쓰러진 자리를 @p partyIndex 로 채웁니다(`NeedsSwitch` 다음). */
        [[nodiscard]] bool switchFainted( int32 side, int32 partyIndex );
        void               drainEvents( vector<MonsterBattleEvent>& outListEvent );
        /** @brief 양쪽(개체 · 능력 변화 · 둔 행동 · 나선 자리 · 교체 대기) · 턴 순서 · 난수 · 잡은 개체 · 날씨 · 도망 횟수 · 결과 · 야생 여부를 씁니다. 카탈로그 · 상성표는 싣지 않는다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다. 모르는 종 · 기술 · 날씨거나 깨졌으면 false 이고 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

        /** @brief 피해 공식입니다. 단계마다 내림하고 상성이 0 이 아니면 최소 1 입니다. */
        static int32 computeDamage( const MonsterDamageInput& input );
        /**
         * @brief 3 · 4 세대 포획 공식입니다. a = ⌊(3M − 2H) × 포획률 × 볼 / 3M⌋ × 상태(수면 · 얼음 2, 마비 · 독 · 화상 1.5),
         *        b = ⌊1048560 / √√⌊16711680 / a⌋⌋, 0..65535 를 네 번 굴려 모두 b 미만이면 잡힙니다.
         */
        static MonsterCaptureResult computeCapture( int32 maxHp, int32 hp, int32 catchRate, float32 ballMultiplier, MonsterStatus status, GameRandom& random );
        /** @brief 능력 변화 단계의 배율 — +n 은 (2+n)/2, −n 은 2/(2+n) 입니다. */
        static float32 computeStageMultiplier( int32 stage );
        /** @brief 급소 단계의 확률 분모 — 0: 24, 1: 8, 2: 2, 3 이상: 1 입니다. */
        static int32 computeCriticalDivisor( int32 critStage );

        /** @brief 공격 타입이 방어 개체에 주는 상성 배율입니다(복합 타입은 곱). */
        float32 computeTypeMultiplier( const hashed_string& moveType, const MonsterInstance& defender ) const;
        /**
         * @brief 기대 피해(난수 평균 0.925 · 급소 없음 · 명중률 곱)입니다 — 트레이너 AI 가 씁니다.
         * @param attackerSide 능력 변화를 반영할 편(−1 이면 변화 없음 — 아직 나오지 않은 개체). @p defenderSide 도 같다.
         */
        float32 computeExpectedDamage( int32 attackerSide, const MonsterInstance& attacker, const hashed_string& moveId, int32 defenderSide,
                                       const MonsterInstance& defender ) const;
        /** @brief 지금 나선 개체의 실효 스피드(능력 변화 · 마비 반영)입니다. */
        float32 computeEffectiveSpeed( int32 side ) const;

        MonsterBattleOutcome           getOutcome() const { return _outcome; }
        bool                           isWild() const { return _bWild; }
        int32                          getRound() const { return _turnOrder.getRound(); }
        int32                          getActiveIndex( int32 side ) const { return _arrSide[side]._activeIndex; }
        const MonsterInstance&         getActive( int32 side ) const;
        const vector<MonsterInstance>& getParty( int32 side ) const { return _arrSide[side]._listMonster; }
        int32                          getStage( int32 side, MonsterStat stat ) const { return _arrSide[side]._arrStage[static_cast<size_t>( stat )]; }
        bool                           needsSwitch( int32 side ) const { return _arrSide[side]._bNeedsSwitch; }
        const hashed_string&           getWeather() const { return _weatherId; }
        int32                          getWeatherTurns() const { return _weatherTurns; }
        const MonsterInstance&         getCaptured() const { return _captured; }
        const MonsterCollectorCatalog* getCatalog() const { return _pCatalog; }
        const ElementChart*            getChart() const { return _pChart; }

    private:
        struct Side
        {
            vector<MonsterInstance> _listMonster{};
            int32                   _arrStage[kMonsterStatCount]{ 0, 0, 0, 0, 0, 0 };
            MonsterAction           _action{};
            int32                   _activeIndex{ 0 };
            bool                    _bNeedsSwitch{ false };
        };

        MonsterInstance& getActiveMutable( int32 side ) { return _arrSide[side]._listMonster[static_cast<size_t>( _arrSide[side]._activeIndex )]; }
        void             pushEvent( MonsterBattleEvent::Kind kind, int32 side, int32 value = 0, const hashed_string& id = hashed_string{}, float32 multiplier = 1.0f );
        void             executeAction( int32 side );
        void             executeMove( int32 side, int32 slot );
        void             executeBall( float32 ballMultiplier );
        void             executeRun();
        void             switchTo( int32 side, int32 partyIndex );
        /** @brief 행동 전 상태이상(수면 · 얼음 · 마비)입니다. 행동할 수 있으면 true 입니다. */
        bool    canActThisTurn( int32 side );
        void    applyStatus( int32 targetSide, MonsterStatus status );
        void    applyStatChange( int32 targetSide, MonsterStat stat, int32 stages );
        void    applyDamage( int32 side, int32 amount, MonsterBattleEvent::Kind kind, const hashed_string& id );
        void    handleFaint( int32 side );
        void    awardExp( const MonsterInstance& defeated );
        void    applyEndOfRound();
        int32   computeStatWithStage( int32 side, const MonsterInstance& monster, MonsterStat stat, bool bCritical, bool bAttacker ) const;
        float32 computeWeatherMultiplier( const hashed_string& moveType ) const;
        int32   findFirstUsable( int32 side ) const;
        void    setOutcome( MonsterBattleOutcome outcome );

        Side                            _arrSide[kSideCount];
        EventBuffer<MonsterBattleEvent> _eventBuffer;
        TurnOrder                       _turnOrder;
        GameRandom                      _random;
        MonsterInstance                 _captured;
        hashed_string                   _weatherId;
        const MonsterCollectorCatalog*  _pCatalog;
        const ElementChart*             _pChart;
        int32                           _weatherTurns;
        int32                           _escapeAttempts;
        MonsterBattleOutcome            _outcome;
        bool                            _bWild;
    };
} // namespace sw
