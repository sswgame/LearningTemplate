#include "pch.h"

#include "GameFramework/Kits/Genre/Action/Fighting/FightingMatch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"

#include "Engine/Reflection/ReflectionValidation.h"
#include "Engine/Serialization/Base/SerializerUtil.h"
#include "Engine/Serialization/Fixed/FixedLayout.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/BinarySerializer.h"

#include "GameFramework/Base/Foundation/Utility/ConfigRangeUtil.h"

namespace sw
{
    SW_LOG_CALLER( "FightingMatch" );

    namespace
    {
        struct FightingMatchInternal
        {
            static constexpr uint16  kButtonMask    = 0x0F;    ///< 1바이트 입력에 담는 버튼 4개
            static constexpr float32 kRoundingSlack = 1.0e-3f; ///< 0.55 × 10 같은 반올림 경계가 부동소수 오차 때문에 내려가지 않게 하는 여유값

            static bool isGuardDirectionBack( uint8 relativeDirection ) { return relativeDirection == 4 || relativeDirection == 7; }

            static bool isCrouchDirection( uint8 relativeDirection ) { return 1 <= relativeDirection && relativeDirection <= 3; }

            /** @brief 재생 중인 타임라인에 지금 기술의 정의를 다시 붙입니다. 기술이 없거나 프레임이 그 기술 밖이면 false 이고 타임라인은 그대로입니다. */
            [[nodiscard]] static bool relinkTimeline( FighterRuntime& fighter )
            {
                MoveTimeline& timeline = fighter._timeline;
                if ( timeline.isPlaying() == false )
                    return true;
                const FighterMove* pMove = fighter.findCurrentMove();
                return pMove != nullptr &&
                       timeline.restoreState( pMove->_frame, timeline.getFrame(), timeline.getHitstopRemaining(), timeline.hasContact(), timeline.wasBlocked() );
            }

            /** @brief 격투의 라운드 규칙입니다. 라운드 승은 1위가 1점이고(순위 점수 목록은 없다), 둘이 함께 선승 조건에 닿으면 무승부입니다. 시간과 대기가 최소 1프레임인 것은 `FightingSettings` 의 범위가 지킨다. */
            static RoundSeriesSettings makeSeriesSettings( const FightingSettings& settings )
            {
                RoundSeriesSettings series;
                series._winScore          = settings._roundsToWin;
                series._roundTicks        = settings._roundFrames;
                series._intermissionTicks = settings._roundOverFrames;
                series._tieRule           = RoundSeriesTieRule::Draw;
                return series;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // FighterRuntime
    // ------------------------------------------------------------------------------
    const FighterMove* FighterRuntime::findCurrentMove() const
    {
        const bool bMoveIndexInvalid = _pDef == nullptr || _moveIndex < 0 || _moveIndex >= static_cast<int32>( _pDef->_listMove.size() );
        if ( bMoveIndexInvalid )
            return nullptr;
        return &_pDef->_listMove[static_cast<size_t>( _moveIndex )];
    }

    // ------------------------------------------------------------------------------
    // FightingMatch
    // ------------------------------------------------------------------------------
    FightingMatch::FightingMatch()
        : _arrFighter{}
        , _settings{}
        , _series{}
        , _eventBuffer{}
        , _listHitboxScratch{}
        , _frame{ 0 }
        , _lastRoundWinner{ -1 }
    {
    }

    bool FightingMatch::initialize( const FighterDef& fighter0, const FighterDef& fighter1, const FightingSettings& settings )
    {
        const string rangeError = ConfigRangeUtil::describeRangeErrors( settings );
        if ( rangeError.empty() == false )
        {
            SW_LOG_ERROR( "FightingMatch: refused settings - %#", rangeError.c_str() );
            return false;
        }
        if ( _series.initialize( FightingMatchInternal::makeSeriesSettings( settings ) ) == false )
            return false;
        _settings = settings;
        _eventBuffer.clear();
        _frame                                 = 0;
        _lastRoundWinner                       = -1;
        const FighterDef* arrDef[kPlayerCount] = { &fighter0, &fighter1 };
        for ( int32 player = 0; player < kPlayerCount; ++player )
        {
            _arrFighter[player]                = FighterRuntime{};
            _arrFighter[player]._pDef          = arrDef[player];
            _arrFighter[player]._fighterIDHash = arrDef[player]->_id.getHash();
            _arrFighter[player]._side          = player == 0 ? 1 : -1;
            _arrFighter[player]._inputBuffer.clear();
        }
        (void)_series.start( kPlayerCount ); // 참가자는 둘이라 실패하지 않는다
        startRound();
        return true;
    }

    void FightingMatch::startRound()
    {
        for ( int32 player = 0; player < kPlayerCount; ++player )
        {
            resetFighter( player );
        }
    }

    void FightingMatch::resetFighter( int32 player )
    {
        FighterRuntime& fighter = _arrFighter[player];
        fighter._timeline.cancel();
        const float32 halfDistance = _settings._startDistance * 0.5f;
        fighter._x                 = player == 0 ? -halfDistance : halfDistance;
        fighter._z                 = 0.0f;
        fighter._y                 = 0.0f;
        fighter._dirX              = player == 0 ? 1.0f : -1.0f;
        fighter._dirZ              = 0.0f;
        fighter._velocityY         = 0.0f;
        fighter._gravity           = 0.0f;
        fighter._carryX            = 0.0f;
        fighter._carryZ            = 0.0f;
        fighter._health            = fighter._pDef != nullptr ? fighter._pDef->_health : 1;
        fighter._stateFrames       = 0;
        fighter._hitstop           = 0;
        fighter._moveIndex         = -1;
        fighter._bufferedMove      = -1;
        fighter._bufferedAge       = 0;
        fighter._stanceIndex       = -1;
        fighter._stanceFrames      = 0;
        fighter._heatFrames        = 0;
        fighter._sidestepSign      = 0;
        fighter._breakButtons      = 0;
        fighter._state             = FighterState::Neutral;
        fighter._posture           = FighterPosture::Standing;
        fighter._guard             = GuardStance::Standing;
        fighter._bAirborne         = SW_FALSE;
        fighter._bRage             = SW_FALSE;
        fighter._bRageUsed         = SW_FALSE;
        fighter._bHeatUsed         = SW_FALSE;
        fighter._bThrowAttempted   = SW_FALSE;
        resetCombo( player );
    }

    void FightingMatch::advanceFrame( const vector<uint8>& listInput )
    {
        const InputFrame input0 = listInput.size() > 0 ? decodeInput( listInput[0] ) : InputFrame{};
        const InputFrame input1 = listInput.size() > 1 ? decodeInput( listInput[1] ) : InputFrame{};
        advanceFrame( input0, input1 );
    }

    void FightingMatch::advanceFrame( const InputFrame& input0, const InputFrame& input1 )
    {
        if ( _series.isRunning() == false )
            return;
        ++_frame;

        const InputFrame arrInput[kPlayerCount]   = { input0, input1 };
        uint16           arrPressed[kPlayerCount] = { 0, 0 };
        for ( int32 player = 0; player < kPlayerCount; ++player )
        {
            FighterRuntime& fighter  = _arrFighter[player];
            const uint16    previous = fighter._inputBuffer.getFrameCount() > 0 ? fighter._inputBuffer.getFrame( 0 )._buttons : 0;
            arrPressed[player]       = static_cast<uint16>( arrInput[player]._buttons & ~previous );
            fighter._inputBuffer.push( arrInput[player] );
        }

        if ( _series.getPhase() == RoundSeriesPhase::Intermission )
        {
            if ( _series.advanceTick() == RoundSeriesTick::RoundStarted )
                startRound();
            return;
        }

        for ( int32 player = 0; player < kPlayerCount; ++player )
        {
            updateFighter( player, arrPressed[player] );
        }
        for ( int32 player = 0; player < kPlayerCount; ++player )
        {
            updateControl( player, arrInput[player] );
        }
        resolveSpacing();
        resolveHits();
        updateRoundRules();
    }

    // ------------------------------------------------------------------------------
    // 상태 기계
    // ------------------------------------------------------------------------------
    void FightingMatch::enterState( int32 player, FighterState state, int32 frames )
    {
        FighterRuntime& fighter = _arrFighter[player];
        fighter._state          = state;
        fighter._stateFrames    = frames;
        if ( state != FighterState::Attacking )
        {
            fighter._timeline.cancel();
            fighter._moveIndex = -1;
        }
        if ( state != FighterState::Neutral && state != FighterState::Blockstun )
            fighter._guard = GuardStance::None;
    }

    void FightingMatch::resetCombo( int32 player )
    {
        FighterRuntime& fighter = _arrFighter[player];
        fighter._comboHits      = 0;
        fighter._airHits        = 0;
        fighter._bScrewUsed     = SW_FALSE;
        fighter._bBoundUsed     = SW_FALSE;
        fighter._bWallSplatUsed = SW_FALSE;
    }

    void FightingMatch::updateFighter( int32 player, uint16 pressedButtons )
    {
        FighterRuntime& fighter  = _arrFighter[player];
        const int32     opponent = 1 - player;
        if ( fighter._state == FighterState::Knockout )
            return;
        if ( fighter._hitstop > 0 )
        {
            --fighter._hitstop;
            return;
        }
        if ( fighter._heatFrames > 0 )
            --fighter._heatFrames;

        switch ( fighter._state )
        {
            case FighterState::Neutral:
            {
                if ( fighter._stanceIndex >= 0 )
                {
                    --fighter._stanceFrames;
                    if ( fighter._stanceFrames <= 0 )
                    {
                        fighter._stanceIndex = -1;
                        fighter._posture     = FighterPosture::Standing;
                    }
                }
                break;
            }
            case FighterState::Attacking:
            {
                updateAttack( player );
                break;
            }
            case FighterState::Sidestep:
            {
                updateSidestep( player );
                break;
            }
            case FighterState::Jump:
            {
                updateAirMotion( player );
                if ( fighter._bAirborne == SW_FALSE )
                    enterState( player, FighterState::Neutral, 0 );
                break;
            }
            case FighterState::Blockstun:
            case FighterState::Hitstun:
            {
                // 경직은 닿은 다음 프레임부터 센다. 0이 된 다음 프레임에 움직일 수 있다(이득 = 경직 − 남은 프레임).
                if ( fighter._stateFrames > 0 )
                {
                    --fighter._stateFrames;
                }
                else
                {
                    enterState( player, FighterState::Neutral, 0 );
                    resetCombo( player );
                }
                break;
            }
            case FighterState::Juggle:
            {
                updateJuggle( player );
                break;
            }
            case FighterState::WallSplat:
            {
                --fighter._stateFrames;
                if ( fighter._stateFrames <= 0 )
                    enterState( player, FighterState::Down, _settings._downFrames );
                break;
            }
            case FighterState::Down:
            {
                --fighter._stateFrames;
                if ( fighter._stateFrames <= 0 )
                    enterState( player, FighterState::Wakeup, _settings._wakeupFrames );
                break;
            }
            case FighterState::Wakeup:
            {
                --fighter._stateFrames;
                if ( fighter._stateFrames <= 0 )
                {
                    enterState( player, FighterState::Neutral, 0 );
                    fighter._guard = GuardStance::Standing;
                    resetCombo( player );
                }
                break;
            }
            case FighterState::Throwing:
            {
                if ( _arrFighter[opponent]._state != FighterState::ThrowBreak )
                {
                    --fighter._stateFrames;
                    if ( fighter._stateFrames <= 0 )
                        enterState( player, FighterState::Neutral, 0 );
                }
                break;
            }
            case FighterState::ThrowBreak:
            {
                updateThrowBreak( player, pressedButtons );
                break;
            }
            case FighterState::Knockout:
            {
                break;
            }
        }
    }

    void FightingMatch::updateAttack( int32 player )
    {
        FighterRuntime& fighter   = _arrFighter[player];
        const bool      bAdvanced = fighter._timeline.advanceFrame();
        if ( fighter._bAirborne == SW_TRUE && fighter._timeline.isInHitstop() == false )
            updateAirMotion( player );
        if ( fighter._timeline.isPlaying() == false )
        {
            finishMove( player );
            return;
        }
        const FighterMove* pMove                = fighter.findCurrentMove();
        const bool         bShouldAimAtOpponent = bAdvanced && pMove != nullptr && pMove->_bTracking;
        if ( bShouldAimAtOpponent )
            aimAtOpponent( player );
    }

    void FightingMatch::updateSidestep( int32 player )
    {
        // 상대를 축으로 돈다. 상대의 직선 기술은 시작 방향에 묶여 있어서 옆으로 벌어진 각도만큼 빗나간다.
        FighterRuntime&       fighter = _arrFighter[player];
        const FighterRuntime& other   = _arrFighter[1 - player];
        const float32         step    = fighter._pDef->_sidestepAngle / static_cast<float32>( fighter._pDef->_sidestepFrames );
        const float32         angle   = step * static_cast<float32>( fighter._sidestepSign );
        const float32         relX    = fighter._x - other._x;
        const float32         relZ    = fighter._z - other._z;
        const float32         cosA    = MathUtil::cos( angle );
        const float32         sinA    = MathUtil::sin( angle );
        fighter._x                    = other._x + relX * cosA - relZ * sinA;
        fighter._z                    = other._z + relX * sinA + relZ * cosA;
        --fighter._stateFrames;
        if ( fighter._stateFrames <= 0 )
            enterState( player, FighterState::Neutral, 0 );
    }

    void FightingMatch::updateJuggle( int32 player )
    {
        FighterRuntime& fighter = _arrFighter[player];
        updateAirMotion( player );
        const float32 limit   = _settings._stageHalfSize - fighter._pDef->_pushRadius;
        const bool    bAtWall = MathUtil::abs( fighter._x ) >= limit || MathUtil::abs( fighter._z ) >= limit;
        if ( fighter._bAirborne == SW_FALSE )
        {
            enterState( player, FighterState::Down, _settings._downFrames );
            return;
        }
        if ( bAtWall == false || fighter._bWallSplatUsed != SW_FALSE )
            return;
        // 저글로 벽까지 밀려왔으므로 벽꽝 상태로 만든다.
        fighter._bWallSplatUsed = SW_TRUE;
        fighter._bAirborne      = SW_FALSE;
        fighter._y              = 0.0f;
        fighter._velocityY      = 0.0f;
        fighter._carryX         = 0.0f;
        fighter._carryZ         = 0.0f;
        enterState( player, FighterState::WallSplat, _settings._wallSplatFrames );
        pushEvent( FightingEvent::Kind::WallSplat, 1 - player, 0 );
    }

    void FightingMatch::updateThrowBreak( int32 player, uint16 pressedButtons )
    {
        FighterRuntime& fighter  = _arrFighter[player];
        const int32     opponent = 1 - player;
        const uint16    pressed  = static_cast<uint16>( pressedButtons & FightingMatchInternal::kButtonMask );
        if ( pressed != 0 && fighter._bThrowAttempted == SW_FALSE )
        {
            fighter._bThrowAttempted = SW_TRUE; // 첫 시도만 인정한다. 틀린 버튼을 눌렀으면 더 풀 수 없다
            if ( fighter._breakButtons != 0 && pressed == fighter._breakButtons )
            {
                enterState( player, FighterState::Blockstun, _settings._throwBreakRecovery );
                fighter._guard = GuardStance::Standing;
                enterState( opponent, FighterState::Blockstun, _settings._throwBreakRecovery );
                _arrFighter[opponent]._guard = GuardStance::Standing;
                fighter._x += _arrFighter[opponent]._dirX * 0.3f;
                fighter._z += _arrFighter[opponent]._dirZ * 0.3f;
                clampToStage( player );
                pushEvent( FightingEvent::Kind::ThrowBroken, player, 0 );
                return;
            }
        }
        --fighter._stateFrames;
        if ( fighter._stateFrames <= 0 )
            landThrow( player );
    }

    void FightingMatch::updateAirMotion( int32 player )
    {
        FighterRuntime& fighter = _arrFighter[player];
        if ( fighter._bAirborne == SW_FALSE )
            return;
        fighter._x += fighter._carryX;
        fighter._z += fighter._carryZ;
        clampToStage( player );
        fighter._y += fighter._velocityY;
        fighter._velocityY -= fighter._gravity;
        if ( fighter._y <= 0.0f && fighter._velocityY < 0.0f )
        {
            fighter._y         = 0.0f;
            fighter._velocityY = 0.0f;
            fighter._carryX    = 0.0f;
            fighter._carryZ    = 0.0f;
            fighter._bAirborne = SW_FALSE;
            if ( fighter._posture == FighterPosture::Airborne )
                fighter._posture = FighterPosture::Standing;
        }
    }

    void FightingMatch::finishMove( int32 player )
    {
        FighterRuntime&    fighter     = _arrFighter[player];
        const FighterMove* pMove       = fighter.findCurrentMove();
        const int32        stanceIndex = pMove != nullptr && pMove->_enterStance.empty() == false ? fighter._pDef->findStanceIndex( pMove->_enterStance ) : -1;
        enterState( player, fighter._bAirborne == SW_TRUE ? FighterState::Jump : FighterState::Neutral, 0 );
        fighter._stanceIndex  = stanceIndex;
        fighter._stanceFrames = stanceIndex >= 0 ? _settings._stanceFrames : 0;
        if ( stanceIndex >= 0 )
            fighter._posture = FighterPosture::Stance;
    }

    // ------------------------------------------------------------------------------
    // 조작
    // ------------------------------------------------------------------------------
    bool FightingMatch::isUpcomingCancelTarget( int32 player, int32 moveIndex ) const
    {
        const FighterRuntime& fighter = _arrFighter[player];
        if ( fighter._state != FighterState::Attacking || fighter._timeline.isPlaying() == false )
            return false;
        const hashed_string& moveID = fighter._pDef->_listMove[static_cast<size_t>( moveIndex )]._frame._id;
        for ( const MoveCancelWindow& window : fighter._timeline.getMove()._listCancel )
        {
            if ( window._toFrame < fighter._timeline.getFrame() )
                continue;
            for ( const hashed_string& candidate : window._listMoveID )
            {
                if ( candidate == moveID )
                    return true;
            }
        }
        return false;
    }

    bool FightingMatch::isMoveAllowed( int32 player, int32 moveIndex, bool bAsIfFree ) const
    {
        const FighterRuntime& fighter = _arrFighter[player];
        const FighterRuntime& other   = _arrFighter[1 - player];
        const FighterMove&    move    = fighter._pDef->_listMove[static_cast<size_t>( moveIndex )];

        if ( fighter._state == FighterState::Attacking && bAsIfFree == false )
        {
            // 스트링과 캔슬은 지금 기술의 캔슬 창 안에서만 가능하다.
            if ( fighter._timeline.canCancelInto( move._frame._id ) == false )
                return false;
        }
        else
        {
            const bool bFree = bAsIfFree || fighter._state == FighterState::Neutral || fighter._state == FighterState::Sidestep ||
                               fighter._state == FighterState::Jump;
            if ( bFree == false || ( move._bStringOnly && isUpcomingCancelTarget( player, moveIndex ) == false ) )
                return false;
            const FighterPosture posture =
                fighter._bAirborne == SW_TRUE ? FighterPosture::Airborne : ( fighter._state == FighterState::Sidestep ? FighterPosture::Standing : fighter._posture );
            if ( move.allowsPosture( posture ) == false )
                return false;
            if ( posture == FighterPosture::Stance )
            {
                const int32 stanceIndex = fighter._pDef->findStanceIndex( move._stance );
                if ( stanceIndex < 0 || stanceIndex != fighter._stanceIndex )
                    return false;
            }
        }
        if ( move._bGroundHit && other._state != FighterState::Down )
            return false;
        if ( move._bNearWall && isNearWall( 1 - player ) == false )
            return false;
        if ( move._bRageArt && fighter._bRage == SW_FALSE )
            return false;
        if ( move._bHeatBurst && ( _settings._bHeatEnabled == SW_FALSE || fighter._bHeatUsed == SW_TRUE ) )
            return false;
        return true;
    }

    int32 FightingMatch::findCompletedMove( int32 player, bool bBufferCandidates ) const
    {
        const FighterRuntime& fighter   = _arrFighter[player];
        int32                 bestIndex = -1;
        for ( size_t index = 0; index < fighter._pDef->_listMove.size(); ++index )
        {
            const int32        moveIndex = static_cast<int32>( index );
            const FighterMove& move      = fighter._pDef->_listMove[index];
            const bool         bCandidate =
                isMoveAllowed( player, moveIndex, bBufferCandidates );
            if ( bCandidate == false || fighter._inputBuffer.isCompleted( move._command, fighter._side ) == false )
                continue;
            if ( bestIndex < 0 )
            {
                bestIndex = moveIndex;
                continue;
            }
            // 기반 `InputCommandBuffer::findCompleted` 와 같은 규칙이다. 우선도가 높은 것을 고르고, 같으면 단계가 많은 것을 고른다.
            const InputCommand& best = fighter._pDef->_listMove[static_cast<size_t>( bestIndex )]._command;
            if ( move._command._priority > best._priority ||
                 ( move._command._priority == best._priority && move._command._listStep.size() > best._listStep.size() ) )
                bestIndex = moveIndex;
        }
        return bestIndex;
    }

    void FightingMatch::startMove( int32 player, int32 moveIndex )
    {
        FighterRuntime&    fighter = _arrFighter[player];
        const FighterMove& move    = fighter._pDef->_listMove[static_cast<size_t>( moveIndex )];
        fighter._timeline.start( move._frame );
        fighter._moveIndex    = moveIndex;
        fighter._state        = FighterState::Attacking;
        fighter._stateFrames  = 0;
        fighter._guard        = GuardStance::None;
        fighter._bufferedMove = -1;
        fighter._bufferedAge  = 0;
        fighter._stanceIndex  = -1;
        fighter._stanceFrames = 0;
        if ( fighter._bAirborne == SW_FALSE )
            fighter._posture = move.allowsPosture( FighterPosture::Crouching ) && fighter._posture == FighterPosture::Crouching ? FighterPosture::Crouching
                                                                                                                                : FighterPosture::Standing;
        aimAtOpponent( player ); // 공격 방향은 여기서 고정한다(추적 기술만 다시 겨눈다)
        pushEvent( FightingEvent::Kind::MoveStarted, player, moveIndex, move._frame._id );
        if ( move._bRageArt )
        {
            fighter._bRage     = SW_FALSE;
            fighter._bRageUsed = SW_TRUE;
            pushEvent( FightingEvent::Kind::RageArt, player, 0, move._frame._id );
        }
        if ( move._bHeatBurst )
        {
            fighter._bHeatUsed  = SW_TRUE;
            fighter._heatFrames = _settings._heatFrames;
            pushEvent( FightingEvent::Kind::HeatActivated, player, _settings._heatFrames, move._frame._id );
        }
    }

    void FightingMatch::updateControl( int32 player, const InputFrame& input )
    {
        FighterRuntime& fighter = _arrFighter[player];
        if ( fighter._state == FighterState::Knockout || fighter._hitstop > 0 )
            return;

        // 1) 지금 낼 수 있는 기술을 실행한다. 없으면 움직일 수 없는 동안 입력한 커맨드를 선입력 버퍼에 담는다.
        int32 moveIndex = findCompletedMove( player, false );
        if ( moveIndex < 0 )
        {
            const int32 bufferIndex = findCompletedMove( player, true );
            if ( bufferIndex >= 0 )
            {
                fighter._bufferedMove = bufferIndex;
                fighter._bufferedAge  = 0;
            }
            else if ( fighter._bufferedMove >= 0 )
            {
                ++fighter._bufferedAge;
                if ( fighter._bufferedAge > _settings._inputBufferFrames )
                    fighter._bufferedMove = -1;
                else if ( isMoveAllowed( player, fighter._bufferedMove ) )
                    moveIndex = fighter._bufferedMove;
            }
        }
        if ( moveIndex >= 0 )
        {
            startMove( player, moveIndex );
            return;
        }
        if ( fighter._state != FighterState::Neutral )
            return;

        // 2) 횡이동과 점프를 처리한다.
        const uint8 relative = InputCommandBuffer::mirrorDirection( input._direction, fighter._side );
        const bool  bFloor   = fighter._posture != FighterPosture::Airborne;
        if ( bFloor && fighter._inputBuffer.isCompleted( fighter._pDef->_sidestepUp, fighter._side ) )
        {
            enterState( player, FighterState::Sidestep, fighter._pDef->_sidestepFrames );
            fighter._sidestepSign = 1;
            fighter._stanceIndex  = -1;
            fighter._posture      = FighterPosture::Standing;
            pushEvent( FightingEvent::Kind::Sidestep, player, 1 );
            return;
        }
        if ( bFloor && fighter._inputBuffer.isCompleted( fighter._pDef->_sidestepDown, fighter._side ) )
        {
            enterState( player, FighterState::Sidestep, fighter._pDef->_sidestepFrames );
            fighter._sidestepSign = -1;
            fighter._stanceIndex  = -1;
            fighter._posture      = FighterPosture::Standing;
            pushEvent( FightingEvent::Kind::Sidestep, player, -1 );
            return;
        }
        if ( bFloor && fighter._inputBuffer.isCompleted( fighter._pDef->_jump, fighter._side ) )
        {
            const float32 frames = static_cast<float32>( fighter._pDef->_jumpFrames );
            enterState( player, FighterState::Jump, 0 );
            fighter._bAirborne   = SW_TRUE;
            fighter._posture     = FighterPosture::Airborne;
            fighter._stanceIndex = -1;
            fighter._velocityY   = 4.0f * fighter._pDef->_jumpHeight / frames;
            fighter._gravity     = 8.0f * fighter._pDef->_jumpHeight / ( frames * frames );
            fighter._carryX      = fighter._dirX * fighter._pDef->_jumpDistance / frames;
            fighter._carryZ      = fighter._dirZ * fighter._pDef->_jumpDistance / frames;
            return;
        }

        // 3) 자세, 가드, 걷기를 처리한다.
        if ( FightingMatchInternal::isCrouchDirection( relative ) )
        {
            fighter._posture     = FighterPosture::Crouching;
            fighter._stanceIndex = -1;
            fighter._guard       = relative == 3 ? GuardStance::None : GuardStance::Crouching;
        }
        else
        {
            if ( fighter._stanceIndex < 0 )
                fighter._posture = FighterPosture::Standing;
            const bool bForward = relative == 6 || relative == 9;
            fighter._guard      = bForward || fighter._stanceIndex >= 0 ? GuardStance::None : GuardStance::Standing;
            if ( bForward )
            {
                fighter._x += fighter._dirX * fighter._pDef->_walkSpeed;
                fighter._z += fighter._dirZ * fighter._pDef->_walkSpeed;
            }
            else if ( FightingMatchInternal::isGuardDirectionBack( relative ) )
            {
                fighter._x -= fighter._dirX * fighter._pDef->_walkSpeed;
                fighter._z -= fighter._dirZ * fighter._pDef->_walkSpeed;
            }
        }
    }

    // ------------------------------------------------------------------------------
    // 공간
    // ------------------------------------------------------------------------------
    void FightingMatch::aimAtOpponent( int32 player )
    {
        FighterRuntime&       fighter = _arrFighter[player];
        const FighterRuntime& other   = _arrFighter[1 - player];
        const float32         deltaX  = other._x - fighter._x;
        const float32         deltaZ  = other._z - fighter._z;
        const float32         length  = MathUtil::sqrt( deltaX * deltaX + deltaZ * deltaZ );
        if ( length < 1.0e-5f )
            return;
        fighter._dirX = deltaX / length;
        fighter._dirZ = deltaZ / length;
    }

    void FightingMatch::clampToStage( int32 player )
    {
        FighterRuntime& fighter = _arrFighter[player];
        const float32   radius  = fighter._pDef != nullptr ? fighter._pDef->_pushRadius : 0.0f;
        const float32   limit   = MathUtil::max( 0.0f, _settings._stageHalfSize - radius );
        fighter._x              = MathUtil::clamp( fighter._x, -limit, limit );
        fighter._z              = MathUtil::clamp( fighter._z, -limit, limit );
    }

    void FightingMatch::resolveSpacing()
    {
        FighterRuntime& first   = _arrFighter[0];
        FighterRuntime& second  = _arrFighter[1];
        const float32   minimum = first._pDef->_pushRadius + second._pDef->_pushRadius;
        float32         deltaX  = second._x - first._x;
        float32         deltaZ  = second._z - first._z;
        float32         length  = MathUtil::sqrt( deltaX * deltaX + deltaZ * deltaZ );
        if ( length < minimum )
        {
            if ( length < 1.0e-5f )
            {
                deltaX = 1.0f;
                deltaZ = 0.0f;
                length = 1.0f;
            }
            const float32 push = ( minimum - length ) * 0.5f;
            first._x -= deltaX / length * push;
            first._z -= deltaZ / length * push;
            second._x += deltaX / length * push;
            second._z += deltaZ / length * push;
        }
        for ( int32 player = 0; player < kPlayerCount; ++player )
        {
            clampToStage( player );
            const FighterState state              = _arrFighter[player]._state;
            const bool         bCanTurnToOpponent = state != FighterState::Attacking && state != FighterState::Juggle && state != FighterState::Throwing;
            if ( bCanTurnToOpponent )
                aimAtOpponent( player );
        }
    }

    float32 FightingMatch::computeDistance() const
    {
        const float32 deltaX = _arrFighter[1]._x - _arrFighter[0]._x;
        const float32 deltaZ = _arrFighter[1]._z - _arrFighter[0]._z;
        return MathUtil::sqrt( deltaX * deltaX + deltaZ * deltaZ );
    }

    bool FightingMatch::isNearWall( int32 player ) const
    {
        const FighterRuntime& fighter = _arrFighter[player];
        if ( fighter._pDef == nullptr )
            return false;
        const float32 limit = _settings._stageHalfSize - fighter._pDef->_pushRadius;
        const float32 reach = MathUtil::max( MathUtil::abs( fighter._x ), MathUtil::abs( fighter._z ) );
        return limit - reach <= _settings._wallNearDistance;
    }

    void FightingMatch::setFighterPosition( int32 player, float32 x, float32 z )
    {
        if ( player < 0 || player >= kPlayerCount )
            return;
        _arrFighter[player]._x = x;
        _arrFighter[player]._z = z;
        clampToStage( player );
        aimAtOpponent( 0 );
        aimAtOpponent( 1 );
    }

    void FightingMatch::setFighterHealth( int32 player, int32 health )
    {
        if ( player < 0 || player >= kPlayerCount )
            return;
        _arrFighter[player]._health = health;
    }

    // ------------------------------------------------------------------------------
    // 판정
    // ------------------------------------------------------------------------------
    bool FightingMatch::testHit( int32 attacker )
    {
        const FighterRuntime& fighter    = _arrFighter[attacker];
        const FighterRuntime& other      = _arrFighter[1 - attacker];
        const bool            bCannotHit = fighter._state != FighterState::Attacking || fighter._timeline.isInHitstop() || fighter._timeline.hasContact() ||
                                fighter._timeline.getPhase() != MovePhase::Active;
        if ( bCannotHit )
            return false;
        const FighterMove* pMove = fighter.findCurrentMove();
        if ( pMove == nullptr )
            return false;
        switch ( other._state )
        {
            case FighterState::Wakeup:
            case FighterState::Throwing:
            case FighterState::ThrowBreak:
            case FighterState::Knockout:
            {
                return false; // 무적
            }
            case FighterState::Down:
            {
                if ( pMove->_bGroundHit == false )
                    return false;
                break;
            }
            default:
            {
                break;
            }
        }
        if ( pMove->_frame._height == AttackHeight::Throw && ( other._bAirborne == SW_TRUE || other._state == FighterState::Juggle ) )
            return false; // 공중에 있는 상대는 잡을 수 없다

        // 허트 캡슐은 바닥 원(반지름)과 높이 구간으로 만든다. 높이는 자세와 상태로 정한다.
        float32 bodyHeight = other._pDef->_standHeight;
        if ( other._state == FighterState::Juggle )
            bodyHeight = _settings._juggleBodyHeight;
        else if ( other._state == FighterState::Down )
            bodyHeight = _settings._downBodyHeight;
        else if ( other._posture == FighterPosture::Crouching )
            bodyHeight = other._pDef->_crouchHeight;
        const float32 bodyBottom = other._y;
        const float32 bodyTop    = other._y + bodyHeight;
        const float32 radius     = other._pDef->_hurtRadius;

        // 공격 방향 기준으로 앞 거리와 옆 거리를 구한다. 옆으로 벌어진 각이 크면 옆 거리가 반지름을 넘는다.
        const float32 relX    = other._x - fighter._x;
        const float32 relZ    = other._z - fighter._z;
        const float32 forward = relX * fighter._dirX + relZ * fighter._dirZ;
        const float32 lateral = MathUtil::abs( relX * fighter._dirZ - relZ * fighter._dirX );

        (void)fighter._timeline.collectActiveHitboxes( _listHitboxScratch ); // 모은 수는 목록 크기로 본다
        for ( const MoveHitbox& hitbox : _listHitboxScratch )
        {
            const float32 halfWidth       = hitbox._width * 0.5f;
            const float32 halfHeight      = hitbox._height * 0.5f;
            const float32 thickness       = hitbox._shape == HitboxShape::Capsule ? MathUtil::min( hitbox._width, hitbox._height ) * 0.5f : 0.0f;
            const float32 boxBottom       = fighter._y + hitbox._y - halfHeight;
            const float32 boxTop          = fighter._y + hitbox._y + halfHeight;
            const bool    bReach          = hitbox._x - halfWidth <= forward + radius && forward - radius <= hitbox._x + halfWidth;
            const bool    bSide           = lateral <= radius + thickness;
            const bool    bHeightBand     = boxBottom <= bodyTop && bodyBottom <= boxTop;
            const bool    bHitboxOverlaps = bReach && bSide && bHeightBand;
            if ( bHitboxOverlaps )
                return true;
        }
        return false;
    }

    int32 FightingMatch::computeDamage( int32 attacker, float32 baseDamage, bool bComboScaled ) const
    {
        if ( baseDamage <= 0.0f )
            return 0;
        const FighterRuntime& fighter  = _arrFighter[attacker];
        const FighterRuntime& defender = _arrFighter[1 - attacker];
        float32               scale    = 1.0f;
        if ( bComboScaled )
            scale = MathUtil::max( _settings._juggleMinScale, 1.0f - _settings._juggleDecayPerHit * static_cast<float32>( defender._airHits ) );
        if ( fighter._bRage == SW_TRUE )
            scale *= _settings._rageDamageScale;
        if ( fighter._heatFrames > 0 )
            scale *= _settings._heatDamageScale;
        return MathUtil::max( 1, static_cast<int32>( baseDamage * scale + 0.5f + FightingMatchInternal::kRoundingSlack ) );
    }

    void FightingMatch::applyDamage( int32 defender, int32 damage )
    {
        FighterRuntime& fighter = _arrFighter[defender];
        fighter._health         = MathUtil::max( 0, fighter._health - damage );
    }

    void FightingMatch::resolveHits()
    {
        // 두 쪽을 먼저 모두 판정한 뒤 함께 적용한다. 같은 프레임에 닿으면 상쇄되어 둘 다 맞는다.
        bool arrContact[kPlayerCount] = { false, false };
        for ( int32 player = 0; player < kPlayerCount; ++player )
        {
            arrContact[player] = testHit( player );
        }
        for ( int32 player = 0; player < kPlayerCount; ++player )
        {
            if ( arrContact[player] )
                applyHit( player );
        }
    }

    void FightingMatch::applyHit( int32 attacker )
    {
        FighterRuntime&    fighter  = _arrFighter[attacker];
        FighterRuntime&    other    = _arrFighter[1 - attacker];
        const int32        defender = 1 - attacker;
        const FighterMove* pMove    = fighter.findCurrentMove();
        if ( pMove == nullptr )
            return;
        const FighterMove&   move      = *pMove;
        const MoveFrameData& frameData = move._frame;
        const bool           bInCombo =
            other._state == FighterState::Juggle || other._state == FighterState::WallSplat || other._state == FighterState::Down;
        const bool         bCanGuard = other._state == FighterState::Neutral || other._state == FighterState::Blockstun;
        const GuardStance  stance    = bCanGuard ? other._guard : GuardStance::None;
        const GuardOutcome outcome =
            bInCombo ? GuardOutcome::Hit : MoveTimeline::computeGuardOutcome( frameData._height, stance, frameData._bUnblockable == SW_TRUE );
        if ( outcome == GuardOutcome::Evaded )
            return; // 높이가 맞지 않아 헛쳤다(앉은 상대에게 상단 기술이나 잡기). 지속 프레임 동안 다시 판정한다

        const float32 pushX = fighter._dirX * move._pushback;
        const float32 pushZ = fighter._dirZ * move._pushback;
        if ( outcome == GuardOutcome::Blocked )
        {
            const int32 chip = computeDamage( attacker, frameData._chipDamage, false );
            applyDamage( defender, chip );
            enterState( defender, FighterState::Blockstun, frameData._blockstun );
            other._guard   = stance;
            other._hitstop = frameData._hitstop;
            other._x += pushX;
            other._z += pushZ;
            clampToStage( defender );
            resetCombo( defender );
            fighter._timeline.registerContact( true );
            pushEvent( FightingEvent::Kind::Blocked, attacker, chip, frameData._id );
            return;
        }

        if ( frameData._height == AttackHeight::Throw )
        {
            grabOpponent( attacker, move );
            return;
        }

        const int32 damage = computeDamage( attacker, frameData._damage, bInCombo );
        applyDamage( defender, damage );
        fighter._timeline.registerContact( false );
        other._hitstop      = frameData._hitstop;
        other._bufferedMove = -1;
        ++other._comboHits;

        const bool bWallSplat = frameData._bWallSplat == SW_TRUE && isNearWall( defender ) && other._bWallSplatUsed == SW_FALSE;
        if ( other._state == FighterState::Juggle )
        {
            hitAirborneOpponent( attacker, move );
        }
        else if ( other._state == FighterState::WallSplat || other._state == FighterState::Down )
        {
            ++other._airHits; // 벽꽝이나 다운 상태의 추가타도 콤보 감쇠를 이어 간다
        }
        else if ( frameData._bLauncher == SW_TRUE || other._bAirborne == SW_TRUE )
        {
            // 띄우기, 또는 점프 중에 맞아 떨어지는 경우다.
            const bool bLaunch = frameData._bLauncher == SW_TRUE;
            enterState( defender, FighterState::Juggle, 0 );
            other._bAirborne = SW_TRUE;
            other._posture   = FighterPosture::Standing;
            other._velocityY = bLaunch ? _settings._launchVelocity : 0.0f;
            other._gravity   = _settings._gravity;
            other._carryX    = fighter._dirX * _settings._juggleCarrySpeed;
            other._carryZ    = fighter._dirZ * _settings._juggleCarrySpeed;
            other._airHits   = 1;
        }
        else if ( bWallSplat )
        {
            other._bWallSplatUsed = SW_TRUE;
            enterState( defender, FighterState::WallSplat, _settings._wallSplatFrames );
            other._airHits = 1;
            pushEvent( FightingEvent::Kind::WallSplat, attacker, 0, frameData._id );
        }
        else if ( frameData._bKnockdown == SW_TRUE )
        {
            enterState( defender, FighterState::Down, _settings._downFrames );
        }
        else
        {
            enterState( defender, FighterState::Hitstun, frameData._hitstun );
            other._x += pushX;
            other._z += pushZ;
            clampToStage( defender );
        }
        pushEvent( FightingEvent::Kind::Hit, attacker, damage, frameData._id );
    }

    void FightingMatch::grabOpponent( int32 attacker, const FighterMove& move )
    {
        // 잡기에 성공했다. 풀기 입력 창을 열고, 피해는 창이 닫힐 때 준다.
        FighterRuntime&      fighter   = _arrFighter[attacker];
        const int32          defender  = 1 - attacker;
        FighterRuntime&      other     = _arrFighter[defender];
        const MoveFrameData& frameData = move._frame;
        enterState( defender, FighterState::ThrowBreak, _settings._throwBreakFrames );
        other._breakButtons    = move._breakButtons;
        other._bThrowAttempted = SW_FALSE;
        other._bufferedMove    = -1;
        fighter._timeline.registerContact( false );
        fighter._state       = FighterState::Throwing;
        fighter._stateFrames = MathUtil::max( 1, frameData._recovery );
        fighter._guard       = GuardStance::None;
        pushEvent( FightingEvent::Kind::ThrowGrab, attacker, 0, frameData._id );
    }

    void FightingMatch::hitAirborneOpponent( int32 attacker, const FighterMove& move )
    {
        const FighterRuntime& fighter   = _arrFighter[attacker];
        FighterRuntime&       other     = _arrFighter[1 - attacker];
        const MoveFrameData&  frameData = move._frame;
        if ( move._bScrew && other._bScrewUsed == SW_FALSE )
        {
            // 스크류는 상대를 다시 띄워 멀리 밀어낸다. 한 콤보에 한 번만 쓸 수 있다.
            other._bScrewUsed = SW_TRUE;
            other._velocityY  = _settings._screwVelocity;
            other._carryX     = fighter._dirX * _settings._screwCarrySpeed;
            other._carryZ     = fighter._dirZ * _settings._screwCarrySpeed;
            pushEvent( FightingEvent::Kind::Screw, attacker, 0, frameData._id );
        }
        else if ( move._bBound && other._bBoundUsed == SW_FALSE )
        {
            other._bBoundUsed = SW_TRUE;
            other._velocityY  = _settings._boundVelocity;
            pushEvent( FightingEvent::Kind::Bound, attacker, 0, frameData._id );
        }
        else
        {
            const float32 lift = MathUtil::max( _settings._juggleMinScale, 1.0f - _settings._juggleDecayPerHit * static_cast<float32>( other._airHits ) );
            other._velocityY   = _settings._juggleHitVelocity * lift;
            other._carryX      = fighter._dirX * _settings._juggleCarrySpeed;
            other._carryZ      = fighter._dirZ * _settings._juggleCarrySpeed;
        }
        ++other._airHits;
    }

    void FightingMatch::landThrow( int32 defender )
    {
        const int32     attacker = 1 - defender;
        FighterRuntime& fighter  = _arrFighter[attacker];
        // 잡은 쪽은 `Throwing` 상태에서 기술 인덱스를 버렸으므로, 피해는 잡힐 때 기억해 둔 기술(잡은 쪽 타임라인의 기술)에서 가져온다.
        const MoveFrameData& frameData = fighter._timeline.getMove();
        const int32          damage    = computeDamage( attacker, frameData._damage, false );
        applyDamage( defender, damage );
        enterState( defender, FighterState::Down, _settings._downFrames );
        pushEvent( FightingEvent::Kind::ThrowLanded, attacker, damage, frameData._id );
    }

    // ------------------------------------------------------------------------------
    // 라운드
    // ------------------------------------------------------------------------------
    void FightingMatch::updateRoundRules()
    {
        const bool bKo0 = _arrFighter[0]._health <= 0;
        const bool bKo1 = _arrFighter[1]._health <= 0;
        if ( bKo0 || bKo1 )
        {
            if ( bKo0 )
                enterState( 0, FighterState::Knockout, 0 );
            if ( bKo1 )
                enterState( 1, FighterState::Knockout, 0 );
            endRound( bKo0 && bKo1 ? kDraw : ( bKo0 ? 1 : 0 ), FightingEvent::Kind::Knockout );
            return;
        }

        for ( int32 player = 0; player < kPlayerCount; ++player )
        {
            FighterRuntime& fighter          = _arrFighter[player];
            const float32   ratio            = static_cast<float32>( fighter._health ) / static_cast<float32>( MathUtil::max( 1, fighter._pDef->_health ) );
            const bool      bShouldEnterRage = fighter._bRage == SW_FALSE && fighter._bRageUsed == SW_FALSE && ratio <= _settings._rageThreshold;
            if ( bShouldEnterRage )
            {
                fighter._bRage = SW_TRUE;
                pushEvent( FightingEvent::Kind::RageEntered, player, fighter._health );
            }
        }

        if ( _series.advanceTick() != RoundSeriesTick::TimeUp )
            return;
        // 시간이 초과되면 체력 비율이 높은 쪽이 이긴다(정수 교차 곱으로 정확히 비교한다).
        const int64 left  = static_cast<int64>( _arrFighter[0]._health ) * MathUtil::max( 1, _arrFighter[1]._pDef->_health );
        const int64 right = static_cast<int64>( _arrFighter[1]._health ) * MathUtil::max( 1, _arrFighter[0]._pDef->_health );
        endRound( left > right ? 0 : ( left < right ? 1 : kDraw ), FightingEvent::Kind::TimeOut );
    }

    void FightingMatch::endRound( int32 winner, FightingEvent::Kind reason )
    {
        pushEvent( reason, winner, 0 );
        _lastRoundWinner = winner;
        // 무승부 라운드는 둘 다 1위라서 둘 다 1승이다. 라운드 중에만 호출되므로 거절되지 않는다.
        const RoundSeriesOutcome outcome = _series.reportRoundWinner( winner == kDraw ? RoundSeries::kNoWinner : winner );
        pushEvent( FightingEvent::Kind::RoundEnd, winner, winner );
        if ( outcome != RoundSeriesOutcome::Finished )
            return;
        const int32 matchWinner = getMatchWinner();
        pushEvent( FightingEvent::Kind::MatchEnd, matchWinner, matchWinner );
    }

    int32 FightingMatch::getRound() const
    {
        return _series.getPhase() == RoundSeriesPhase::Waiting ? 0 : _series.getRoundIndex() + 1;
    }

    int32 FightingMatch::getMatchWinner() const
    {
        if ( _series.isFinished() == false )
            return -1;
        return _series.getWinner() == RoundSeries::kNoWinner ? kDraw : _series.getWinner();
    }

    void FightingMatch::pushEvent( FightingEvent::Kind kind, int32 player, int32 value, const hashed_string& moveID )
    {
        FightingEvent event;
        event._kind   = kind;
        event._player = player;
        event._value  = value;
        event._moveID = moveID;
        _eventBuffer.push( event );
    }

    void FightingMatch::drainEvents( vector<FightingEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }

    // ------------------------------------------------------------------------------
    // 롤백 — 입력 1 바이트 · 상태 바이트
    // ------------------------------------------------------------------------------
    uint8 FightingMatch::encodeInput( const InputFrame& frame )
    {
        const uint8 direction = 1 <= frame._direction && frame._direction <= 9 ? frame._direction : 5;
        return static_cast<uint8>( ( frame._buttons & FightingMatchInternal::kButtonMask ) | ( direction << 4 ) );
    }

    InputFrame FightingMatch::decodeInput( uint8 byte )
    {
        InputFrame frame;
        frame._buttons        = static_cast<uint16>( byte & FightingMatchInternal::kButtonMask );
        const uint8 direction = static_cast<uint8>( byte >> 4 );
        frame._direction      = 1 <= direction && direction <= 9 ? direction : 5;
        return frame;
    }

    void FightingMatch::saveState( vector<uint8>& outBuffer ) const
    {
        // 롤백은 프레임마다, 되감아 다시 돌 때마다 상태를 저장한다. 링 슬롯의 버퍼를 이어받아 쓰고 돌려주므로 새로 할당하지 않는다.
        BitWriter  writer{ std::move( outBuffer ) };
        const bool bWritten = FixedLayout::write( *this, writer );
        outBuffer           = writer.releaseBytes();
        if ( bWritten == false )
        {
            // 범위 밖 값은 시뮬레이션의 결함이다. 잘라 쓰지 않고, 읽는 쪽이 거절하도록 빈 바이트를 남긴다.
            SW_LOG_ERROR( "Fighting rollback state has a value outside its declared range at frame %# - the state is not saved", _frame );
            SW_ASSERT( bWritten );
            outBuffer.clear();
        }
    }

    bool FightingMatch::loadState( const vector<uint8>& buffer )
    {
        const FixedReadResult result = FixedLayout::read( buffer.data(), static_cast<int32>( buffer.size() ), *this );
        if ( result != FixedReadResult::Ok )
        {
            SW_LOG_WARNING( "Fighting rollback state was rejected (%#) - the match keeps its current state", FixedLayout::toString( result ) );
            return false;
        }
        completeLoad();
        return true;
    }

    void FightingMatch::writeState( Archive& outArchive ) const
    {
        // 실패하면 아카이브가 오류 상태가 된다. 세이브를 마무리하는 쪽이 아카이브 상태로 실패를 본다.
        (void)BinarySerializer::serializeVersioned( kStateVersion, this, *StaticType(), outArchive );
    }

    bool FightingMatch::readState( Archive& archive )
    {
        // 세이브에 없는 필드(정의 포인터, 설정)는 이 복사본에서 이어지고, 끝까지 맞을 때만 옮긴다.
        FightingMatch loaded  = *this;
        uint32        version = 0;
        // 선언된 `Min` · `Max` 는 읽기 공통 끝이 두 캐릭터 배열 · 고리 버퍼 원소까지 본다(`SerializerUtil::isWithinDeclaredRanges`).
        if ( BinarySerializer::deserializeVersioned( version, &loaded, *StaticType(), archive, kStateVersion ) == false )
            return false;
        // 롤백 읽기와 같은 검증(이 타입과 값으로 든 고리 버퍼 · 라운드 시리즈의 `Validate`)을 돌린다.
        ValidationContext context;
        (void)ReflectionValidation::validateObject( *StaticType(), &loaded, context ); // 결과는 context 의 오류로 본다
        if ( context.hasError() )
            return false;
        loaded.completeLoad();
        *this = std::move( loaded );
        return true;
    }

    void FightingMatch::validateState( ValidationContext& context ) const
    {
        for ( const FighterRuntime& fighter : _arrFighter )
        {
            if ( fighter._pDef == nullptr )
            {
                context.addError( "fighter has no definition - initialize the match before loading a state" );
                continue;
            }
            if ( fighter._fighterIDHash != fighter._pDef->_id.getHash() )
                context.addError( "state belongs to another fighter" );
            const int32 moveCount   = static_cast<int32>( fighter._pDef->_listMove.size() );
            const int32 stanceCount = static_cast<int32>( fighter._pDef->_listStance.size() );
            const bool  bBadIndex   = fighter._moveIndex >= moveCount || fighter._bufferedMove >= moveCount || fighter._stanceIndex >= stanceCount;
            if ( bBadIndex )
                context.addError( "move or stance index is outside the fighter definition" );
            FighterRuntime relinked = fighter; // 붙여 볼 수 있는지만 본다
            if ( FightingMatchInternal::relinkTimeline( relinked ) == false )
                context.addError( "playing timeline does not fit the current move" );
        }
        if ( _series.getParticipantCount() != kPlayerCount )
            context.addError( "round series does not have two participants" );
    }

    void FightingMatch::completeLoad()
    {
        for ( FighterRuntime& fighter : _arrFighter )
        {
            const bool bRelinked = FightingMatchInternal::relinkTimeline( fighter );
            SW_ASSERT( bRelinked ); // validateState 가 같은 함수로 미리 봤다
            (void)bRelinked;
        }
        _eventBuffer.clear();
    }
} // namespace sw
