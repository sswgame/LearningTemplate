/**
 * @file JRPGBattle.h
 * @brief 클래식 JRPG 전투입니다. 라운드제 턴 순서(기반 TurnOrder), 명령 선택과 실행, 타이밍 공격과 타이밍 방어(기반 TimingJudge, 씨 오브 스타즈),
 *        적 시전과 잠금(lock) 깨기, 콤보 포인트 합동기, 방어, 도망 확률, 무협 옵션(내공과 초식 숙련), 승리 보상 분배를 다룹니다.
 * @details 모든 난수는 시드 하나로 만든 `GameRandom` 이 만든다. 시드, 명령, 타이밍 입력이 같으면 같은 전투가 된다.
 */
#pragma once
#include "Core/Container/EventBuffer.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/FourCcUtil.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Character/Combat/TurnOrder.h"
#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Archive;
    class JRPGCatalog;
    class JRPGParty;
    class TimingJudge;

    /** @brief 멤버 명령 종류입니다. */
    enum class JRPGCommandKind : uint8
    {
        None = 0,
        Attack, ///< `_target` 은 적 인덱스
        Spell,  ///< `_id` 는 주문, 특기, 초식이고 `_target` 은 적 인덱스(피해) 또는 멤버 인덱스(회복과 부활)
        Combo,  ///< `_id` 는 합동기이며 참여 멤버의 이번 라운드 행동을 함께 쓴다
        Defend  ///< 먼저 움직이고 이번 라운드에 받는 피해를 줄인다
    };

    /** @brief 멤버 하나의 이번 라운드 명령입니다. */
    struct JRPGCommand
    {
        hashed_string   _id{};
        int32           _target{ 0 };
        JRPGCommandKind _kind{ JRPGCommandKind::None };

        static JRPGCommand makeAttack( int32 enemyIndex ) { return JRPGCommand{ hashed_string{}, enemyIndex, JRPGCommandKind::Attack }; }
        static JRPGCommand makeSpell( const hashed_string& spellID, int32 target ) { return JRPGCommand{ spellID, target, JRPGCommandKind::Spell }; }
        static JRPGCommand makeCombo( const hashed_string& comboID, int32 enemyIndex ) { return JRPGCommand{ comboID, enemyIndex, JRPGCommandKind::Combo }; }
        static JRPGCommand makeDefend() { return JRPGCommand{ hashed_string{}, 0, JRPGCommandKind::Defend }; }
    };
} // namespace sw

namespace sw
{
    /** @brief 타이밍 입력을 묻는 때입니다. */
    enum class JRPGTimingKind : uint8
    {
        Attack = 0, ///< 멤버가 기본 공격을 휘두를 때이며, 맞으면 추가 타격이 들어간다
        Block       ///< 멤버가 맞을 때이며, 맞으면 피해가 줄어든다
    };

    /**
     * @class IJRPGTimingInput
     * @brief 전투가 타이밍 순간마다 "목표 시각에서 얼마나 어긋나게 눌렀는가"를 묻는 인터페이스입니다. 실제 게임은 연출을 멈추고 입력을 기다린 결과를 반환하고, 테스트와 AI 는 정해 둔 값을 반환합니다.
     */
    class IJRPGTimingInput
    {
    public:
        IJRPGTimingInput()          = default;
        virtual ~IJRPGTimingInput() = default;

        IJRPGTimingInput( const IJRPGTimingInput& )            = default;
        IJRPGTimingInput& operator=( const IJRPGTimingInput& ) = default;

        /** @brief 눌렀으면 어긋난 시간(초, 음수면 일찍 누른 것)을 @p outOffset 에 두고 true 를 반환하며, 누르지 않았으면 false 를 반환합니다. */
        [[nodiscard]] virtual bool findPressOffset( JRPGTimingKind kind, int32 memberIndex, float32& outOffset ) const = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief 전투 규칙의 수치입니다. */
    struct JRPGBattleSettings
    {
        float32 _timedHitBonus{ 0.5f };       ///< 타이밍 공격에서 추가 타격이 주는 피해 비율
        float32 _timedBlockReduction{ 0.5f }; ///< 타이밍 방어에서 받는 피해에서 빼는 비율
        float32 _defendReduction{ 0.5f };     ///< 방어 명령에서 받는 피해에서 빼는 비율
        float32 _weaknessMultiplier{ 1.5f };  ///< 약점 유형을 맞혔을 때의 피해 배율
        float32 _fleeBase{ 0.5f };
        float32 _fleePerAgility{ 0.02f }; ///< 평균 민첩 차이 1마다 늘어나는 도망 확률
        float32 _fleePerAttempt{ 0.1f };  ///< 실패할 때마다 오르는 도망 확률
        int32   _comboPerHit{ 1 };        ///< 기본 공격 한 타마다 얻는 콤보 포인트
        int32   _comboMax{ 9 };
        int32   _innerPerAttack{ 10 }; ///< 무협 옵션에서 기본 공격 한 타마다 얻는 내공
        int32   _innerPerHitTaken{ 5 };
        bool    _bWuxia{ false }; ///< 무협 옵션이다(내공 게이지와 초식 내공 비용을 쓴다)
    };
} // namespace sw

namespace sw
{
    /** @brief 전투 중 적 하나입니다. */
    struct JRPGEnemyState
    {
        hashed_string         _enemyID{};
        vector<hashed_string> _listLock{}; ///< 남은 잠금
        int32                 _hp{ 1 };
        int32                 _castTurnsLeft{ 0 }; ///< 0이면 시전 중이 아니다
        int32                 _lockTotal{ 0 };
        int32                 _turnsTaken{ 0 };

        bool isAlive() const { return _hp > 0; }
        bool isCasting() const { return _castTurnsLeft > 0; }
    };
} // namespace sw

namespace sw
{
    /** @brief 전투의 끝입니다. */
    enum class JRPGBattleOutcome : uint8
    {
        Ongoing = 0,
        Victory,
        Defeat,
        Fled
    };

    /** @brief 전투에서 일어난 일입니다. `_bEnemyActor` 가 true 면 `_actor` 는 적 인덱스이고, 아니면 멤버 인덱스입니다(대상도 반대편입니다). */
    struct JRPGBattleEvent
    {
        enum class Kind : uint8
        {
            Attack = 0,
            TimedHit, ///< 타이밍 공격에 성공했다(다음 Damage 가 추가 타격이 된다)
            Damage,   ///< `_value` 는 피해
            TimedBlock,
            Defending,
            SpellCast,
            Healed,
            Revived,
            NotEnoughMP,
            NotEnoughInner,
            CannotUse,   ///< 배우지 않은 주문이거나 열리지 않은 초식이다
            CastStarted, ///< `_value` 는 잠금 수
            LockBroken,  ///< `_id` 는 깬 유형, `_value` 는 남은 잠금
            CastCancelled,
            CastReleased, ///< `_value` 는 위력(깬 잠금만큼 줄었다)
            ComboPoints,  ///< `_value` 는 지금 콤보 포인트
            ComboUsed,
            ProficiencyGained,
            Defeated,
            FleeFailed,
            Fled,
            Victory, ///< `_value` 는 한 멤버의 경험치 몫
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
     * @class JRPGBattle
     * @brief 전투 하나를 진행합니다. 파티는 빌려 쓰므로 HP, MP, 내공, 숙련이 그대로 남고, 적은 전투가 만들어 가지고 있습니다.
     * @details 한 라운드는 멤버마다 `setCommand` 를 호출한 뒤 `resolveRound` 를 호출하는 순서다. 행동 순서는 우선도(방어가 +1), 민첩, 시드 난수 순으로 정한다.
     *          피해(DQ 식)는 (공격력 / 2 − 방어력 / 4) × 0.875..1.125 이고, 1 미만이면 0 또는 1이다. 주문은 위력 × (100 + 지능) / 100 × 0.9..1.1 이다.
     *          시전하는 적은 `castEvery` 번째 차례마다 시전을 시작하고 `castTurns` 차례 뒤에 터뜨린다. 그 사이 잠금과 같은 유형으로 맞히면 하나씩 깨지고,
     *          모두 깨면 시전이 취소된다. 깬 만큼 위력이 (남은 수 + 1) / (전체 + 1) 로 줄어든다.
     */
    class SW_KIT_API JRPGBattle
    {
    public:
        static constexpr uint32 kStateTag       = FourCcUtil::make( "JBTL" );
        static constexpr uint32 kStateVersion   = 1;
        static constexpr int32  kEnemyActorBase = 100; ///< TurnOrder 의 적 번호는 이 값에 적 인덱스를 더한 값이다
        static constexpr int32  kDefendPriority = 1;

        JRPGBattle();

        /** @brief 카탈로그와 판정 창을 빌립니다(판정이 없으면 타이밍은 항상 실패합니다). */
        void initialize( const JRPGCatalog* pCatalog, const TimingJudge* pJudge, const JRPGBattleSettings& settings, uint32 seed );
        void setTimingInput( const IJRPGTimingInput* pTimingInput ) { _pTimingInput = pTimingInput; }
        /** @brief 파티와 적 목록으로 전투를 시작합니다. 모르는 적은 빼고, 남은 적이 없으면 false 를 반환합니다. */
        [[nodiscard]] bool start( JRPGParty* pParty, const vector<hashed_string>& listEnemyID );
        /** @brief 멤버의 이번 라운드 명령을 정합니다. 쓸 수 없는 명령(쓰러진 멤버, 모르는 주문, 포인트 부족, 참여 멤버가 쓰러진 합동기)이면 false 를 반환합니다. */
        [[nodiscard]] bool setCommand( int32 memberIndex, const JRPGCommand& command );
        void               resolveRound();
        /**
         * @brief 도망칩니다. 성공하면 전투가 끝나고(Fled), 실패하면 이번 라운드의 멤버 명령을 모두 버리고 적만 행동합니다. 보스가 있으면 항상 실패합니다.
         * @return 도망쳤으면 true 입니다.
         */
        [[nodiscard]] bool tryFlee();
        void               drainEvents( vector<JRPGBattleEvent>& outListEvent );
        /** @brief 적, 이번 라운드 명령, 방어, 턴 순서, 난수, 보상, 콤보, 라운드, 도망 횟수, 결과를 씁니다. 파티, 카탈로그, 판정, 타이밍 입력, 설정은 싣지 않습니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief 전투가 빌려 쓸 파티를 연결합니다. 새 전투에 `readState` 하기 전에 호출해야 합니다(파티는 `JRPGParty::readState` 로 따로 읽습니다). `start` 도 파티를 연결합니다. */
        void bindParty( JRPGParty* pParty ) { _pParty = pParty; }
        /**
         * @brief `writeState` 가 쓴 바이트로 상태를 바꿉니다. 모르는 적이거나, 명령 수가 연결한 파티의 멤버 수와 다르거나, 바이트가 깨졌으면 상태를 바꾸지 않고 false 를 반환합니다.
         *        시작 전 전투는 파티를 연결하지 않아도 됩니다.
         */
        [[nodiscard]] bool readState( Archive& archive );

        /** @brief DQ 식 물리 피해를 계산합니다. */
        static int32 computePhysicalDamage( int32 attack, int32 defense, GameRandom& random );
        /** @brief 지금 도망칠 확률을 반환합니다(0.05..0.95이며, 보스가 있으면 0입니다). */
        float32 computeFleeChance() const;

        JRPGBattleOutcome             getOutcome() const { return _outcome; }
        const vector<JRPGEnemyState>& getEnemies() const { return _listEnemy; }
        int32                         getComboPoints() const { return _comboPoints; }
        int32                         getRound() const { return _round; }
        int64                         getRewardExp() const { return _rewardExp; }
        int64                         getRewardGold() const { return _rewardGold; }

    private:
        void pushEvent( JRPGBattleEvent::Kind kind, bool bEnemyActor, int32 actor, int32 target, int32 value = 0, const hashed_string& id = hashed_string{} );
        void syncActors();
        void executeMember( int32 memberIndex );
        void executeAttack( int32 memberIndex, int32 enemyIndex );
        void executeSpell( int32 memberIndex, const JRPGCommand& command );
        void executeCombo( int32 memberIndex, const JRPGCommand& command );
        void executeEnemy( int32 enemyIndex );
        /** @brief 적에게 피해를 주고 잠금을 깹니다. 유형이 약점이면 배율을 곱합니다. */
        void  hitEnemy( int32 memberIndex, int32 enemyIndex, int32 damage, const vector<hashed_string>& listDamageType );
        void  hitMember( int32 enemyIndex, int32 memberIndex, int32 damage );
        void  addComboPoints( int32 amount );
        void  addInner( int32 memberIndex, int32 amount );
        bool  isTimed( JRPGTimingKind kind, int32 memberIndex ) const;
        int32 findLivingEnemy( int32 preferred ) const;
        void  finishIfDecided();

        vector<JRPGEnemyState>       _listEnemy;
        vector<JRPGCommand>          _listCommand;   ///< 멤버 인덱스마다의 명령
        vector<uint8>                _listDefending; ///< 이번 라운드에 방어 중인지(멤버 인덱스마다)
        EventBuffer<JRPGBattleEvent> _eventBuffer;
        TurnOrder                    _turnOrder;
        GameRandom                   _random;
        JRPGBattleSettings           _settings;
        const JRPGCatalog*           _pCatalog;
        const TimingJudge*           _pJudge;
        const IJRPGTimingInput*      _pTimingInput;
        JRPGParty*                   _pParty;
        int64                        _rewardExp;
        int64                        _rewardGold;
        int32                        _comboPoints;
        int32                        _round;
        int32                        _fleeAttempts;
        JRPGBattleOutcome            _outcome;
    };
} // namespace sw
