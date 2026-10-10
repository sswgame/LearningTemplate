/**
 * @file TrampolineArena.h
 * @brief 트램펄린 아레나(바이킹 온 트램펄린 류) — 둥근 트램펄린 위에서 튕기며 공중 공격 · 내려찍기로 상대를 밀어 떨어뜨리면 점수입니다.
 * @details 키트 자체의 2.5D 물리입니다 — 수평은 XZ, 높이는 Y(트램펄린 면이 0). 옆에서 보는 2D 게임은 Z 를 0 으로 두면 됩니다.
 *
 *          튕김: 면에 닿으면 잠깐(`_contactTime`) 눌려 있다가 튀어 오릅니다. 닿는 순간을 목표로 점프 입력을 기반 `TimingJudge` 로 판정해 창 안이면
 *          콤보가 하나 오르고, 튀는 높이는 `_baseBounceHeight + 콤보 × _comboHeightStep`(콤보는 `_maxCombo` 에서 멈춘다)입니다.
 *          미리 누른 입력(닿기 전 창 안)도, 눌려 있는 동안의 입력(닿은 뒤 창 안)도 듣습니다. 창을 놓치면 콤보가 0 으로 돌아갑니다.
 *          공중 공격은 그 방향으로 짧게 돌진하고 닿은 상대를 밀어냅니다. 내려찍기는 곧장 떨어져 닿는 자리 둘레를 밀어내고 콤보를 끊습니다.
 *          트램펄린 밖에 떨어지면 탈락(링 아웃)이고, 최근 `_creditWindow` 초 안에 민 사람이 점수를 받습니다(기반 `MatchState` 의 처치 · 도움 · 부활 대기).
 *          고정 걸음 + 씨앗 난수(아이템)라 같은 입력이면 같은 판입니다(락스텝 — 입력은 `writeInput` · `readInput`).
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Actor/Input/TimingJudge.h"
#include "GameFramework/Base/Foundation/Utility/EventBuffer.h"
#include "GameFramework/Base/Foundation/Utility/Time/Countdown.h"
#include "GameFramework/Base/Foundation/Utility/Time/FixedStepTimer.h"
#include "GameFramework/Base/Gameplay/Match/MatchState.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Casual/PartyArena/PartyItemSpawner.h"

namespace sw
{
    class Archive;
    class BitReader;
    class BitWriter;

    /** @brief 아레나 수치입니다. 거리는 m, 시간은 초입니다. */
    struct TrampolineSettings
    {
        float32 _step{ 1.0f / 60.0f };
        float32 _arenaRadius{ 8.0f };
        float32 _gravity{ 30.0f };
        float32 _baseBounceHeight{ 2.0f };
        float32 _comboHeightStep{ 0.75f };
        float32 _contactTime{ 0.12f }; ///< 면에 눌려 있는 시간 — 늦은 판정 창은 이보다 길 수 없다
        float32 _spawnHeight{ 3.0f };
        float32 _airMoveSpeed{ 6.0f };
        float32 _airAcceleration{ 20.0f };
        float32 _knockbackDrag{ 3.0f }; ///< 밀려난 속도가 줄어드는 정도(/초)
        float32 _bodyRadius{ 0.5f };
        float32 _attackDashSpeed{ 12.0f };
        float32 _attackDuration{ 0.25f };
        float32 _attackCooldown{ 0.6f };
        float32 _attackRadius{ 0.8f }; ///< 몸 반지름에 더한다
        float32 _attackKnockback{ 9.0f };
        float32 _stunTime{ 0.4f }; ///< 맞은 뒤 조작이 듣지 않는다
        float32 _poundSpeed{ 25.0f };
        float32 _poundMinHeight{ 1.0f };
        float32 _poundRadius{ 3.0f };
        float32 _poundKnockback{ 7.0f };
        float32 _poundHitHeight{ 1.0f }; ///< 이보다 낮은 상대만 내려찍기에 밀린다
        float32 _creditWindow{ 3.0f };
        float32 _fallDepth{ -4.0f }; ///< 밖으로 떨어져 이 높이 아래면 탈락
        float32 _respawnDelay{ 2.0f };
        float32 _spawnInvulnerable{ 1.0f };
        float32 _pickupRadius{ 1.0f };
        int32   _maxCombo{ 4 };
        int32   _ringOutScore{ 1 };
        int32   _selfOutPenalty{ 0 }; ///< 아무도 밀지 않았는데 떨어지면 깎는 점수
    };
} // namespace sw

namespace sw
{
    /** @brief 한 걸음의 입력입니다. 눌림은 다음 걸음 하나에만 듣습니다. */
    struct TrampolineInput
    {
        float32 _moveX{ 0.0f }; ///< −1..1
        float32 _moveZ{ 0.0f };
        uint8   _bJumpPressed{ SW_FALSE };
        uint8   _bAttackPressed{ SW_FALSE };
        uint8   _bPoundPressed{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 사람의 상태입니다. */
    enum class TrampolinePlayerState : uint8
    {
        Air = 0,
        Contact,   ///< 면에 눌려 있다
        Falling,   ///< 트램펄린 밖으로 떨어지는 중
        Respawning ///< 탈락 — 부활 대기
    };

    /** @brief 사람 한 명입니다. */
    struct TrampolinePlayer
    {
        float3                _position{};
        float3                _velocity{};
        float3                _facing{ 0.0f, 0.0f, 1.0f };
        TrampolineInput       _input{};
        float32               _landTime{ 0.0f };
        float32               _jumpPressTime{ -1000.0f };
        Countdown             _contactTimer{};
        Countdown             _attackTimer{};
        Countdown             _attackCooldown{};
        Countdown             _stunTimer{};
        Countdown             _invulnerableTimer{};
        float32               _lastHitTime{ -1000.0f };
        Countdown             _heavyTimer{};
        float32               _knockbackTaken{ 1.0f }; ///< Heavy 동안 받는 밀림 배율
        float32               _knockbackDealt{ 1.0f };
        int32                 _combo{ 0 };
        int32                 _lastHitter{ -1 };
        uint8                 _bPounding{ SW_FALSE };
        uint8                 _bSuperBounce{ SW_FALSE };
        uint8                 _bShield{ SW_FALSE };
        uint8                 _bHitThisDash{ SW_FALSE };
        TrampolinePlayerState _state{ TrampolinePlayerState::Air };
    };
} // namespace sw

namespace sw
{
    /** @brief 아레나에서 생긴 일입니다. */
    struct TrampolineEvent
    {
        enum class Kind : uint8
        {
            Landed = 0,
            Bounced,     ///< _value = 콤보, _grade = 판정(비면 창 밖)
            ComboBroken, ///< _value = 끊긴 콤보
            Attacked,
            Hit,           ///< _player = 맞은 쪽, _other = 때린 쪽
            ShieldBlocked, ///< _player = 막은 쪽
            GroundPound,   ///< _value = 밀린 사람 수
            RingOut,       ///< _player = 떨어진 쪽, _other = 점수를 받은 쪽(−1 = 없음)
            Respawned,
            ItemPicked ///< _itemID
        };
        hashed_string _grade{};
        hashed_string _itemID{};
        int32         _player{ -1 };
        int32         _other{ -1 };
        int32         _value{ 0 };
        Kind          _kind{ Kind::Landed };
    };
} // namespace sw

namespace sw
{
    /**
     * @class TrampolineArena
     * @brief 한 라운드의 아레나입니다. 점수 · 부활 대기 · 시간 제한은 안의 `MatchState`(사람마다 팀 하나)가 듭니다.
     */
    class SW_GF_API TrampolineArena
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "PTRA" );
        static constexpr uint32 kStateVersion = 1;

        TrampolineArena();

        /** @brief 2..4 명으로 라운드를 준비합니다. @p roundTime 0 은 시간 제한 없음, @p scoreLimit 0 은 점수 제한 없음입니다. */
        [[nodiscard]] bool initialize( const TrampolineSettings& settings, int32 playerCount, float32 roundTime, int32 scoreLimit, uint32 seed );
        /** @brief 튕김 판정 창입니다(기본 Perfect ±0.05 · Good ±0.1). 깨는 창(`_bBreaksCombo`)은 실패로 칩니다. */
        void              setTimingWindows( const vector<TimingWindow>& listWindow ) { _judge.setWindows( listWindow ); }
        PartyItemSpawner& getItemSpawner() { return _itemSpawner; }
        void              start();

        void setInput( int32 player, const TrampolineInput& input );
        /** @brief 프레임 시간을 고정 걸음으로 나눠 나아갑니다. 걸음 수입니다. */
        int32 update( float32 frameTime );
        void  step();

        /** @brief 락스텝 입력 직렬화입니다(3 + 8 + 8 비트). */
        static void            writeInput( BitWriter& writer, const TrampolineInput& input );
        static TrampolineInput readInput( BitReader& reader );

        /** @brief 사람마다 몸 · 받아 둔 입력 · 타이머 · 콤보 · 아이템 효과, 아이템 상태 · 경기(`MatchState`) · 고정 걸음 · 시각을 씁니다. 설정 · 판정 창 · 아이템 정의는 싣지 않고, 알림은 읽을 때 비웁니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다. 인원이 다르거나 깨졌으면 false 이고 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

        /** @brief 콤보 @p combo 의 튀는 높이입니다. */
        float32 computeBounceHeight( int32 combo ) const;
        /** @brief 이번 라운드 점수(사람 순서)입니다. */
        void computeRoundScores( vector<int32>& outListScore ) const;

        const TrampolinePlayer*   findPlayer( int32 player ) const;
        int32                     getPlayerCount() const { return static_cast<int32>( _listPlayer.size() ); }
        const MatchState&         getMatch() const { return _match; }
        bool                      isRoundOver() const { return _match.getPhase() == MatchPhase::Ended; }
        float32                   getTime() const { return _time; }
        const TrampolineSettings& getSettings() const { return _settings; }
        void                      drainEvents( vector<TrampolineEvent>& outListEvent );

    private:
        bool   isValidPlayer( int32 player ) const { return 0 <= player && player < static_cast<int32>( _listPlayer.size() ); }
        float3 makeSpawnPosition( int32 player ) const;
        void   updatePlayer( int32 player, float32 deltaTime );
        void   updateAirControl( TrampolinePlayer& body, float32 deltaTime );
        void   land( int32 player );
        void   launch( int32 player, bool bSuccess, const hashed_string& grade );
        void   updateAttacks();
        void   knockBack( int32 victim, int32 attacker, const float3& direction, float32 strength );
        void   ringOut( int32 player );
        void   respawn( int32 player );
        void   pickUpItems();
        void   applyItem( int32 player, const PartyItemDef& def );
        void   pushEvent( TrampolineEvent::Kind kind, int32 player, int32 other, int32 value );

        vector<TrampolinePlayer>     _listPlayer;
        EventBuffer<TrampolineEvent> _eventBuffer;
        vector<MatchEvent>           _listMatchEvent; ///< 걸음마다 다시 쓰는 경기 알림 자리
        TrampolineSettings           _settings;
        TimingJudge                  _judge;
        PartyItemSpawner             _itemSpawner;
        MatchState                   _match;
        FixedStepTimer               _timer;
        float32                      _time;
    };
} // namespace sw
