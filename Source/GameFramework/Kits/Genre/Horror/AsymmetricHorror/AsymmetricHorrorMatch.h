/**
 * @file AsymmetricHorrorMatch.h
 * @brief 1 대 4 비대칭 공포 한 판(서버 권위)을 진행합니다. 생존자(건강, 부상, 빈사, 들림, 갈고리 1, 2, 3단계, 구출, 치료, 몸부림), 발전기(다인 수리, 스킬 체크,
 *        폭발 소음, 걷어차기 퇴행), 탈출구(전원, 열기), 해치, 엔드게임 붕괴, 판자, 창틀, 사물함, 살인마(속도, 공격 쿨다운, 헛방, 위협 반경, 능력),
 *        블러드포인트, 결과(탈출과 희생)를 다룹니다.
 * @details 고정 걸음(`FixedStepTimer`)으로만 흐르고 스킬 체크는 시드로 정해지므로, 같은 호출이면 같은 결과가 나온다. 기반은 이렇게 쓴다.
 *          - 체력 상태: `HealthStateMachine`(체력 2 는 건강, 1 은 부상, 기절은 빈사이며, 출혈 체력은 남은 출혈 초이고 기절 중 부활은 빈사 회복이다)
 *          - 수리, 치료, 탈출구: `InteractionProgress`(인원 배율, 스킬 체크 `TimingJudge`, 실패 소음, 퇴행)
 *          - 판과 결과: `MatchState`(생존자 팀과 살인마 팀이며, 탈출과 희생은 탈락이고, 점수를 쓴다)
 *          - 살인마 봇의 감각: `collectKillerStimuli` 가 `AIPerception::sense` 에 넣을 자극(위치와 소음 반경)을 채운다.
 *          위치는 XZ 평면(y = 0)이고 이동은 `moveSurvivor` 와 `moveKiller` 로 한다. 키트가 상태에 맞는 속도를 곱한다(벽 충돌은 게임이 처리한다).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Encoding/FourCCUtil.h"
#include "Core/Event/EventBuffer.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"
#include "Core/Time/Countdown.h"
#include "Core/Time/FixedStepTimer.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Character/Combat/Health/HealthStateMachine.h"
#include "GameFramework/Runtime/Foundation/GameData/StatBlock.h"
#include "GameFramework/Runtime/Gameplay/Interaction/InteractionProgress.h"
#include "GameFramework/Runtime/Gameplay/Match/MatchState.h"

namespace sw
{
    struct AIStimulus;
    struct AsymmetricHorrorKillerDef;
    struct AsymmetricHorrorSnapshot;

    class Archive;
    class AsymmetricHorrorRulesCatalog;
    class BitWriter;
    class NetMessageChannel;
    class NetMessageWriter;

    /** @brief 생존자의 상태입니다. */
    enum class SurvivorState : uint8
    {
        Healthy = 0,
        Injured,
        Dying,   ///< 빈사 상태이다. 기어가며 출혈이 있고, 치료하면 부상까지 회복한다
        Carried, ///< 살인마가 들고 있다(몸부림)
        Hooked,  ///< 갈고리에 걸렸다(`_hookStage` 1 은 버티기, 2 는 몸부림)
        Sacrificed,
        Escaped
    };

    /** @brief 생존자가 하고 있는 일입니다. */
    enum class SurvivorActivity : uint8
    {
        None = 0,
        Repairing,   ///< `_activityTarget` 은 발전기이다
        Healing,     ///< `_activityTarget` 은 치료받는 생존자이다
        OpeningGate, ///< `_activityTarget` 은 탈출구이다
        Vaulting,    ///< `_activityTarget` 은 창틀이다
        InLocker     ///< `_activityTarget` 은 사물함이다
    };

    /** @brief 판자의 상태입니다. */
    enum class PalletState : uint8
    {
        Upright = 0,
        Dropped,
        Broken
    };

    /** @brief 살인마 공격의 결과입니다. */
    enum class KillerAttackResult : uint8
    {
        Hit = 0, ///< 건강에서 부상이 되었다
        Downed,  ///< 부상에서 빈사가 되었다
        Missed,  ///< 헛방이다(헛방 쿨다운이 걸린다)
        NotReady ///< 쿨다운, 기절, 다른 일 중, 들고 있는 상태 중 하나라서 공격할 수 없다
    };

    /** @brief 생존자 하나입니다. */
    struct AsymmetricHorrorSurvivor
    {
        HealthStateMachine  _healthMachine{};
        InteractionProgress _healing{}; ///< 이 생존자를 치료하는 진행이다(치료하는 쪽들이 붙는다)
        StatBlock           _score{};   ///< 블러드포인트 범주별 점수이다
        float3              _position{};
        float32             _lastMoveSpeed{ 0.0f }; ///< 이번 프레임에 움직인 속도이다(빠른 넘기 판정에 쓴다)
        Countdown           _hasteRemaining{};
        float32             _hookTimer{ 0.0f };
        float32             _struggleIdle{ 0.0f }; ///< 2단계에서 몸부림을 멈춘 시간
        float32             _wiggleProgress{ 0.0f };
        Countdown           _vaultRemaining{};
        float32             _noiseRadius{ 0.0f }; ///< 지금 내고 있는 소음이다(살인마 자극)
        Countdown           _noiseRemaining{};
        float3              _vaultExit{};
        int32               _hookStage{ 0 }; ///< 걸린 횟수이다(이번 걸림의 단계)
        int32               _activityTarget{ constant::invalid_index::kInt32 };
        int32               _participant{ constant::invalid_index::kInt32 };
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
    struct AsymmetricHorrorKiller
    {
        StatBlock                        _score{};
        float3                           _position{};
        float3                           _forward{ 0.0f, 0.0f, 1.0f };
        float3                           _busyExit{}; ///< 넘기가 끝나면 설 위치
        const AsymmetricHorrorKillerDef* _pDef{ nullptr };
        Countdown                        _attackCooldown{};
        Countdown                        _stunRemaining{};
        Countdown                        _busyRemaining{}; ///< 판자 부수기, 넘기, 사물함 뒤지기 중이다
        Countdown                        _abilityCooldown{};
        int32                            _carrying{ constant::invalid_index::kInt32 };
        int32                            _breakingPallet{ constant::invalid_index::kInt32 };
        int32                            _participant{ constant::invalid_index::kInt32 };
        uint8                            _bVaulting{ SW_FALSE };

        bool canAct() const { return _stunRemaining.isActive() == false && _busyRemaining.isActive() == false; }
    };
} // namespace sw

namespace sw
{
    /** @brief 발전기 하나입니다. */
    struct AsymmetricHorrorGenerator
    {
        InteractionProgress _progress{};
        float3              _position{};
        uint8               _bKicked{ SW_FALSE };  ///< 걷어차여서 아무도 없으면 줄고 있다(누가 다시 붙으면 멈춘다)
        uint8               _bBlocked{ SW_FALSE }; ///< 필요한 수가 다 고쳐져서 더는 고칠 수 없다
    };
} // namespace sw

namespace sw
{
    /** @brief 탈출구 하나입니다. */
    struct AsymmetricHorrorGate
    {
        InteractionProgress _progress{};
        float3              _position{};
    };
} // namespace sw

namespace sw
{
    /** @brief 창틀 하나입니다. */
    struct AsymmetricHorrorWindow
    {
        float3  _position{};
        float32 _blockedRemaining{ 0.0f };
        int32   _chaseVaultCount{ 0 }; ///< 추격 중 넘은 횟수이다(막히면 0)
    };
} // namespace sw

namespace sw
{
    /** @brief 판에서 일어난 일을 알리는 이벤트입니다. 생존자 번호는 0부터이고 살인마는 -1 로 적습니다. */
    struct AsymmetricHorrorEvent
    {
        enum class Kind : uint8
        {
            Hit = 0, ///< `_actor` 는 맞은 생존자이다
            Missed,
            Downed,
            PickedUp,
            Hooked, ///< `_value` 는 단계이다
            HookStageAdvanced,
            Sacrificed, ///< `_value` 가 1 이면 출혈사이다
            Rescued,    ///< `_actor` 는 구한 쪽, `_target` 은 구해진 쪽이다
            WiggledFree,
            Healed,             ///< `_target` 은 나은 쪽, `_value` 는 새 상태(SurvivorState)이다
            SkillCheckStarted,  ///< `_value` 는 목표 시각이다(그 일의 시계이며 `findActivityProgress` 로 얻는다)
            SkillCheckResult,   ///< `_value` 가 1 이면 성공, 0 이면 실패이다
            Noise,              ///< `_position` 과 `_value`(반경)로 살인마에게 알린다
            GeneratorCompleted, ///< `_target` 은 발전기이다
            GeneratorKicked,
            GatesPowered,
            GateOpened,
            HatchOpened,
            HatchClosed,
            CollapseStarted,
            Escaped, ///< `_value` 가 1 이면 해치로 탈출한 것이다
            PalletDropped,
            KillerStunned, ///< `_value` 는 시간이다
            PalletBroken,
            Vaulted, ///< `_actor` 가 -1 이면 살인마이고, `_target` 은 창틀이며, `_value` 가 1 이면 빠른 넘기이다
            WindowBlocked,
            LockerGrab,
            MatchEnded ///< `_value` 는 희생 수, `_target` 은 탈출 수이다
        };
        float3          _position{};
        float32         _value{ 0.0f };
        optional<int32> _actor{};
        optional<int32> _target{};
        Kind            _kind{ Kind::Hit };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 판 하나의 설정입니다.
     * @details 범위는 필드의 `PROPERTY( Min · Max )` 이고, 벗어난 값은 `AsymmetricHorrorMatch::initialize` 가 오류로 거절한다(`ConfigRangeUtil`). 값을 조용히 맞추지 않는다(폴백 금지 원칙).
     */
    REFLECT()
    struct SW_KIT_API AsymmetricHorrorMatchSettings
    {
        REFLECT_BODY();
        PROPERTY()
        hashed_string _killerID{};
        PROPERTY( Min = 0.001, Max = 1.0 )
        float32 _fixedStep{ 1.0f / 30.0f };
        PROPERTY()
        uint32 _seed{ 0xD8D0u };
    };
} // namespace sw

namespace sw
{
    /**
     * @class AsymmetricHorrorMatch
     * @brief 서버가 돌리는 한 판입니다. 규칙 카탈로그는 빌려 쓰므로 판보다 오래 살아야 합니다.
     * @details 무대(발전기, 갈고리, 판자, 창틀, 사물함, 탈출구, 해치 위치)는 `start` 전에 추가해야 한다.
     *          모든 행동 함수는 거리와 상태를 확인해 할 수 없으면 false 를 반환하고, 시간이 드는 일은 `update` 가 진행한다.
     */
    class SW_KIT_API AsymmetricHorrorMatch
    {
    public:
        static constexpr uint32 kStateTag     = FourCCUtil::make( "HMAT" );
        static constexpr uint32 kStateVersion = 1;

        AsymmetricHorrorMatch();

        /** @brief 규칙과 살인마를 정합니다. 살인마 id 를 모르면 false 를 반환합니다. */
        [[nodiscard]] bool initialize( const AsymmetricHorrorMatchSettings& settings, const AsymmetricHorrorRulesCatalog* pCatalog );
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
        /** @brief @p direction(XZ 평면, 길이 1 이하) 쪽으로 이번 프레임만큼 움직입니다. 하던 일은 그만둡니다. */
        void               moveSurvivor( int32 survivor, const float3& direction, float32 deltaTime );
        [[nodiscard]] bool startRepair( int32 survivor, int32 generator );
        [[nodiscard]] bool startHeal( int32 healer, int32 patient );
        [[nodiscard]] bool startOpenGate( int32 survivor, int32 gate );
        void               stopActivity( int32 survivor );
        /** @brief 스킬 체크에 응답합니다. @p pressTime 은 그 일의 시계(`findActivityProgress( survivor )->getTime()`)입니다. */
        bool               respondSkillCheck( int32 survivor, float32 pressTime );
        [[nodiscard]] bool rescue( int32 rescuer, int32 hooked );
        [[nodiscard]] bool dropPallet( int32 survivor, int32 pallet );
        /** @brief 창틀을 넘습니다. 달려 들어오면(이번 프레임 이동 속도 기준) 빠른 넘기이며 소음이 납니다. */
        [[nodiscard]] bool vault( int32 survivor, int32 window );
        [[nodiscard]] bool enterLocker( int32 survivor, int32 locker );
        void               exitLocker( int32 survivor );
        /** @brief 열린 탈출구나 열린 해치 옆이면 탈출합니다. */
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
        /** @brief 사물함을 엽니다. 안에 생존자가 있으면 바로 들고(true), 비었으면 뒤지는 시간만 듭니다. */
        [[nodiscard]] bool searchLocker( int32 locker );
        [[nodiscard]] bool closeHatch();
        /** @brief 능력을 씁니다. 쿨다운이 남았으면 false 를 반환하며, 효과는 게임이 처리합니다. */
        [[nodiscard]] bool useKillerAbility();

        // --- 읽기 ---
        /** @brief 심장 소리 세기를 0..1 로 반환합니다(위협 반경 밖은 0, 살인마 위치는 1). */
        float32 computeHeartbeat( int32 survivor ) const;
        float32 computeSurvivorSpeed( int32 survivor ) const;
        float32 computeKillerSpeed() const;
        /** @brief 살인마 봇의 `AIPerception::sense` 에 넣을 자극을 모읍니다. 숨지 않은 생존자의 위치에 이번 걸음의 소음 반경을 실습니다. */
        void                       collectKillerStimuli( vector<AIStimulus>& outListStimulusEntry ) const;
        const InteractionProgress* findActivityProgress( int32 survivor ) const;
        void                       makeSnapshot( AsymmetricHorrorSnapshot& outSnapshot ) const;
        /** @brief 스냅샷(`AsymmetricHorrorSnapshot`)을 넷 채널의 종류 @p kind 로 등록합니다. 채널을 봉인하기 전에 부르고, 그 채널이 내준 호스트 설정으로 연결한다(핸드셰이크가 스키마를 확인한다). */
        [[nodiscard]] static bool registerSnapshotMessage( NetMessageChannel& inoutChannel, uint8 kind );
        /**
         * @brief 서버 권위 상태(`AsymmetricHorrorSnapshot`)를 봉인한 채널의 메시지(종류 바이트 + 스키마 해시 없는 본문)로 씁니다. 받는 쪽은 `NetMessageChannel::readMessage` 로 읽는다.
         * @return 범위 밖 값(255 개를 넘는 목록, 갈고리 단계 15 초과 등)이 있거나, 채널이 봉인 전이거나, 그 종류에 스냅샷을 등록하지 않았으면 false 입니다(잘라 쓰지 않는다).
         */
        [[nodiscard]] bool writeSnapshotMessage( const NetMessageChannel& channel, uint8 kind, NetMessageWriter& outWriter ) const;
        /**
         * @brief 세이브와 핫 리로드용 전체 상태를 씁니다. 살인마 id, 판(`MatchState`), 걸음, 무대(위치, 판자, 사물함, 창틀, 발전기, 탈출구, 해치),
         *        생존자(체력, 치료 진행, 점수, 타이머, 갈고리, 하던 일), 살인마(점수, 위치, 쿨다운, 들고 있는 생존자), 엔드게임을 씁니다. 네트워크 스냅샷(`writeSnapshotMessage`)은
         *        화면용으로 양자화한 값이라 복원할 수 없으므로 따로 둡니다. 설정과 카탈로그는 `initialize` 에서 받으므로 저장하지 않고, 이벤트는 읽을 때 비웁니다.
         *        `writeState( Archive& )` 가 쓴 바이트를 읽어 상태를 복원합니다. 무대와 생존자는 바이트가 정하고, 규칙은 `initialize` 의 카탈로그를 씁니다.
         *        `initialize` 하지 않았거나, 살인마 id 가 지금 설정과 다르거나, 바이트가 깨졌거나, 값이 범위 밖이거나 서로 맞지 않으면(`isStateConsistent`) false 를 반환하고 상태는 그대로입니다.
         */
        SW_DECLARE_STATE_IO();
        void pollEvents( vector<AsymmetricHorrorEvent>& outListEvent );

        const AsymmetricHorrorSurvivor*  findSurvivor( int32 survivor ) const { return isValidSurvivor( survivor ) ? &_listSurvivor[static_cast<size_t>( survivor )] : nullptr; }
        int32                            getSurvivorCount() const { return static_cast<int32>( _listSurvivor.size() ); }
        const AsymmetricHorrorKiller&    getKiller() const { return _killer; }
        const AsymmetricHorrorGenerator* findGenerator( int32 generator ) const;
        const AsymmetricHorrorGate*      findGate( int32 gate ) const;
        const AsymmetricHorrorWindow*    findWindow( int32 window ) const;
        PalletState                      getPalletState( int32 pallet ) const;
        int32                            getCompletedGeneratorCount() const { return _completedGeneratorCount; }
        bool                             areGatesPowered() const { return _bGatesPowered == SW_TRUE; }
        bool                             isHatchOpen() const { return _bHatchOpen == SW_TRUE; }
        bool                             isCollapseStarted() const { return _bCollapseStarted == SW_TRUE; }
        float32                          getCollapseRemaining() const { return _collapseRemaining.getRemaining(); }
        const MatchState&                getMatch() const { return _match; }
        bool                             isEnded() const { return _match.getPhase() == MatchPhase::Ended; }
        int32                            countStandingSurvivors() const;
        uint32                           getTick() const { return _tick; }

    private:
        bool isValidSurvivor( int32 survivor ) const { return survivor >= 0 && survivor < static_cast<int32>( _listSurvivor.size() ); }
        bool isNear( const float3& from, const float3& to, float32 range ) const;
        void step( float32 deltaTime );
        void stepKiller( float32 deltaTime );
        void stepSurvivor( int32 survivor, float32 deltaTime );
        void stepGenerators( float32 deltaTime );
        void stepGates( float32 deltaTime );
        void stepWindows( float32 deltaTime );
        void stepEndgame( float32 deltaTime );
        /** @brief 진행 도구의 이벤트를 꺼내 스킬 체크, 소음, 점수로 옮깁니다. 이번에 끝났으면 true 를 반환합니다. */
        bool drainProgress( InteractionProgress& progress, const float3& position );
        /** @brief 발전기 진행의 이벤트를 처리합니다. 끝났으면 고치던 사람을 떼고 완료 수와 전원을 처리합니다. 스킬 체크 응답은 걸음 사이에 오므로 응답에서도 부릅니다. */
        void settleGenerator( int32 generator );
        /** @brief 치료 진행의 이벤트를 처리합니다. 끝났으면 치료를 마칩니다. 스킬 체크 응답은 걸음 사이에 오므로 응답에서도 부릅니다. */
        void settleHealing( int32 patient );
        /** @brief @p patient 에게서 치료 거리를 벗어난 치료자를 뗍니다. 환자가 움직이면 부릅니다. */
        void                 releaseFarHealers( int32 patient );
        InteractionProgress* findActivityProgressMutable( int32 survivor );
        void                 releaseHealers( int32 patient, bool bResetProgress );
        void                 finishHeal( int32 patient );
        /** @brief 빈사나 들림 상태에서 부상으로 되돌립니다(치료 완료, 구출, 몸부림 탈출). */
        void restoreInjured( AsymmetricHorrorSurvivor& survivor );
        void sacrifice( int32 survivor, bool bBledOut );
        void leaveActivity( int32 survivor );
        void dropCarried( bool bStunKiller, float32 stunTime );
        void stunKiller( float32 seconds );
        void startCollapse();
        void resolveEnd();
        void syncHealthState( AsymmetricHorrorSurvivor& survivor );
        void awardScore( StatBlock& outScore, const utf8* pAction, float32 amount );
        /** @brief 발전기의 수리 규칙을 카탈로그에서 다시 잡습니다(진행량은 남긴다). 규칙이 범위 밖이라 거절되면 false 이고 발전기는 그대로입니다. */
        [[nodiscard]] bool configureGenerator( int32 generator, bool bRegressing );
        /** @brief `start` 가 판에 주는 설정입니다. 복원할 때 같은 설정으로 판을 다시 엽니다. */
        MatchSettings makeMatchSettings() const;
        /** @brief 생존자 하나의 상태를 놓습니다(규칙과 치료 설정은 `addSurvivor` 가 만든 것을 씁니다). */
        void serializeSurvivor( StateIO& io, AsymmetricHorrorSurvivor& survivor ) const;
        /** @brief 무대(갈고리, 판자, 사물함, 창문, 발전기, 탈출문)를 놓습니다. 읽을 때는 위치로 다시 만들고 그 위에 상태를 덮습니다. 판자 수를 @p inoutPalletCount 에 적습니다. */
        void serializeStage( StateIO& io, uint32& inoutPalletCount );
        /** @brief 살인마 상태를 놓습니다. 든 생존자 · 부수는 판자 번호가 @p survivorCount · @p palletCount 밖이면 거절합니다. */
        void serializeKiller( StateIO& io, uint32 survivorCount, uint32 palletCount );
        /**
         * @brief 읽은 상태가 스스로 맞는지 봅니다. 판과 팀 번호, 완료 수와 전원, 해치, 창틀, 생존자의 상태와 체력과 갈고리 단계, 하던 일과 그 진행의 참가자,
         *        사물함 짝, 들린 생존자 짝, 부수는 판자를 봅니다. 어긋난 짝은 범위 안 번호여도 판을 멈추거나(들린 채 내려오지 못함) 없는 사람이 일을 잇게 합니다.
         */
        [[nodiscard]] bool isStateConsistent() const;
        void               makeNoise( int32 survivor, float32 radius, const float3& position );
        void               pushEvent( AsymmetricHorrorEvent::Kind kind, optional<int32> actor, optional<int32> target, float32 value, const float3& position );

    private:
        AsymmetricHorrorMatchSettings       _settings;
        AsymmetricHorrorKiller              _killer;
        vector<AsymmetricHorrorSurvivor>    _listSurvivor;
        vector<AsymmetricHorrorGenerator>   _listGenerator;
        vector<AsymmetricHorrorGate>        _listGate;
        vector<AsymmetricHorrorWindow>      _listWindow;
        vector<float3>                      _listHook;
        vector<float3>                      _listPallet;
        vector<PalletState>                 _listPalletState;
        vector<float3>                      _listLocker;
        vector<int32>                       _listLockerOccupant;
        EventBuffer<AsymmetricHorrorEvent>  _eventBuffer;
        vector<InteractionEvent>            _listInteractionScratch;
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
