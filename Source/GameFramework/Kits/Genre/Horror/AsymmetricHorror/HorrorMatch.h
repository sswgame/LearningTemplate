/**
 * @file HorrorMatch.h
 * @brief 1 대 4 비대칭 공포 한 판(서버 권위) — 생존자(건강 → 부상 → 빈사 → 들림 → 갈고리 1 · 2 · 3 단계, 구출 · 치료 · 몸부림), 발전기(다인 수리 · 스킬 체크 ·
 *        폭발 소음 · 걷어차기 퇴행), 탈출구 전원 · 열기 · 해치 · 엔드게임 붕괴, 판자 · 창틀 · 사물함, 살인마(속도 · 공격 쿨다운 · 헛방 · 위협 반경 · 능력),
 *        블러드포인트, 결과(탈출 · 희생)입니다.
 * @details 고정 걸음(`FixedStepTimer`)으로만 흐르고 스킬 체크는 씨앗으로 정해져 같은 호출이면 같은 결과입니다. 기반을 이렇게 씁니다.
 *          - 체력 상태: `Vitality`(체력 2 = 건강 · 1 = 부상 · 기절 = 빈사, 출혈 체력 = 남은 출혈 초, 기절 중 부활 = 빈사 회복)
 *          - 수리 · 치료 · 탈출구: `InteractionProgress`(인원 배율 · 스킬 체크 `TimingJudge` · 실패 소음 · 퇴행)
 *          - 판 · 결과: `MatchState`(생존자 팀 · 살인마 팀, 탈출 · 희생은 탈락, 점수)
 *          - 살인마 봇의 감각: `collectKillerStimuli` 가 `AIPerception::sense` 에 넣을 자극(위치 · 소음 반경)을 채운다.
 *          위치는 XZ 평면(y = 0)이고 이동은 `moveSurvivor` · `moveKiller` 로 — 키트가 상태에 맞는 속도를 곱합니다(벽 충돌은 게임이).
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Actor/Combat/Health/Vitality.h"
#include "GameFramework/Base/Foundation/Data/StatBlock.h"
#include "GameFramework/Base/Foundation/Utility/EventBuffer.h"
#include "GameFramework/Base/Foundation/Utility/Time/Countdown.h"
#include "GameFramework/Base/Foundation/Utility/Time/FixedStepTimer.h"
#include "GameFramework/Base/Gameplay/Interaction/InteractionProgress.h"
#include "GameFramework/Base/Gameplay/Match/MatchState.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct AIStimulus;
    struct HorrorKillerDef;
    struct HorrorSnapshot;

    class Archive;
    class AsymmetricHorrorRulesCatalog;
    class BitWriter;

    /** @brief 생존자의 상태입니다. */
    enum class SurvivorState : uint8
    {
        Healthy = 0,
        Injured,
        Dying,   ///< 빈사 — 기어가며 출혈, 치료로 부상까지 회복
        Carried, ///< 살인마가 들고 있다(몸부림)
        Hooked,  ///< 갈고리(`_hookStage` 1 = 버티기, 2 = 몸부림)
        Sacrificed,
        Escaped
    };

    /** @brief 생존자가 붙어 있는 일입니다. */
    enum class SurvivorActivity : uint8
    {
        None = 0,
        Repairing,   ///< `_activityTarget` = 발전기
        Healing,     ///< `_activityTarget` = 치료받는 생존자
        OpeningGate, ///< `_activityTarget` = 탈출구
        Vaulting,    ///< `_activityTarget` = 창틀
        InLocker     ///< `_activityTarget` = 사물함
    };

    /** @brief 판자의 상태입니다. */
    enum class PalletState : uint8
    {
        Upright = 0,
        Dropped,
        Broken
    };

    /** @brief 살인마 공격 결과입니다. */
    enum class KillerAttackResult : uint8
    {
        Hit = 0, ///< 건강 → 부상
        Downed,  ///< 부상 → 빈사
        Missed,  ///< 헛방(헛방 쿨다운)
        NotReady ///< 쿨다운 · 기절 · 다른 일 중 · 들고 있다
    };

    /** @brief 생존자 하나입니다. */
    struct HorrorSurvivor
    {
        Vitality            _vitality{};
        InteractionProgress _healing{}; ///< 이 생존자를 치료하는 진행(치료하는 쪽들이 붙는다)
        StatBlock           _score{};   ///< 블러드포인트 범주 → 점수
        float3              _position{};
        float32             _lastMoveSpeed{ 0.0f }; ///< 이번 프레임에 움직인 속도(빠른 넘기 판정)
        Countdown           _hasteRemaining{};
        float32             _hookTimer{ 0.0f };
        float32             _struggleIdle{ 0.0f }; ///< 2 단계에서 몸부림을 멈춘 시간
        float32             _wiggleProgress{ 0.0f };
        Countdown           _vaultRemaining{};
        float32             _noiseRadius{ 0.0f }; ///< 지금 내고 있는 소음(살인마 자극)
        Countdown           _noiseRemaining{};
        float3              _vaultExit{};
        int32               _hookStage{ 0 }; ///< 걸린 횟수(이번 걸림의 단계)
        int32               _activityTarget{ -1 };
        int32               _participant{ -1 };
        SurvivorState       _state{ SurvivorState::Healthy };
        SurvivorActivity    _activity{ SurvivorActivity::None };
        uint8               _bStruggling{ SW_FALSE };
        uint8               _bWiggling{ SW_FALSE };
        uint8               _bBledOut{ SW_FALSE }; ///< 희생이 아니라 출혈로 죽었다

        bool isStanding() const { return _state != SurvivorState::Sacrificed && _state != SurvivorState::Escaped; }
        bool canAct() const { return ( _state == SurvivorState::Healthy || _state == SurvivorState::Injured ) && _activity != SurvivorActivity::Vaulting; }
    };
} // namespace sw

namespace sw
{
    /** @brief 살인마입니다. */
    struct HorrorKiller
    {
        StatBlock              _score{};
        float3                 _position{};
        float3                 _forward{ 0.0f, 0.0f, 1.0f };
        float3                 _busyExit{}; ///< 넘기가 끝나면 설 자리
        const HorrorKillerDef* _pDef{ nullptr };
        Countdown              _attackCooldown{};
        Countdown              _stunRemaining{};
        Countdown              _busyRemaining{}; ///< 판자 부수기 · 넘기 · 사물함 뒤지기
        Countdown              _abilityCooldown{};
        int32                  _carrying{ -1 };
        int32                  _breakingPallet{ -1 };
        int32                  _participant{ -1 };
        uint8                  _bVaulting{ SW_FALSE };

        bool canAct() const { return _stunRemaining.isActive() == false && _busyRemaining.isActive() == false; }
    };
} // namespace sw

namespace sw
{
    /** @brief 발전기 하나입니다. */
    struct HorrorGenerator
    {
        InteractionProgress _progress{};
        float3              _position{};
        uint8               _bKicked{ SW_FALSE };  ///< 걷어차여 아무도 없으면 줄고 있다(누가 다시 붙으면 멈춘다)
        uint8               _bBlocked{ SW_FALSE }; ///< 필요한 수가 다 고쳐져 더는 못 고친다
    };
} // namespace sw

namespace sw
{
    /** @brief 탈출구 하나입니다. */
    struct HorrorGate
    {
        InteractionProgress _progress{};
        float3              _position{};
    };
} // namespace sw

namespace sw
{
    /** @brief 창틀 하나입니다. */
    struct HorrorWindow
    {
        float3  _position{};
        float32 _blockedRemaining{ 0.0f };
        int32   _chaseVaultCount{ 0 }; ///< 추격 중 넘은 횟수(막히면 0)
    };
} // namespace sw

namespace sw
{
    /** @brief 판에서 생긴 일입니다. 생존자 번호는 0 부터, 살인마는 −1 로 적습니다. */
    struct AsymmetricHorrorEvent
    {
        enum class Kind : uint8
        {
            Hit = 0, ///< _actor = 맞은 생존자
            Missed,
            Downed,
            PickedUp,
            Hooked, ///< _value = 단계
            HookStageAdvanced,
            Sacrificed, ///< _value = 1 이면 출혈사
            Rescued,    ///< _actor = 구한 쪽, _target = 구해진 쪽
            WiggledFree,
            Healed,             ///< _target = 나은 쪽, _value = 새 상태(SurvivorState)
            SkillCheckStarted,  ///< _actor, _value = 목표 시각(그 일의 시계 — `findActivityProgress`)
            SkillCheckResult,   ///< _actor, _value = 1 성공 · 0 실패
            Noise,              ///< _position, _value = 반경 — 살인마에게 알린다
            GeneratorCompleted, ///< _target = 발전기
            GeneratorKicked,
            GatesPowered,
            GateOpened,
            HatchOpened,
            HatchClosed,
            CollapseStarted,
            Escaped, ///< _actor, _value = 1 이면 해치
            PalletDropped,
            KillerStunned, ///< _value = 시간
            PalletBroken,
            Vaulted, ///< _actor(−1 = 살인마), _target = 창틀, _value = 1 이면 빠른 넘기
            WindowBlocked,
            LockerGrab,
            MatchEnded ///< _value = 희생 수, _target = 탈출 수
        };
        float3  _position{};
        float32 _value{ 0.0f };
        int32   _actor{ -1 };
        int32   _target{ -1 };
        Kind    _kind{ Kind::Hit };
    };
} // namespace sw

namespace sw
{
    /** @brief 판 하나의 설정입니다. */
    struct HorrorMatchSettings
    {
        hashed_string _killerId{};
        float32       _fixedStep{ 1.0f / 30.0f };
        uint32        _seed{ 0xD8D0u };
    };
} // namespace sw

namespace sw
{
    /**
     * @class HorrorMatch
     * @brief 서버가 돌리는 한 판입니다. 규칙 카탈로그는 빌려 씁니다(판보다 오래 산다).
     * @details 무대(발전기 · 갈고리 · 판자 · 창틀 · 사물함 · 탈출구 · 해치 자리)는 `start` 전에 더합니다.
     *          모든 행동 함수는 거리 · 상태를 보고 못 하면 false 이고, 시간이 드는 일은 `update` 가 진행합니다.
     */
    class SW_GF_API HorrorMatch
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "HMAT" );
        static constexpr uint32 kStateVersion = 1;

        HorrorMatch();

        /** @brief 규칙과 살인마를 정합니다. 살인마 id 를 모르면 false 입니다. */
        [[nodiscard]] bool initialize( const HorrorMatchSettings& settings, const AsymmetricHorrorRulesCatalog* pCatalog );
        int32              addSurvivor( const float3& position );
        void               setKillerPosition( const float3& position );
        int32              addGenerator( const float3& position );
        int32              addHook( const float3& position );
        int32              addPallet( const float3& position );
        int32              addWindow( const float3& position );
        int32              addLocker( const float3& position );
        int32              addGate( const float3& position );
        void               setHatchPosition( const float3& position );
        void               start();
        void               update( float32 deltaTime );

        // --- 생존자 ---
        /** @brief @p direction(XZ, 길이 1 이하) 쪽으로 이번 프레임만큼 움직입니다. 하던 일은 그만둡니다. */
        void               moveSurvivor( int32 survivor, const float3& direction, float32 deltaTime );
        [[nodiscard]] bool startRepair( int32 survivor, int32 generator );
        [[nodiscard]] bool startHeal( int32 healer, int32 patient );
        [[nodiscard]] bool startOpenGate( int32 survivor, int32 gate );
        void               stopActivity( int32 survivor );
        /** @brief 스킬 체크에 응답합니다. @p pressTime 은 그 일의 시계(`findActivityProgress( survivor )->getTime()`)입니다. */
        bool               respondSkillCheck( int32 survivor, float32 pressTime );
        [[nodiscard]] bool rescue( int32 rescuer, int32 hooked );
        [[nodiscard]] bool dropPallet( int32 survivor, int32 pallet );
        /** @brief 창틀을 넘습니다. 달려 들어오면(이번 프레임 이동 속도) 빠른 넘기 — 소음이 난다. */
        [[nodiscard]] bool vault( int32 survivor, int32 window );
        [[nodiscard]] bool enterLocker( int32 survivor, int32 locker );
        void               exitLocker( int32 survivor );
        /** @brief 열린 탈출구 · 열린 해치 옆이면 탈출합니다. */
        [[nodiscard]] bool escape( int32 survivor );
        void               setStruggling( int32 survivor, bool bStruggling );
        void               setWiggling( int32 survivor, bool bWiggling );

        // --- 살인마 ---
        void               moveKiller( const float3& direction, float32 deltaTime );
        KillerAttackResult killerAttack();
        [[nodiscard]] bool pickUp( int32 survivor );
        [[nodiscard]] bool hookCarried( int32 hook );
        [[nodiscard]] bool kickGenerator( int32 generator );
        [[nodiscard]] bool breakPallet( int32 pallet );
        [[nodiscard]] bool killerVault( int32 window );
        /** @brief 사물함을 엽니다. 안에 생존자가 있으면 바로 듭니다(true). 비었으면 뒤지는 시간만 듭니다. */
        [[nodiscard]] bool searchLocker( int32 locker );
        [[nodiscard]] bool closeHatch();
        /** @brief 능력을 씁니다(쿨다운이 남았으면 false — 효과는 게임이). */
        [[nodiscard]] bool useKillerAbility();

        // --- 읽기 ---
        /** @brief 심장 소리 세기 0..1(위협 반경 밖 0, 살인마 자리 1)입니다. */
        float32 computeHeartbeat( int32 survivor ) const;
        float32 computeSurvivorSpeed( int32 survivor ) const;
        float32 computeKillerSpeed() const;
        /** @brief 살인마 봇의 `AIPerception::sense` 에 넣을 자극 — 숨지 않은 생존자, 이번 걸음 소음 반경을 실은 위치입니다. */
        void                       collectKillerStimuli( vector<AIStimulus>& outListStimulusEntry ) const;
        const InteractionProgress* findActivityProgress( int32 survivor ) const;
        void                       makeSnapshot( HorrorSnapshot& outSnapshot ) const;
        /** @brief 서버 권위 상태를 바이트로 씁니다(`HorrorSnapshotCodec::write`). */
        void writeState( BitWriter& outWriter ) const;
        /**
         * @brief 세이브 · 핫 리로드용 전체 상태를 씁니다 — 살인마 id · 판(`MatchState`) · 걸음 · 무대(자리 · 판자 · 사물함 · 창틀 · 발전기 · 탈출구 · 해치) ·
         *        생존자(체력 · 치료 진행 · 점수 · 타이머 · 갈고리 · 일) · 살인마(점수 · 자리 · 쿨다운 · 들고 있는 생존자) · 엔드게임. 넷 스냅숏(`writeState( BitWriter& )`)은
         *        화면용으로 양자화해 되살릴 수 없어 따로 둡니다. 설정 · 카탈로그는 `initialize` 의 것이라 싣지 않고, 알림은 읽을 때 비웁니다.
         */
        void writeState( Archive& outArchive ) const;
        /**
         * @brief `writeState( Archive& )` 의 바이트로 바꿉니다. 무대 · 생존자는 바이트가 정하고 규칙은 `initialize` 의 카탈로그를 씁니다.
         *        `initialize` 하지 않았거나, 살인마 id 가 지금 설정과 다르거나, 깨진 바이트면 false 이고 그대로입니다.
         */
        [[nodiscard]] bool readState( Archive& archive );
        void               drainEvents( vector<AsymmetricHorrorEvent>& outListEvent );

        const HorrorSurvivor*  findSurvivor( int32 survivor ) const { return isValidSurvivor( survivor ) ? &_listSurvivor[static_cast<size_t>( survivor )] : nullptr; }
        int32                  getSurvivorCount() const { return static_cast<int32>( _listSurvivor.size() ); }
        const HorrorKiller&    getKiller() const { return _killer; }
        const HorrorGenerator* findGenerator( int32 generator ) const;
        const HorrorGate*      findGate( int32 gate ) const;
        const HorrorWindow*    findWindow( int32 window ) const;
        PalletState            getPalletState( int32 pallet ) const;
        int32                  getCompletedGeneratorCount() const { return _completedGeneratorCount; }
        bool                   areGatesPowered() const { return _bGatesPowered == SW_TRUE; }
        bool                   isHatchOpen() const { return _bHatchOpen == SW_TRUE; }
        bool                   isCollapseStarted() const { return _bCollapseStarted == SW_TRUE; }
        float32                getCollapseRemaining() const { return _collapseRemaining.getRemaining(); }
        const MatchState&      getMatch() const { return _match; }
        bool                   isEnded() const { return _match.getPhase() == MatchPhase::Ended; }
        int32                  countStandingSurvivors() const;
        uint32                 getTick() const { return _tick; }

    private:
        bool isValidSurvivor( int32 survivor ) const { return survivor >= 0 && survivor < static_cast<int32>( _listSurvivor.size() ); }
        bool isNear( const float3& from, const float3& to, float32 range ) const;
        void step( float32 deltaTime );
        void stepKiller( float32 deltaTime );
        void stepSurvivor( int32 survivor, float32 deltaTime );
        void stepGenerators( float32 deltaTime );
        void stepGates( float32 deltaTime );
        void stepEndgame( float32 deltaTime );
        /** @brief 진행의 알림을 꺼내 스킬 체크 · 소음 · 점수로 옮깁니다. 이번에 끝났으면 true 입니다. */
        bool                 drainProgress( InteractionProgress& progress, const float3& position );
        InteractionProgress* findActivityProgressMutable( int32 survivor );
        void                 releaseHealers( int32 patient, bool bResetProgress );
        void                 finishHeal( int32 patient );
        /** @brief 빈사 · 들림에서 부상으로 돌립니다(치료 완료 · 구출 · 몸부림 탈출). */
        void restoreInjured( HorrorSurvivor& survivor );
        void sacrifice( int32 survivor, bool bBledOut );
        void leaveActivity( int32 survivor );
        void dropCarried( bool bStunKiller, float32 stunTime );
        void stunKiller( float32 seconds );
        void startCollapse();
        void resolveEnd();
        void syncHealthState( HorrorSurvivor& survivor );
        void awardScore( StatBlock& outScore, const utf8* pAction, float32 amount );
        void configureGenerator( int32 generator, bool bRegressing );
        /** @brief `start` 가 판에 주는 설정입니다(되살릴 때 같은 설정으로 판을 다시 연다). */
        MatchSettings makeMatchSettings() const;
        /** @brief 생존자 하나의 상태를 읽어 덮습니다(규칙 · 치료 설정은 `addSurvivor` 가 세운 것). 깨졌으면 false 입니다. */
        [[nodiscard]] bool readSurvivor( Archive& archive, HorrorSurvivor& outSurvivor ) const;
        void               makeNoise( int32 survivor, float32 radius, const float3& position );
        void               pushEvent( AsymmetricHorrorEvent::Kind kind, int32 actor, int32 target, float32 value, const float3& position );

        HorrorMatchSettings                 _settings;
        HorrorKiller                        _killer;
        vector<HorrorSurvivor>              _listSurvivor;
        vector<HorrorGenerator>             _listGenerator;
        vector<HorrorGate>                  _listGate;
        vector<HorrorWindow>                _listWindow;
        vector<float3>                      _listHook;
        vector<float3>                      _listPallet;
        vector<PalletState>                 _listPalletState;
        vector<float3>                      _listLocker;
        vector<int32>                       _listLockerOccupant;
        EventBuffer<AsymmetricHorrorEvent>  _eventBuffer;
        vector<InteractionEvent>            _listInteractionScratch;
        vector<VitalityEvent>               _listVitalityScratch;
        MatchState                          _match;
        FixedStepTimer                      _timer;
        float3                              _hatchPosition;
        const AsymmetricHorrorRulesCatalog* _pCatalog;
        Countdown                           _collapseRemaining;
        int32                               _completedGeneratorCount;
        int32                               _survivorTeam;
        int32                               _killerTeam;
        uint32                              _tick;
        uint8                               _bStarted;
        uint8                               _bGatesPowered;
        uint8                               _bHatchPlaced;
        uint8                               _bHatchOpen;
        uint8                               _bHatchClosed;
        uint8                               _bCollapseStarted;
        uint8                               _bEndReported;
    };
} // namespace sw
