/**
 * @file JrpgBattle.h
 * @brief 클래식 JRPG 전투 — 라운드제 턴 순서(기반 TurnOrder), 명령 선택 → 실행, 타이밍 공격 · 타이밍 방어(기반 TimingJudge — 씨 오브 스타즈),
 *        적 시전과 잠금(lock) 깨기, 콤보 포인트 합동기, 방어, 도망 확률, 무협 옵션(내공 · 초식 숙련), 승리 보상 분배입니다.
 * @details 모든 난수는 씨앗 하나의 `GameRandom` 입니다 — 씨앗 · 명령 · 타이밍 입력이 같으면 같은 전투입니다.
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
#include "GameFramework/Kits/Genre/Rpg/ClassicJrpg/JrpgCatalog.h"

namespace sw
{
    class Archive;
    class JrpgParty;
    class TimingJudge;

    /** @brief 멤버 명령 종류입니다. */
    enum class JrpgCommandKind : uint8
    {
        None = 0,
        Attack, ///< `_target` = 적 자리
        Spell,  ///< `_id` = 주문 · 특기 · 초식, `_target` = 적 자리(피해) 또는 멤버 자리(회복 · 부활)
        Combo,  ///< `_id` = 합동기 — 참여 멤버의 이번 라운드 행동을 함께 쓴다
        Defend  ///< 먼저 움직이고 이번 라운드 받는 피해를 줄인다
    };

    /** @brief 멤버 하나의 이번 라운드 명령입니다. */
    struct JrpgCommand
    {
        hashed_string   _id{};
        int32           _target{ 0 };
        JrpgCommandKind _kind{ JrpgCommandKind::None };

        static JrpgCommand makeAttack( int32 enemyIndex ) { return JrpgCommand{ hashed_string{}, enemyIndex, JrpgCommandKind::Attack }; }
        static JrpgCommand makeSpell( const hashed_string& spellID, int32 target ) { return JrpgCommand{ spellID, target, JrpgCommandKind::Spell }; }
        static JrpgCommand makeCombo( const hashed_string& comboID, int32 enemyIndex ) { return JrpgCommand{ comboID, enemyIndex, JrpgCommandKind::Combo }; }
        static JrpgCommand makeDefend() { return JrpgCommand{ hashed_string{}, 0, JrpgCommandKind::Defend }; }
    };
} // namespace sw

namespace sw
{
    /** @brief 타이밍 입력을 묻는 때입니다. */
    enum class JrpgTimingKind : uint8
    {
        Attack = 0, ///< 멤버가 기본 공격을 휘두를 때 — 맞으면 추가 타격
        Block       ///< 멤버가 맞을 때 — 맞으면 피해 감소
    };

    /**
     * @class IJrpgTimingInput
     * @brief 전투가 타이밍 순간마다 "목표 시각에서 얼마나 어긋나게 눌렀는가" 를 묻습니다. 실제 게임은 연출을 멈추고 입력을 기다린 결과를, 시험 · AI 는 정해 둔 값을 줍니다.
     */
    class SW_GF_API IJrpgTimingInput
    {
    public:
        IJrpgTimingInput()          = default;
        virtual ~IJrpgTimingInput() = default;

        IJrpgTimingInput( const IJrpgTimingInput& )            = default;
        IJrpgTimingInput& operator=( const IJrpgTimingInput& ) = default;

        /** @brief 눌렀으면 어긋난 시간(초, 음수 = 이르다)을 @p outOffset 에 두고 true, 누르지 않았으면 false 입니다. */
        virtual bool findPressOffset( JrpgTimingKind kind, int32 memberIndex, float32& outOffset ) const = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief 전투 규칙의 수치입니다. */
    struct JrpgBattleSettings
    {
        float32 _timedHitBonus{ 0.5f };       ///< 타이밍 공격 — 추가 타격의 피해 비율
        float32 _timedBlockReduction{ 0.5f }; ///< 타이밍 방어 — 받는 피해에서 빼는 비율
        float32 _defendReduction{ 0.5f };     ///< 방어 명령 — 받는 피해에서 빼는 비율
        float32 _weaknessMultiplier{ 1.5f };  ///< 약점 유형
        float32 _fleeBase{ 0.5f };
        float32 _fleePerAgility{ 0.02f }; ///< 평균 민첩 차 1 마다
        float32 _fleePerAttempt{ 0.1f };  ///< 실패할 때마다 오른다
        int32   _comboPerHit{ 1 };        ///< 기본 공격 한 타마다 콤보 포인트
        int32   _comboMax{ 9 };
        int32   _innerPerAttack{ 10 }; ///< 무협 — 기본 공격 한 타마다 내공
        int32   _innerPerHitTaken{ 5 };
        bool    _bWuxia{ false }; ///< 무협 옵션 — 내공 게이지 · 초식 내공 비용
    };
} // namespace sw

namespace sw
{
    /** @brief 전투 중 적 하나입니다. */
    struct JrpgEnemyState
    {
        hashed_string         _enemyID{};
        vector<hashed_string> _listLock{}; ///< 남은 잠금
        int32                 _hp{ 1 };
        int32                 _castTurnsLeft{ 0 }; ///< 0 = 시전 중이 아니다
        int32                 _lockTotal{ 0 };
        int32                 _turnsTaken{ 0 };

        bool isAlive() const { return _hp > 0; }
        bool isCasting() const { return _castTurnsLeft > 0; }
    };
} // namespace sw

namespace sw
{
    /** @brief 전투의 끝입니다. */
    enum class JrpgBattleOutcome : uint8
    {
        Ongoing = 0,
        Victory,
        Defeat,
        Fled
    };

    /** @brief 전투에서 일어난 일입니다. `_bEnemyActor` 면 `_actor` 는 적 자리, 아니면 멤버 자리입니다(대상도 반대편). */
    struct JrpgBattleEvent
    {
        enum class Kind : uint8
        {
            Attack = 0,
            TimedHit, ///< 타이밍 공격 성공 — 다음 Damage 가 추가 타격
            Damage,   ///< `_value` = 피해
            TimedBlock,
            Defending,
            SpellCast,
            Healed,
            Revived,
            NotEnoughMp,
            NotEnoughInner,
            CannotUse,   ///< 배우지 않은 주문 · 열리지 않은 초식
            CastStarted, ///< `_value` = 잠금 수
            LockBroken,  ///< `_id` = 깬 유형, `_value` = 남은 잠금
            CastCancelled,
            CastReleased, ///< `_value` = 위력(깬 잠금만큼 줄었다)
            ComboPoints,  ///< `_value` = 지금 콤보 포인트
            ComboUsed,
            ProficiencyGained,
            Defeated,
            FleeFailed,
            Fled,
            Victory, ///< `_value` = 한 멤버의 경험치 몫
            Defeat
        };
        hashed_string _id{};
        int32         _actor{ -1 };
        int32         _target{ -1 };
        int32         _value{ 0 };
        Kind          _kind{ Kind::Attack };
        bool          _bEnemyActor{ false };
    };
} // namespace sw

namespace sw
{
    /**
     * @class JrpgBattle
     * @brief 파티는 빌려 씁니다(HP · MP · 내공 · 숙련이 그대로 남는다). 적은 전투가 만들어 들고 있습니다.
     * @details 한 라운드: 멤버마다 `setCommand` → `resolveRound`. 순서는 우선도(방어 +1) → 민첩 → 씨앗 난수입니다.
     *          피해(DQ 식) = (공격력 / 2 − 방어력 / 4) × 0.875..1.125, 1 미만이면 0 또는 1. 주문 = 위력 × (100 + 지능) / 100 × 0.9..1.1.
     *          시전하는 적은 `castEvery` 번째 차례마다 시전을 시작하고 `castTurns` 차례 뒤에 터뜨립니다. 그 사이 잠금과 같은 유형으로 맞히면 하나씩 깨지고,
     *          모두 깨면 시전이 취소됩니다. 깬 만큼 위력이 (남은 수 + 1) / (전체 + 1) 로 줄어듭니다.
     */
    class SW_GF_API JrpgBattle
    {
    public:
        static constexpr uint32 kStateTag       = FourCcUtil::make( "JBTL" );
        static constexpr uint32 kStateVersion   = 1;
        static constexpr int32  kEnemyActorBase = 100; ///< TurnOrder 의 적 번호 = 이것 + 자리
        static constexpr int32  kDefendPriority = 1;

        JrpgBattle();

        /** @brief 카탈로그 · 판정 창을 빌립니다(판정이 없으면 타이밍은 늘 실패). */
        void initialize( const JrpgCatalog* pCatalog, const TimingJudge* pJudge, const JrpgBattleSettings& settings, uint32 seed );
        void setTimingInput( const IJrpgTimingInput* pTimingInput ) { _pTimingInput = pTimingInput; }
        /** @brief 파티와 적 목록으로 시작합니다. 모르는 적은 빼고, 남은 적이 없으면 false 입니다. */
        [[nodiscard]] bool start( JrpgParty* pParty, const vector<hashed_string>& listEnemyID );
        /** @brief 멤버의 이번 라운드 명령입니다. 쓸 수 없는 명령(쓰러진 멤버 · 모르는 주문 · 포인트 부족 · 참여 멤버가 쓰러진 합동기)은 false 입니다. */
        [[nodiscard]] bool setCommand( int32 memberIndex, const JrpgCommand& command );
        void               resolveRound();
        /**
         * @brief 도망칩니다. 성공하면 끝(Fled), 실패하면 이번 라운드 멤버 명령을 모두 버리고 적만 행동합니다. 보스가 있으면 늘 실패입니다.
         * @return 도망쳤으면 true 입니다.
         */
        [[nodiscard]] bool tryFlee();
        void               drainEvents( vector<JrpgBattleEvent>& outListEvent );
        /** @brief 적 · 이번 라운드 명령 · 방어 · 턴 순서 · 난수 · 보상 · 콤보 · 라운드 · 도망 횟수 · 결과를 씁니다. 파티 · 카탈로그 · 판정 · 타이밍 입력 · 설정은 싣지 않는다. */
        void writeState( Archive& outArchive ) const;
        /** @brief 전투가 빌려 쓸 파티를 묶습니다 — 새 전투에 `readState` 하기 전에(파티는 따로 싣는다 — `JrpgParty::readState`). `start` 도 묶는다. */
        void bindParty( JrpgParty* pParty ) { _pParty = pParty; }
        /**
         * @brief `writeState` 의 바이트로 바꿉니다. 모르는 적이거나 명령 수가 묶은 파티의 멤버 수와 다르거나 깨졌으면 false 이고 그대로입니다.
         *        시작 전 전투는 파티를 묶지 않아도 된다.
         */
        [[nodiscard]] bool readState( Archive& archive );

        /** @brief DQ 식 물리 피해입니다. */
        static int32 computePhysicalDamage( int32 attack, int32 defense, GameRandom& random );
        /** @brief 지금 도망칠 확률(0.05..0.95, 보스가 있으면 0)입니다. */
        float32 computeFleeChance() const;

        JrpgBattleOutcome             getOutcome() const { return _outcome; }
        const vector<JrpgEnemyState>& getEnemies() const { return _listEnemy; }
        int32                         getComboPoints() const { return _comboPoints; }
        int32                         getRound() const { return _round; }
        int64                         getRewardExp() const { return _rewardExp; }
        int64                         getRewardGold() const { return _rewardGold; }

    private:
        void pushEvent( JrpgBattleEvent::Kind kind, bool bEnemyActor, int32 actor, int32 target, int32 value = 0, const hashed_string& id = hashed_string{} );
        void syncActors();
        void executeMember( int32 memberIndex );
        void executeAttack( int32 memberIndex, int32 enemyIndex );
        void executeSpell( int32 memberIndex, const JrpgCommand& command );
        void executeCombo( int32 memberIndex, const JrpgCommand& command );
        void executeEnemy( int32 enemyIndex );
        /** @brief 적에게 피해를 주고 잠금을 깹니다. 유형이 약점이면 배율을 곱합니다. */
        void  hitEnemy( int32 memberIndex, int32 enemyIndex, int32 damage, const vector<hashed_string>& listDamageType );
        void  hitMember( int32 enemyIndex, int32 memberIndex, int32 damage );
        void  addComboPoints( int32 amount );
        void  addInner( int32 memberIndex, int32 amount );
        bool  isTimed( JrpgTimingKind kind, int32 memberIndex ) const;
        int32 findLivingEnemy( int32 preferred ) const;
        void  finishIfDecided();

        vector<JrpgEnemyState>       _listEnemy;
        vector<JrpgCommand>          _listCommand;   ///< 멤버 자리마다
        vector<uint8>                _listDefending; ///< 이번 라운드 방어 중(멤버 자리마다)
        EventBuffer<JrpgBattleEvent> _eventBuffer;
        TurnOrder                    _turnOrder;
        GameRandom                   _random;
        JrpgBattleSettings           _settings;
        const JrpgCatalog*           _pCatalog;
        const TimingJudge*           _pJudge;
        const IJrpgTimingInput*      _pTimingInput;
        JrpgParty*                   _pParty;
        int64                        _rewardExp;
        int64                        _rewardGold;
        int32                        _comboPoints;
        int32                        _round;
        int32                        _fleeAttempts;
        JrpgBattleOutcome            _outcome;
    };
} // namespace sw
