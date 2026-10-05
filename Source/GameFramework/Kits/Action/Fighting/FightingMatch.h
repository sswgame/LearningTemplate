/**
 * @file FightingMatch.h
 * @brief 철권 류 3D 격투의 프레임 시뮬레이션 — 두 캐릭터, 프레임마다 입력 → 커맨드(우선도 · 스트링 · 선입력) → 상태 기계 → 히트박스 대 허트 캡슐 →
 *        가드 높이 → 피해 · 경직 · 히트스톱, 띄우기 저글(감쇠 · 스크류 · 바운드 · 벽꽝), 잡기 풀기, 횡이동, 레이지 · 히트, 라운드 판정, 롤백용 상태 저장입니다.
 * @details 결정적입니다 — 같은 입력이면 같은 결과입니다(초 대신 프레임, 난수 없음). 60 프레임 고정 걸음마다 `advanceFrame` 을 부릅니다.
 *          롤백 넷코드 키트의 게임 쪽(`saveState` · `loadState` · 플레이어마다 1 바이트 입력의 `advanceFrame`)과 같은 모양이라 게임이 어댑터 하나로 붙입니다.
 *
 *          좌표는 바닥 평면 (x, z) 와 높이 y 입니다. 입력 방향은 화면 기준 넘패드이고, 플레이어 0 은 왼쪽(앞 = 6), 1 은 오른쪽(앞 = 4)에서 시작합니다.
 *          공격 방향은 기술 시작 때 상대 쪽으로 고정됩니다(추적 기술은 매 프레임 상대를 따라간다) — 그래서 횡이동으로 각을 벌리면 직선 기술이 빗나갑니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Combat/FrameData.h"
#include "GameFramework/Base/Input/InputCommandBuffer.h"
#include "GameFramework/Base/Match/RoundSeries.h"
#include "GameFramework/Base/Utility/EventBuffer.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Action/Fighting/FighterCatalog.h"

namespace sw
{
    /** @brief 대전 규칙 수치입니다. 시간은 모두 프레임(60 = 1 초)입니다. */
    struct FightingSettings
    {
        int32   _roundFrames{ 60 * 60 };     ///< 라운드 시간(1 보다 작으면 1)
        int32   _roundsToWin{ 2 };           ///< 이만큼 이기면 대전 끝(1 보다 작으면 1, 둘이 함께 닿으면 무승부)
        int32   _roundOverFrames{ 90 };      ///< 라운드가 끝나고 다음 라운드까지(1 보다 작으면 1)
        int32   _downFrames{ 30 };           ///< 다운 — 바닥에 누운 시간
        int32   _wakeupFrames{ 20 };         ///< 기상 — 무적으로 일어나는 시간
        int32   _wallSplatFrames{ 40 };      ///< 벽꽝 — 벽에 붙어 있는 시간
        int32   _throwBreakFrames{ 20 };     ///< 잡기 풀기 창
        int32   _throwBreakRecovery{ 16 };   ///< 잡기를 풀고 둘 다 못 움직이는 시간
        int32   _inputBufferFrames{ 8 };     ///< 못 움직이는 동안 넣은 커맨드를 기억하는 시간(선입력)
        int32   _stanceFrames{ 30 };         ///< 고유 자세를 유지하는 시간
        int32   _heatFrames{ 300 };          ///< 히트 지속
        float32 _stageHalfSize{ 5.0f };      ///< 정사각 무대의 반 너비(벽까지)
        float32 _startDistance{ 1.2f };      ///< 라운드 시작 거리
        float32 _wallNearDistance{ 0.6f };   ///< 몸이 벽에서 이만큼 안이면 "벽 근처"
        float32 _gravity{ 0.008f };          ///< 저글 중력(프레임²당)
        float32 _launchVelocity{ 0.14f };    ///< 띄우기의 위 속도
        float32 _juggleHitVelocity{ 0.08f }; ///< 공중 추가타의 위 속도(감쇠 비율을 곱한다)
        float32 _screwVelocity{ 0.11f };     ///< 스크류의 위 속도
        float32 _boundVelocity{ 0.1f };      ///< 바운드의 위 속도
        float32 _juggleCarrySpeed{ 0.004f }; ///< 저글이 밀려가는 속도(프레임당)
        float32 _screwCarrySpeed{ 0.03f };   ///< 스크류가 나르는 속도
        float32 _juggleDecayPerHit{ 0.15f }; ///< 공중 히트마다 피해 비율이 이만큼 준다
        float32 _juggleMinScale{ 0.3f };     ///< 피해 비율의 바닥
        float32 _juggleBodyHeight{ 0.9f };   ///< 떠 있는 몸의 허트 높이
        float32 _downBodyHeight{ 0.4f };     ///< 누운 몸의 허트 높이
        float32 _rageThreshold{ 0.25f };     ///< 체력 비율이 이 이하면 레이지
        float32 _rageDamageScale{ 1.1f };    ///< 레이지 중 주는 피해 배율
        float32 _heatDamageScale{ 1.1f };    ///< 히트 중 주는 피해 배율
        uint8   _bHeatEnabled{ SW_FALSE };   ///< 히트 시스템을 쓰는가(히트 발동 기술이 나간다)
    };
} // namespace sw

namespace sw
{
    /** @brief 캐릭터 상태 기계입니다. */
    enum class FighterState : uint8
    {
        Neutral = 0, ///< 움직일 수 있다(서기 · 앉기 · 고유 자세 · 걷기 · 가드)
        Attacking,   ///< 기술 중(발생 · 지속 · 후딜)
        Sidestep,    ///< 횡이동 중(가드 없음, 기술로 캔슬 가능)
        Jump,        ///< 점프 중(공중 기술 가능)
        Blockstun,   ///< 가드 경직(잡기를 푼 뒤 경직도)
        Hitstun,     ///< 히트 경직
        Juggle,      ///< 떠서 저글당하는 중
        WallSplat,   ///< 벽꽝 — 벽에 붙어 맞는 중
        Down,        ///< 다운
        Wakeup,      ///< 기상(무적)
        Throwing,    ///< 잡은 쪽
        ThrowBreak,  ///< 잡힌 쪽 — 풀기 창
        Knockout
    };

    /** @brief 일어난 일입니다. `drainEvents` 로 가져갑니다(상태 저장에는 들지 않는다 — 다시 돌릴 때 게임이 알아서 거른다). */
    struct FightingEvent
    {
        enum class Kind : uint8
        {
            MoveStarted = 0, ///< `_value` = 기술 자리
            Hit,             ///< `_value` = 피해
            Blocked,         ///< `_value` = 가드 피해
            Screw,
            Bound,
            WallSplat,
            ThrowGrab,
            ThrowBroken,
            ThrowLanded, ///< `_value` = 피해
            Sidestep,
            RageEntered,
            RageArt,
            HeatActivated,
            Knockout,
            TimeOut,
            RoundEnd, ///< `_value` = 이긴 쪽(`FightingMatch::kDraw` 면 무승부)
            MatchEnd  ///< `_value` = 이긴 쪽
        };

        hashed_string _moveId{};
        int32         _player{ -1 }; ///< 한 쪽(공격 · 발동한 쪽)
        int32         _value{ 0 };
        Kind          _kind{ Kind::MoveStarted };
    };
} // namespace sw

namespace sw
{
    /** @brief 캐릭터 하나의 실행 상태입니다. 모두 상태 저장에 들어갑니다(`_pDef` 는 빌린 정의 — 저장하지 않는다). */
    struct SW_GF_API FighterRuntime
    {
        MoveTimeline       _timeline{};
        InputCommandBuffer _inputBuffer{};
        const FighterDef*  _pDef{ nullptr };
        float32            _x{ 0.0f };
        float32            _z{ 0.0f };
        float32            _y{ 0.0f };    ///< 높이(점프 · 저글)
        float32            _dirX{ 1.0f }; ///< 공격 · 바라보는 방향(단위 벡터)
        float32            _dirZ{ 0.0f };
        float32            _velocityY{ 0.0f };
        float32            _gravity{ 0.0f };
        float32            _carryX{ 0.0f }; ///< 공중에서 밀려가는 속도
        float32            _carryZ{ 0.0f };
        int32              _health{ 0 };
        int32              _stateFrames{ 0 }; ///< 상태마다의 남은 프레임(경직 · 다운 · 기상 · 풀기 창 · 횡이동 …)
        int32              _hitstop{ 0 };     ///< 맞은 쪽 히트스톱(때린 쪽은 타임라인이 든다)
        int32              _moveIndex{ -1 };  ///< 지금 기술(`_pDef->_listMove` 자리)
        int32              _bufferedMove{ -1 };
        int32              _bufferedAge{ 0 };
        int32              _stanceIndex{ -1 };
        int32              _stanceFrames{ 0 };
        int32              _comboHits{ 0 }; ///< 이번 콤보에 맞은 수
        int32              _airHits{ 0 };   ///< 떠서 맞은 수(띄우기 포함) — 피해 감쇠
        int32              _heatFrames{ 0 };
        int32              _sidestepSign{ 0 }; ///< +1 안쪽, −1 바깥쪽
        int32              _side{ 1 };         ///< 화면 쪽 — +1 이면 앞 = 6
        uint16             _breakButtons{ 0 }; ///< 잡혔을 때 풀기 버튼
        FighterState       _state{ FighterState::Neutral };
        FighterPosture     _posture{ FighterPosture::Standing };
        GuardStance        _guard{ GuardStance::Standing };
        uint8              _bAirborne{ SW_FALSE };
        uint8              _bScrewUsed{ SW_FALSE };
        uint8              _bBoundUsed{ SW_FALSE };
        uint8              _bWallSplatUsed{ SW_FALSE };
        uint8              _bRage{ SW_FALSE };
        uint8              _bRageUsed{ SW_FALSE }; ///< 이번 라운드에 레이지 아츠를 썼다(레이지가 다시 켜지지 않는다)
        uint8              _bHeatUsed{ SW_FALSE };
        uint8              _bThrowAttempted{ SW_FALSE }; ///< 잡혔을 때 이미 버튼을 눌렀다(한 번만 시도)

        /** @brief 지금 기술입니다. 없으면 nullptr 입니다. */
        const FighterMove* findCurrentMove() const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class FightingMatch
     * @brief 1 대 1 대전 하나입니다. 캐릭터 정의(`FighterCatalog`)는 빌려 쓰므로 대전보다 오래 살아야 합니다.
     * @details 한 프레임의 순서: 입력을 버퍼에 쌓는다 → 상태를 한 프레임 진행(경직 · 다운 · 저글 물리 · 횡이동 · 잡기 풀기 창) →
     *          움직일 수 있으면 커맨드(우선도가 높은 것, 같으면 단계가 많은 것 — 기반 규칙)로 기술 · 스트링 · 횡이동 · 점프, 아니면 선입력에 담는다 →
     *          몸 겹침 · 벽 → 두 쪽 판정을 함께 본 뒤 함께 적용(상쇄) → K.O. · 레이지 · 타이머.
     *          경직은 닿은 다음 프레임부터 세어 `MoveTimeline::computeFrameAdvantage` 의 이득이 그대로 성립합니다(−10 이면 10 프레임 기술이 확정).
     *          라운드 번호 · 선승 · 라운드 시간 · 라운드 사이 대기는 기반 `RoundSeries` 가 듭니다 — 라운드 승은 1 위 1 점, 무승부 라운드는 둘 다 1 승,
     *          둘이 함께 선승에 닿으면 대전 무승부입니다.
     */
    class SW_GF_API FightingMatch
    {
    public:
        static constexpr int32 kPlayerCount = 2;
        static constexpr int32 kDraw        = 2; ///< 라운드 · 대전 결과의 무승부

        FightingMatch();

        /** @brief 두 캐릭터로 대전을 새로 시작합니다(1 라운드). */
        void initialize( const FighterDef& fighter0, const FighterDef& fighter1, const FightingSettings& settings = FightingSettings{} );
        /** @brief 한 프레임 진행합니다. 입력 방향은 화면 기준입니다. */
        void advanceFrame( const InputFrame& input0, const InputFrame& input1 );
        /** @brief 롤백 어댑터용 — 플레이어 순 1 바이트 입력(`encodeInput`)입니다. 모자란 플레이어는 중립입니다. */
        void advanceFrame( const vector<uint8>& listInput );

        /** @brief 전체 상태를 바이트로 씁니다(이벤트는 빼고). */
        void saveState( vector<uint8>& outBuffer ) const;
        /** @brief `saveState` 의 바이트로 되돌립니다. 같은 캐릭터로 `initialize` 한 대전이어야 하고, 깨졌거나 맞지 않으면 false 이고 바꾸지 않습니다. */
        [[nodiscard]] bool loadState( const vector<uint8>& buffer );
        void               drainEvents( vector<FightingEvent>& outListEvent );

        /** @brief 연습 모드 · 시험용 — 위치를 옮깁니다(벽 안으로 자른다). */
        void setFighterPosition( int32 player, float32 x, float32 z );
        /** @brief 연습 모드 · 시험용 — 체력을 정합니다(레이지 판정은 다음 프레임). */
        void setFighterHealth( int32 player, int32 health );

        const FighterRuntime&   getFighter( int32 player ) const { return _arrFighter[player]; }
        const FightingSettings& getSettings() const { return _settings; }
        /** @brief 라운드 묶음(단계 · 라운드 번호 · 남은 시간 · 대기 · 라운드 승)입니다. */
        const RoundSeries& getSeries() const { return _series; }
        RoundSeriesPhase   getPhase() const { return _series.getPhase(); }
        int32              getFrame() const { return _frame; }
        /** @brief 지금 라운드(1 부터, `initialize` 전 0)입니다. */
        int32 getRound() const;
        int32 getRoundFramesRemaining() const { return _series.getRoundTicksRemaining(); }
        /** @brief 이긴 라운드 수입니다(무승부 라운드는 둘 다 1 승). */
        int32 getRoundWins( int32 player ) const { return _series.getTotal( player ); }
        /** @brief 지난 라운드 결과(0 · 1 · `kDraw`, 아직 없으면 −1)입니다. */
        int32 getLastRoundWinner() const { return _lastRoundWinner; }
        /** @brief 대전 결과(0 · 1 · `kDraw`, 끝나지 않았으면 −1)입니다. */
        int32 getMatchWinner() const;
        /** @brief 두 캐릭터 바닥 거리입니다. */
        float32 computeDistance() const;
        /** @brief 몸이 벽에서 `_wallNearDistance` 안인가입니다. */
        bool isNearWall( int32 player ) const;

        /** @brief 입력 하나를 1 바이트로 — 아래 4 비트 버튼, 위 4 비트 방향입니다. */
        static uint8      encodeInput( const InputFrame& frame );
        static InputFrame decodeInput( uint8 byte );

    private:
        /** @brief 두 몸을 출발 자리로 되돌립니다(라운드 번호 · 시간은 `_series` 가 연다). */
        void startRound();
        void resetFighter( int32 player );
        void updateFighter( int32 player, uint16 pressedButtons );
        void updateControl( int32 player, const InputFrame& input );
        void resolveSpacing();
        void resolveHits();
        void updateRoundRules();

        /** @brief 지금 낼 수 있는가입니다. @p bAsIfFree 면 움직일 수 있다고 치고(선입력 후보) 자세 · 조건만 봅니다. */
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
        void  pushEvent( FightingEvent::Kind kind, int32 player, int32 value, const hashed_string& moveId = hashed_string() );

        FighterRuntime             _arrFighter[kPlayerCount];
        FightingSettings           _settings;
        RoundSeries                _series; ///< 라운드 번호 · 시간 · 대기 · 라운드 승 · 대전 결과(롤백 상태의 맨 뒤)
        EventBuffer<FightingEvent> _eventBuffer;
        vector<MoveHitbox>         _listHitboxScratch; ///< 판정용 임시(상태 아님)
        int32                      _frame;
        int32                      _lastRoundWinner;
    };
} // namespace sw
