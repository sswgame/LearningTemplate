/**
 * @file FightingMatch.h
 * @brief 철권 같은 3D 격투의 프레임 시뮬레이션입니다. 캐릭터 둘이 프레임마다 입력, 커맨드, 상태 기계, 히트박스 판정을 거치고, 라운드 판정과 롤백용 상태 저장까지 처리합니다.
 * @details 결과는 결정적이다. 같은 입력이면 같은 결과가 나온다(시간 대신 프레임을 쓰고 난수는 쓰지 않는다). 60프레임 고정 간격마다 `advanceFrame` 을 부른다.
 *          롤백 넷코드 키트의 게임 쪽 인터페이스(`saveState`, `loadState`, 플레이어마다 1바이트 입력을 받는 `advanceFrame`)와 같은 구조라서 게임은 어댑터 하나로 붙인다.
 *
 *          좌표는 바닥 평면 (x, z) 와 높이 y 다. 입력 방향은 화면 기준 넘패드이고, 플레이어 0은 왼쪽(앞 = 6), 1은 오른쪽(앞 = 4)에서 시작한다.
 *          공격 방향은 기술을 시작할 때 상대 쪽으로 고정된다(추적 기술은 매 프레임 상대를 따라간다). 그래서 횡이동으로 각도를 벌리면 직선 기술이 빗나간다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/EventBuffer.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Actor/Combat/FrameData.h"
#include "GameFramework/Base/Actor/Input/InputCommandBuffer.h"
#include "GameFramework/Base/Gameplay/Match/RoundSeries.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Action/Fighting/FighterCatalog.h"

namespace sw
{
    class Archive;
    class ValidationContext;

    /** @brief 대전 규칙 수치입니다. 시간 단위는 모두 프레임이며 60프레임이 1초입니다. */
    struct FightingSettings
    {
        int32   _roundFrames{ 60 * 60 };     ///< 라운드 시간(1 보다 작으면 1)
        int32   _roundsToWin{ 2 };           ///< 이만큼 이기면 대전 끝(1 보다 작으면 1, 둘이 함께 닿으면 무승부)
        int32   _roundOverFrames{ 90 };      ///< 라운드가 끝나고 다음 라운드까지(1 보다 작으면 1)
        int32   _downFrames{ 30 };           ///< 다운 상태로 바닥에 누워 있는 시간
        int32   _wakeupFrames{ 20 };         ///< 기상 중 무적으로 일어나는 시간
        int32   _wallSplatFrames{ 40 };      ///< 벽꽝 상태로 벽에 붙어 있는 시간
        int32   _throwBreakFrames{ 20 };     ///< 잡기 풀기 입력을 받는 시간
        int32   _throwBreakRecovery{ 16 };   ///< 잡기를 푼 뒤 둘 다 움직일 수 없는 시간
        int32   _inputBufferFrames{ 8 };     ///< 움직일 수 없는 동안 입력한 커맨드를 기억하는 시간(선입력)
        int32   _stanceFrames{ 30 };         ///< 고유 자세를 유지하는 시간
        int32   _heatFrames{ 300 };          ///< 히트 상태가 지속되는 시간
        float32 _stageHalfSize{ 5.0f };      ///< 정사각 무대의 반 너비(중심에서 벽까지)
        float32 _startDistance{ 1.2f };      ///< 라운드 시작 거리
        float32 _wallNearDistance{ 0.6f };   ///< 몸이 벽에서 이 거리 안에 있으면 "벽 근처"로 본다
        float32 _gravity{ 0.008f };          ///< 저글 중력(프레임² 당)
        float32 _launchVelocity{ 0.14f };    ///< 띄우기의 위 속도
        float32 _juggleHitVelocity{ 0.08f }; ///< 공중 추가타의 위쪽 속도(감쇠 비율을 곱한다)
        float32 _screwVelocity{ 0.11f };     ///< 스크류의 위 속도
        float32 _boundVelocity{ 0.1f };      ///< 바운드의 위 속도
        float32 _juggleCarrySpeed{ 0.004f }; ///< 저글 상태에서 밀려가는 속도(프레임당)
        float32 _screwCarrySpeed{ 0.03f };   ///< 스크류 상태에서 밀려가는 속도
        float32 _juggleDecayPerHit{ 0.15f }; ///< 공중 히트마다 피해 비율이 이만큼 줄어든다
        float32 _juggleMinScale{ 0.3f };     ///< 피해 비율의 하한
        float32 _juggleBodyHeight{ 0.9f };   ///< 떠 있는 몸의 허트 높이
        float32 _downBodyHeight{ 0.4f };     ///< 누운 몸의 허트 높이
        float32 _rageThreshold{ 0.25f };     ///< 체력 비율이 이 이하면 레이지
        float32 _rageDamageScale{ 1.1f };    ///< 레이지 중 주는 피해 배율
        float32 _heatDamageScale{ 1.1f };    ///< 히트 중 주는 피해 배율
        uint8   _bHeatEnabled{ SW_FALSE };   ///< 히트 시스템을 쓰는가(켜면 히트 발동 기술이 나간다)
    };
} // namespace sw

namespace sw
{
    /** @brief 캐릭터의 상태 기계 상태입니다. */
    ENUM()
    enum class FighterState : uint8
    {
        Neutral = 0, ///< 움직일 수 있다(서기, 앉기, 고유 자세, 걷기, 가드)
        Attacking,   ///< 기술 중(발생, 지속, 후딜)
        Sidestep,    ///< 횡이동 중(가드 불가, 기술로 캔슬 가능)
        Jump,        ///< 점프 중(공중 기술 가능)
        Blockstun,   ///< 가드 경직(잡기를 푼 뒤의 경직도 포함)
        Hitstun,     ///< 히트 경직
        Juggle,      ///< 떠서 저글을 맞는 중
        WallSplat,   ///< 벽꽝, 벽에 붙어 맞는 중
        Down,        ///< 다운
        Wakeup,      ///< 기상(무적)
        Throwing,    ///< 잡은 쪽
        ThrowBreak,  ///< 잡힌 쪽, 잡기 풀기 입력을 받는 중
        Knockout
    };

    /** @brief 대전 중에 일어난 일입니다. `drainEvents` 로 가져갑니다. 상태 저장에는 포함되지 않으며, 롤백으로 다시 돌릴 때 중복 이벤트는 게임이 걸러야 합니다. */
    struct FightingEvent
    {
        enum class Kind : uint8
        {
            MoveStarted = 0, ///< `_value` 는 기술 인덱스
            Hit,             ///< `_value` 는 피해
            Blocked,         ///< `_value` 는 가드 피해
            Screw,
            Bound,
            WallSplat,
            ThrowGrab,
            ThrowBroken,
            ThrowLanded, ///< `_value` 는 피해
            Sidestep,
            RageEntered,
            RageArt,
            HeatActivated,
            Knockout,
            TimeOut,
            RoundEnd, ///< `_value` 는 이긴 쪽(`FightingMatch::kDraw` 면 무승부)
            MatchEnd  ///< `_value` 는 이긴 쪽
        };

        hashed_string _moveID{};
        int32         _player{ -1 }; ///< 이벤트와 관련된 플레이어(공격하거나 발동한 쪽)
        int32         _value{ 0 };
        Kind          _kind{ Kind::MoveStarted };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 캐릭터 하나의 실행 상태입니다. `PROPERTY` 필드가 롤백(고정 배치형)과 세이브(자기 기술형)에 들어갑니다.
     * @details `_pDef` 는 빌려 온 정의라서 싣지 않습니다. 읽기는 대상의 복사본에 읽으므로 정의 포인터가 그대로 이어지고,
     *          기술 · 자세 인덱스가 그 정의 안에 있는지는 `FightingMatch::validateState` 가 봅니다. 필드 순서가 롤백 바이트의 순서입니다.
     */
    REFLECT()
    struct SW_KIT_API FighterRuntime
    {
        REFLECT_BODY();
        PROPERTY()
        MoveTimeline _timeline{};
        PROPERTY()
        InputCommandBuffer _inputBuffer{};
        const FighterDef*  _pDef{ nullptr };
        PROPERTY()
        uint32 _fighterIDHash{ 0 }; ///< 정의 id(`FighterDef::_id`)의 해시. 읽은 상태가 같은 캐릭터의 것인지 대조한다
        PROPERTY()
        float32 _x{ 0.0f };
        PROPERTY()
        float32 _z{ 0.0f };
        PROPERTY()
        float32 _y{ 0.0f }; ///< 높이(점프, 저글)
        PROPERTY()
        float32 _dirX{ 1.0f }; ///< 공격하거나 바라보는 방향(단위 벡터)
        PROPERTY()
        float32 _dirZ{ 0.0f };
        PROPERTY()
        float32 _velocityY{ 0.0f };
        PROPERTY()
        float32 _gravity{ 0.0f };
        PROPERTY()
        float32 _carryX{ 0.0f }; ///< 공중에서 밀려가는 속도
        PROPERTY()
        float32 _carryZ{ 0.0f };
        PROPERTY()
        int32 _health{ 0 };
        PROPERTY()
        int32 _stateFrames{ 0 }; ///< 상태별 남은 프레임(경직, 다운, 기상, 풀기 입력 시간, 횡이동 등)
        PROPERTY()
        int32 _hitstop{ 0 }; ///< 맞은 쪽의 히트스톱(때린 쪽은 타임라인이 가지고 있다)
        PROPERTY( Min = -1 )
        int32 _moveIndex{ -1 }; ///< 지금 쓰는 기술(`_pDef->_listMove` 인덱스)
        PROPERTY( Min = -1 )
        int32 _bufferedMove{ -1 };
        PROPERTY()
        int32 _bufferedAge{ 0 };
        PROPERTY( Min = -1 )
        int32 _stanceIndex{ -1 };
        PROPERTY()
        int32 _stanceFrames{ 0 };
        PROPERTY( Min = 0 )
        int32 _comboHits{ 0 }; ///< 이번 콤보에 맞은 수
        PROPERTY( Min = 0 )
        int32 _airHits{ 0 }; ///< 떠서 맞은 횟수(띄우기 포함, 피해 감쇠에 쓴다)
        PROPERTY()
        int32 _heatFrames{ 0 };
        PROPERTY( Min = -1, Max = 1 )
        int32 _sidestepSign{ 0 }; ///< +1은 안쪽, −1은 바깥쪽
        PROPERTY( Min = -1, Max = 1 )
        int32 _side{ 1 }; ///< 화면 쪽, +1이면 앞 = 6
        PROPERTY()
        uint16 _breakButtons{ 0 }; ///< 잡혔을 때 풀기에 쓰는 버튼
        PROPERTY()
        FighterState _state{ FighterState::Neutral };
        PROPERTY()
        FighterPosture _posture{ FighterPosture::Standing };
        PROPERTY()
        GuardStance _guard{ GuardStance::Standing };
        PROPERTY( Min = 0, Max = 1 )
        uint8 _bAirborne{ SW_FALSE };
        PROPERTY( Min = 0, Max = 1 )
        uint8 _bScrewUsed{ SW_FALSE };
        PROPERTY( Min = 0, Max = 1 )
        uint8 _bBoundUsed{ SW_FALSE };
        PROPERTY( Min = 0, Max = 1 )
        uint8 _bWallSplatUsed{ SW_FALSE };
        PROPERTY( Min = 0, Max = 1 )
        uint8 _bRage{ SW_FALSE };
        PROPERTY( Min = 0, Max = 1 )
        uint8 _bRageUsed{ SW_FALSE }; ///< 이번 라운드에 레이지 아츠를 썼다(레이지가 다시 켜지지 않는다)
        PROPERTY( Min = 0, Max = 1 )
        uint8 _bHeatUsed{ SW_FALSE };
        PROPERTY( Min = 0, Max = 1 )
        uint8 _bThrowAttempted{ SW_FALSE }; ///< 잡혔을 때 이미 버튼을 눌렀다(한 번만 시도할 수 있다)

        /** @brief 지금 쓰는 기술입니다. 없으면 nullptr 입니다. */
        const FighterMove* findCurrentMove() const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class FightingMatch
     * @brief 1대1 대전 하나입니다. 캐릭터 정의(`FighterCatalog`)는 빌려 쓰므로 대전보다 오래 살아야 합니다.
     * @details 한 프레임은 다음 순서로 진행한다.
     *          1. 입력을 버퍼에 쌓는다.
     *          2. 상태를 한 프레임 진행한다(경직, 다운, 저글 물리, 횡이동, 잡기 풀기 입력 시간).
     *          3. 움직일 수 있으면 커맨드로 기술, 스트링, 횡이동, 점프를 실행한다. 커맨드는 우선도가 높은 것을 고르고, 같으면 단계가 많은 것을 고른다(기반 규칙). 움직일 수 없으면 선입력 버퍼에 담는다.
     *          4. 몸 겹침과 벽을 처리한다.
     *          5. 두 쪽의 판정을 함께 본 뒤 함께 적용한다(상쇄).
     *          6. K.O., 레이지, 타이머를 처리한다.
     *          경직은 닿은 다음 프레임부터 센다. 그래서 `MoveTimeline::computeFrameAdvantage` 의 이득이 그대로 성립한다(−10이면 10프레임 기술이 확정된다).
     *          라운드 번호, 선승 조건, 라운드 시간, 라운드 사이 대기는 기반 클래스 `RoundSeries` 가 가진다. 라운드 승은 1위가 1점이고, 무승부 라운드는 둘 다 1승이다.
     *          둘이 함께 선승 조건에 닿으면 대전이 무승부다.
     *
     *          상태는 `PROPERTY` 필드(두 캐릭터, 라운드 시리즈, 프레임, 지난 라운드 결과) 한 벌이고, 형식은 쓰는 곳이 고른다.
     *          롤백(`saveState` · `loadState`)은 같은 빌드끼리만 읽는 고정 배치형(`FixedLayout`)이고, 세이브(`writeState` · `readState`)는
     *          버전 머리가 붙은 자기 기술형(`BinarySerializer`)이다. 롤백 바이트를 세이브에 싣지 않으므로 빌드가 바뀌어도 세이브는 마이그레이션 경로로 읽힌다.
     *          설정, 이벤트, 판정 임시 버퍼는 상태가 아니라서 싣지 않는다.
     */
    REFLECT( Validate = validateState )
    class SW_KIT_API FightingMatch
    {
    public:
        REFLECT_BODY();
        /** @brief 세이브 본문(자기 기술형)의 버전입니다. 필드의 뜻이 바뀌면 올리고 `SchemaMigrateFn` 을 등록합니다. 롤백 바이트는 스키마 해시가 가리므로 버전이 없습니다. */
        static constexpr uint32 kStateVersion = 1;
        static constexpr int32  kPlayerCount  = 2;
        static constexpr int32  kDraw         = 2; ///< 라운드와 대전 결과의 무승부 값

        FightingMatch();

        /** @brief 두 캐릭터로 대전을 새로 시작합니다(1라운드부터). */
        void initialize( const FighterDef& fighter0, const FighterDef& fighter1, const FightingSettings& settings = FightingSettings{} );
        /** @brief 한 프레임 진행합니다. 입력 방향은 화면 기준입니다. */
        void advanceFrame( const InputFrame& input0, const InputFrame& input1 );
        /** @brief 롤백 어댑터용 `advanceFrame` 입니다. 플레이어 순서대로 1바이트 입력(`encodeInput`)을 받고, 모자란 플레이어는 중립으로 처리합니다. */
        void advanceFrame( const vector<uint8>& listInput );

        /** @brief 이벤트를 뺀 전체 상태를 롤백 바이트(고정 배치형)로 씁니다. 범위 밖 값이 있으면 결함이므로 오류를 남기고 단언하며, @p outBuffer 는 빈 채입니다. */
        void saveState( vector<uint8>& outBuffer ) const;
        /** @brief `saveState` 가 쓴 바이트로 상태를 되돌립니다. 같은 캐릭터로 `initialize` 한 대전이어야 하고, 바이트가 깨졌거나 맞지 않으면 상태를 바꾸지 않고 false 를 반환합니다. */
        [[nodiscard]] bool loadState( const vector<uint8>& buffer );
        /** @brief 세이브(`Archive`)에 상태를 버전 머리(`kStateVersion`)가 붙은 자기 기술형으로 씁니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 본문으로 상태를 되돌립니다. 같은 캐릭터로 `initialize` 한 대전이어야 하고, 본문이 잘렸거나 맞지 않으면 상태를 바꾸지 않고 false 를 반환합니다. */
        [[nodiscard]] bool readState( Archive& archive );
        void               drainEvents( vector<FightingEvent>& outListEvent );
        /**
         * @brief 읽은 상태가 이 대전의 캐릭터 정의와 맞는지 봅니다(`REFLECT( Validate )`, 롤백과 세이브가 함께 씁니다).
         * @details 같은 캐릭터인지, 기술 · 자세 인덱스가 정의 안인지, 재생 중인 타임라인의 프레임이 그 기술 안인지, 참가자가 둘인지 봅니다.
         */
        void validateState( ValidationContext& context ) const;

        /** @brief 연습 모드와 테스트에서 위치를 옮깁니다. 벽 안으로 잘라 냅니다. */
        void setFighterPosition( int32 player, float32 x, float32 z );
        /** @brief 연습 모드와 테스트에서 체력을 정합니다. 레이지 판정은 다음 프레임에 반영됩니다. */
        void setFighterHealth( int32 player, int32 health );

        const FighterRuntime&   getFighter( int32 player ) const { return _arrFighter[player]; }
        const FightingSettings& getSettings() const { return _settings; }
        /** @brief 라운드 진행 상태(단계, 라운드 번호, 남은 시간, 대기, 라운드 승)입니다. */
        const RoundSeries& getSeries() const { return _series; }
        RoundSeriesPhase   getPhase() const { return _series.getPhase(); }
        int32              getFrame() const { return _frame; }
        /** @brief 지금 라운드 번호입니다. 1부터 시작하고 `initialize` 전에는 0입니다. */
        int32 getRound() const;
        int32 getRoundFramesRemaining() const { return _series.getRoundTicksRemaining(); }
        /** @brief 이긴 라운드 수입니다. 무승부 라운드는 둘 다 1승으로 셉니다. */
        int32 getRoundWins( int32 player ) const { return _series.getTotal( player ); }
        /** @brief 지난 라운드의 결과입니다. 0, 1, `kDraw` 중 하나이고 아직 없으면 −1입니다. */
        int32 getLastRoundWinner() const { return _lastRoundWinner; }
        /** @brief 대전 결과입니다. 0, 1, `kDraw` 중 하나이고 끝나지 않았으면 −1입니다. */
        int32 getMatchWinner() const;
        /** @brief 두 캐릭터 사이의 바닥 거리입니다. */
        float32 computeDistance() const;
        /** @brief 몸이 벽에서 `_wallNearDistance` 안에 있는지 반환합니다. */
        bool isNearWall( int32 player ) const;

        /** @brief 입력 하나를 1바이트로 만듭니다. 아래 4비트가 버튼이고 위 4비트가 방향입니다. */
        static uint8      encodeInput( const InputFrame& frame );
        static InputFrame decodeInput( uint8 byte );

    private:
        /** @brief 두 몸을 출발 위치로 되돌립니다. 라운드 번호와 시간은 `_series` 가 다시 시작합니다. */
        void startRound();
        void resetFighter( int32 player );
        void updateFighter( int32 player, uint16 pressedButtons );
        void updateControl( int32 player, const InputFrame& input );
        void resolveSpacing();
        void resolveHits();
        void updateRoundRules();

        /** @brief 지금 낼 수 있는지 반환합니다. @p bAsIfFree 가 true 면 움직일 수 있다고 가정하고(선입력 후보) 자세와 조건만 봅니다. */
        bool  isMoveAllowed( int32 player, int32 moveIndex, bool bAsIfFree = false ) const;
        bool  isUpcomingCancelTarget( int32 player, int32 moveIndex ) const;
        int32 findCompletedMove( int32 player, bool bBufferCandidates ) const;
        void  startMove( int32 player, int32 moveIndex );
        void  finishMove( int32 player );
        bool  testHit( int32 attacker );
        void  applyHit( int32 attacker );
        void  landThrow( int32 defender );
        int32 computeDamage( int32 attacker, float32 baseDamage, bool bComboScaled ) const;
        void  applyDamage( int32 defender, int32 damage );
        void  updateAirMotion( int32 player );
        void  aimAtOpponent( int32 player );
        void  clampToStage( int32 player );
        void  enterState( int32 player, FighterState state, int32 frames );
        void  resetCombo( int32 player );
        void  endRound( int32 winner, FightingEvent::Kind reason );
        void  pushEvent( FightingEvent::Kind kind, int32 player, int32 value, const hashed_string& moveID = hashed_string() );
        /** @brief 읽은 타임라인에 기술 정의를 다시 붙이고 이벤트를 비웁니다. `validateState` 를 통과한 상태에만 부릅니다. */
        void completeLoad();

        PROPERTY()
        FighterRuntime   _arrFighter[kPlayerCount];
        FightingSettings _settings;
        PROPERTY()
        RoundSeries                _series; ///< 라운드 번호, 시간, 대기, 라운드 승, 대전 결과
        EventBuffer<FightingEvent> _eventBuffer;
        vector<MoveHitbox>         _listHitboxScratch; ///< 판정에 쓰는 임시 버퍼(상태가 아님)
        PROPERTY( Min = 0 )
        int32 _frame;
        PROPERTY( Min = -1, Max = 2 )
        int32 _lastRoundWinner; ///< 0, 1, `kDraw`(2), 아직 없으면 −1
    };
} // namespace sw
