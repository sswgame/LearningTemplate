/**
 * @file MechArenaWorld.h
 * @brief 기체 대전 한 판(서버 권위) — 조종사 · 기체 덱 · 이동(대시 · 점프 · 부유) · 부스트 오버히트 · 록온 · 사격 · 유도탄 · 근접 콤보 · 특수기 ·
 *        다운치 · 다운 · 기상 무적 · 경직 · 넉백 · 변형 · 스킬 · 팀 전력 게이지 · 리스폰 · 기체 교체 · 승패입니다.
 * @details 고정 걸음(`FixedStepTimer`, 기본 60 Hz — 근접 기술의 프레임과 같다)으로만 흘러 같은 입력이면 같은 결과입니다. 기반을 이렇게 씁니다.
 *          - 체력 · 다운치 · 기상 무적: `Vitality`(poise = 다운치 — 0 까지 깎이면 다운, 붕괴 시간 = 누운 시간)
 *          - 부스트: `ResourceGauge` 과열형(쓰면 열이 오르고 최대에 닿으면 오버히트 — 식을 때까지 대시 · 점프 · 부유 잠금)
 *          - 록온: `LockOnSelector`, 주무기: `WeaponState`, 근접: `MoveTimeline`(캔슬 창이 다음 콤보 단을 연다)
 *          - 판 · 전력 게이지: `MatchState` — 조종사의 **덱 칸마다** 참가자 하나를 두고 칸 기체의 코스트를 부활 비용으로 준다.
 *            그래서 기체를 바꾸면 다음 격추에 바뀐 기체의 코스트가 게이지에서 빠진다(통계는 조종사 단위로 모아 읽는다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Combat/FrameData.h"
#include "GameFramework/Combat/LockOnSelector.h"
#include "GameFramework/Combat/ResourceGauge.h"
#include "GameFramework/Combat/Vitality.h"
#include "GameFramework/Combat/Weapon.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Action/MechArena/MechCatalog.h"
#include "GameFramework/Match/MatchState.h"
#include "GameFramework/Utility/Countdown.h"
#include "GameFramework/Utility/EventBuffer.h"
#include "GameFramework/Utility/FixedStepTimer.h"

namespace sw
{
    struct MechArenaSnapshot;

    class BitWriter;

    /** @brief 판 규칙의 수치입니다. 시간은 초, 거리는 m 입니다. */
    struct MechArenaSettings
    {
        float32 _fixedStep{ 1.0f / 60.0f }; ///< 근접 기술 프레임 하나 = 한 걸음
        float32 _gravity{ 25.0f };
        float32 _arenaHalfSize{ 200.0f }; ///< 위치는 이 안으로 자른다(직렬화 양자화 범위)
        float32 _ceiling{ 60.0f };
        float32 _respawnDelay{ 5.0f };
        float32 _respawnInvulnerable{ 2.0f };
        float32 _timeLimit{ 0.0f };  ///< 0 = 없음. 끝나면 남은 게이지가 많은 팀이 이긴다
        float32 _gaugeScale{ 2.0f }; ///< `_teamGauge` 가 0 이면 팀 게이지 = 팀원 첫 기체 코스트 합 × 이 값
        int32   _teamGauge{ 0 };     ///< 팀 게이지를 바로 정한다(0 = 위의 합)
        uint32  _seed{ 0x5D6C0F1u }; ///< 탄 퍼짐 씨앗(조종사 번호를 섞는다)
    };
} // namespace sw

namespace sw
{
    /** @brief 조종사 한 명의 한 걸음 입력입니다. 버튼은 "누르고 있다" 이고, 눌린 순간은 월드가 앞 걸음과 비교해 압니다. */
    struct MechInput
    {
        float32 _moveX{ 0.0f }; ///< 이동 방향(월드 XZ, 길이 1 이하)
        float32 _moveZ{ 0.0f };
        int32   _skillSlot{ -1 }; ///< 수동 스킬 칸(−1 = 없음)
        uint8   _bDash{ SW_FALSE };
        uint8   _bJump{ SW_FALSE };
        uint8   _bHover{ SW_FALSE }; ///< 공중에서 누르고 있으면 부유(부스트 소비)
        uint8   _bFire{ SW_FALSE };
        uint8   _bMelee{ SW_FALSE };
        uint8   _bSpecial{ SW_FALSE };
        uint8   _bTransform{ SW_FALSE };
        uint8   _bLockOn{ SW_FALSE }; ///< 대상이 없으면 잡고, 있으면 다음 대상으로
    };
} // namespace sw

namespace sw
{
    /** @brief 조종사가 판에 들고 나오는 것입니다. */
    struct MechPilotConfig
    {
        vector<hashed_string> _listMechId{};  ///< 덱(첫 칸으로 출격)
        vector<hashed_string> _listSkillId{}; ///< 장착 스킬 — 기체의 `_skillSlots` 만큼 앞에서부터 쓴다
        float3                _spawnPosition{};
        int32                 _team{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 조종사의 상태입니다. */
    enum class MechPilotState : uint8
    {
        Waiting = 0, ///< `start` 전
        Active,
        Down,      ///< 다운 — 누워 있다(조작 불가, 다운치는 더 쌓이지 않는다)
        Destroyed, ///< 격추 — 리스폰 대기(이때만 기체를 바꿀 수 있다)
        Retired    ///< 판이 끝났거나 팀이 졌다
    };

    /** @brief 기체 교체 결과입니다. */
    enum class MechSwapResult : uint8
    {
        Ok = 0,
        NotDestroyed, ///< 격추되어 리스폰을 기다릴 때만 바꾼다
        InvalidSlot,
        OverCost ///< 그 기체의 코스트가 팀 남은 게이지보다 크다
    };

    /** @brief 판에서 생긴 일입니다. */
    struct MechArenaEvent
    {
        enum class Kind : uint8
        {
            Fired = 0,     ///< _pilot, _id = 무기 칸
            Hit,           ///< _pilot = 맞은 쪽, _other = 때린 쪽, _value = 체력 피해
            Downed,        ///< _pilot, _other = 다운시킨 쪽
            WokeUp,        ///< _pilot — 기상 무적이 붙었다
            Destroyed,     ///< _pilot, _other = 격추한 쪽
            Respawned,     ///< _pilot, _value = 덱 칸
            Overheated,    ///< _pilot — 부스트를 다 썼다
            Transformed,   ///< _pilot, _id = 새 형태
            SkillStarted,  ///< _pilot, _id = 스킬
            SkillEnded,    ///< _pilot, _id = 스킬
            ComboAdvanced, ///< _pilot, _value = 새 단(0 부터)
            MechSwapped,   ///< _pilot, _value = 덱 칸
            MatchEnded     ///< _other = 이긴 팀(−1 = 무승부)
        };
        hashed_string _id{};
        float32       _value{ 0.0f };
        int32         _pilot{ -1 };
        int32         _other{ -1 };
        Kind          _kind{ Kind::Fired };
    };
} // namespace sw

namespace sw
{
    /** @brief 조종사 하나의 상태입니다(월드가 쥔다 — 읽기 전용으로 내준다). */
    struct SW_GF_API MechPilot
    {
        Vitality                    _vitality{};
        ResourceGauge               _boost{};
        LockOnSelector              _lockOn{};
        MoveTimeline                _melee{};
        vector<const MechDef*>      _listDeckMech{};
        vector<int32>               _listParticipant{}; ///< 덱 칸마다 `MatchState` 참가자
        vector<const MechSkillDef*> _listSkill{};
        vector<Countdown>           _listSkillRemaining{}; ///< 켜져 있는 남은 시간(0 = 꺼짐)
        vector<Countdown>           _listSkillCooldown{};
        vector<uint8>               _listSkillSpent{};  ///< 이번 목숨에 이미 켰다(HealthBelow)
        vector<WeaponState>         _listWeaponState{}; ///< 기체의 모든 형태 · 칸(형태마다 탄창이 따로)
        vector<Countdown>           _listSpecialCooldown{};
        MechInput                   _input{};
        MechInput                   _previousInput{};
        float3                      _position{};
        float3                      _velocity{};
        float3                      _forward{ 0.0f, 0.0f, 1.0f };
        float3                      _dashDirection{};
        float3                      _spawnPosition{};
        Countdown                   _dashRemaining{};
        Countdown                   _staggerRemaining{};
        Countdown                   _transformCooldown{};
        int32                       _team{ 0 };
        int32                       _deckIndex{ 0 };  ///< 지금 타는 덱 칸
        int32                       _deathSlot{ -1 }; ///< 격추된 덱 칸(그 참가자의 부활을 기다린다)
        int32                       _mode{ 0 };
        int32                       _comboStage{ -1 }; ///< 근접 콤보의 지금 단(−1 = 근접 아님)
        int32                       _meleeSlot{ -1 };
        int32                       _kills{ 0 };
        int32                       _deaths{ 0 };
        MechPilotState              _state{ MechPilotState::Waiting };
        uint8                       _bGrounded{ SW_TRUE };
        uint8                       _bMeleeHit{ SW_FALSE };    ///< 이 단에서 이미 맞혔다
        uint8                       _bMeleeQueued{ SW_FALSE }; ///< 다음 단을 눌러 두었다(캔슬 창이 열리면 나간다)
        uint8                       _bWasOverheated{ SW_FALSE };

        const MechDef*     getMech() const { return _listDeckMech.empty() ? nullptr : _listDeckMech[static_cast<size_t>( _deckIndex )]; }
        const MechModeDef* getMode() const;
        bool               canAct() const { return _state == MechPilotState::Active && _staggerRemaining.isActive() == false; }
    };
} // namespace sw

namespace sw
{
    /** @brief 날아가는 탄 하나입니다. */
    struct MechProjectile
    {
        float3         _position{};
        float3         _direction{ 0.0f, 0.0f, 1.0f };
        float32        _speed{ 0.0f };
        float32        _homing{ 0.0f }; ///< 도/초
        float32        _rangeLeft{ 0.0f };
        float32        _damage{ 0.0f };
        float32        _downValue{ 0.0f };
        float32        _knockback{ 0.0f };
        float32        _staggerTime{ 0.0f };
        int32          _owner{ -1 };
        int32          _target{ -1 }; ///< 쏠 때 록온한 조종사(유도 대상)
        MechWeaponKind _kind{ MechWeaponKind::Shot };
    };
} // namespace sw

namespace sw
{
    /**
     * @class MechArenaWorld
     * @brief 서버가 돌리는 한 판입니다. 카탈로그는 빌려 씁니다(판보다 오래 산다).
     * @code
     *     MechArenaWorld world;
     *     world.initialize( settings, &mechCatalog, &weaponCatalog, &moveCatalog );
     *     const int32 amuro = world.addPilot( config );
     *     world.start();
     *     world.setInput( amuro, input );
     *     world.update( deltaTime );
     *     world.writeState( writer ); // 넷 키트에 싣는다
     * @endcode
     */
    class SW_GF_API MechArenaWorld
    {
    public:
        MechArenaWorld();

        void initialize( const MechArenaSettings& settings, const MechCatalog* pMechCatalog, const WeaponCatalog* pWeaponCatalog, const MoveCatalog* pMoveCatalog );
        /** @brief 팀을 더합니다(번호를 돌려준다). */
        int32 addTeam( const hashed_string& name );
        /** @brief 조종사를 더합니다. 덱이 비었거나 모르는 기체 · 팀이거나 덱 코스트가 상한을 넘으면 −1 입니다. */
        int32 addPilot( const MechPilotConfig& config );
        /** @brief 팀 게이지를 정하고(코스트 합) 출격시킵니다. */
        void start();
        void update( float32 deltaTime );

        void setInput( int32 pilot, const MechInput& input );
        /** @brief 격추된 동안 다음에 탈 덱 칸을 고릅니다(코스트가 팀 남은 게이지 안이어야 한다). */
        MechSwapResult requestMechSwap( int32 pilot, int32 deckIndex );
        /** @brief 게임 쪽 판정(지형 낙하 · 함정)의 피해를 줍니다. 무기와 같은 길(다운치 · 경직 · 격추 · 게이지)을 탑니다. */
        void applyDamage( int32 attacker, int32 victim, float32 damage, float32 downValue, float32 staggerTime );
        /** @brief 조종사를 그 자리로 옮깁니다(스폰 지점 · 시험). */
        void teleport( int32 pilot, const float3& position );

        /** @brief 지금 상태를 스냅샷으로 만듭니다. */
        void makeSnapshot( MechArenaSnapshot& outSnapshot ) const;
        /** @brief 서버 권위 상태를 바이트로 씁니다(`MechArenaSnapshotCodec::write`). */
        void writeState( BitWriter& outWriter ) const;
        void drainEvents( vector<MechArenaEvent>& outListEvent );

        const MechPilot*              findPilot( int32 pilot ) const { return isValidPilot( pilot ) ? &_listPilot[static_cast<size_t>( pilot )] : nullptr; }
        int32                         getPilotCount() const { return static_cast<int32>( _listPilot.size() ); }
        const vector<MechProjectile>& getProjectiles() const { return _listProjectile; }
        const MatchState&             getMatch() const { return _match; }
        /** @brief 팀의 남은 전력 게이지입니다. */
        int32 getTeamGauge( int32 team ) const;
        /** @brief 켜진 스킬들의 배율 곱입니다("attack" · "defense" · "speed" · "boostRegen" · "downResist"). */
        float32 computeModifier( int32 pilot, const hashed_string& name ) const;
        uint32  getTick() const { return _tick; }
        bool    isEnded() const { return _match.getPhase() == MatchPhase::Ended; }

    private:
        bool   isValidPilot( int32 pilot ) const { return pilot >= 0 && pilot < static_cast<int32>( _listPilot.size() ); }
        void   step( float32 deltaTime );
        void   spawnPilot( int32 pilot, bool bRespawn );
        void   stepPilot( int32 pilot, float32 deltaTime );
        void   stepMovement( int32 pilot, bool bControl, bool bPressedDash, bool bPressedJump, float32 deltaTime );
        void   stepLockOn( int32 pilot, bool bPressedLockOn, float32 deltaTime );
        void   stepWeapons( int32 pilot, float32 deltaTime );
        void   stepMelee( int32 pilot, bool bPressedMelee );
        void   stepSkills( int32 pilot, float32 deltaTime );
        void   stepProjectiles( float32 deltaTime );
        void   stepMatch( float32 deltaTime );
        void   fireShot( int32 pilot, int32 slotIndex, const MechWeaponSlotDef& slot, bool bPressed );
        void   fireSpecial( int32 pilot, int32 slotIndex, const MechWeaponSlotDef& slot );
        void   launch( int32 pilot, int32 target, const float3& direction, float32 speed, float32 range, float32 damage, const MechWeaponSlotDef& slot );
        void   resolveHit( int32 attacker, int32 victim, float32 damage, float32 downValue, float32 knockback, float32 staggerTime, bool bKnockdown,
                           MechWeaponKind kind );
        void   activateSkill( int32 pilot, int32 skillIndex );
        void   triggerSkills( int32 pilot, MechSkillTrigger trigger );
        void   equipMech( MechPilot& pilot, int32 pilotIndex );
        void   collectCandidates( int32 pilot, vector<LockOnCandidate>& outListCandidate ) const;
        float3 computeCenter( const MechPilot& pilot ) const;
        int32  findLockTarget( int32 pilot );
        int32  countActiveSkills( const MechPilot& pilot ) const;
        int32  findSlot( const MechPilot& pilot, MechWeaponKind kind ) const;
        bool   isTargetable( int32 pilot ) const;
        void   pushEvent( MechArenaEvent::Kind kind, int32 pilot, int32 other, float32 value, const hashed_string& id );

        MechArenaSettings           _settings;
        vector<MechPilot>           _listPilot;
        vector<MechProjectile>      _listProjectile;
        vector<hashed_string>       _listTeamName;
        EventBuffer<MechArenaEvent> _eventBuffer;
        vector<VitalityEvent>       _listVitalityScratch;
        vector<MatchEvent>          _listMatchScratch;
        vector<LockOnCandidate>     _listCandidateScratch;
        MatchState                  _match;
        FixedStepTimer              _timer;
        const MechCatalog*          _pMechCatalog;
        const WeaponCatalog*        _pWeaponCatalog;
        const MoveCatalog*          _pMoveCatalog;
        uint32                      _tick;
        uint8                       _bStarted;
        uint8                       _bEndReported;
    };
} // namespace sw
