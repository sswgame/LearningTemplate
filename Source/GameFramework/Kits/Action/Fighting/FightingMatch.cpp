#include "pch.h"

#include "GameFramework/Kits/Action/Fighting/FightingMatch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"

namespace sw
{
    namespace
    {
        struct FightingMatchInternal
        {
            static constexpr uint32  kStateMagic    = 0x54484746u; ///< "FGHT"
            static constexpr int32   kStateVersion  = 2;
            static constexpr uint16  kButtonMask    = 0x0F; ///< 1 바이트 입력에 싣는 버튼 4 개
            static constexpr int32   kMaxCounter    = 1 << 20;
            static constexpr float32 kRoundingSlack = 1.0e-3f; ///< 0.55 × 10 같은 반올림 경계가 부동소수 오차로 내려가지 않게
            static constexpr int32   kStateCount    = static_cast<int32>( FighterState::Knockout ) + 1;

            static bool isGuardDirectionBack( uint8 relativeDirection ) { return relativeDirection == 4 || relativeDirection == 7; }

            static bool isCrouchDirection( uint8 relativeDirection ) { return 1 <= relativeDirection && relativeDirection <= 3; }

            static void writeCounter( BitWriter& writer, int32 value ) { writer.writeVarInt( value ); }

            static int32 readCounter( BitReader& reader )
            {
                const int64 value = reader.readVarInt();
                return static_cast<int32>( MathUtil::clamp<int64>( value, -kMaxCounter, kMaxCounter ) );
            }

            /** @brief 타임라인을 쓰고(기술은 캐릭터의 기술 자리로 따로) 읽습니다. */
            static void writeTimeline( BitWriter& writer, const MoveTimeline& timeline )
            {
                writer.writeBool( timeline.isPlaying() );
                writer.writeBool( timeline.hasContact() );
                writer.writeBool( timeline.wasBlocked() );
                writeCounter( writer, timeline.getFrame() );
                writeCounter( writer, timeline.getHitstopRemaining() );
            }

            /** @brief 타임라인을 되살립니다(기반 `MoveTimeline::restoreState` — 기술은 쓰는 쪽이 찾아 넘긴다). */
            [[nodiscard]] static bool readTimeline( BitReader& reader, const FighterMove* pMove, MoveTimeline& outTimeline )
            {
                const bool  bPlaying = reader.readBool();
                const bool  bContact = reader.readBool();
                const bool  bBlocked = reader.readBool();
                const int32 frame    = readCounter( reader );
                const int32 hitstop  = readCounter( reader );
                outTimeline.cancel();
                if ( bPlaying == false )
                    return true;
                return pMove != nullptr && outTimeline.restoreState( pMove->_frame, frame, hitstop, bContact, bBlocked );
            }

            /** @brief 격투의 라운드 규칙 — 라운드 승 = 1 위 1 점(순위 점수 목록 없음), 둘이 함께 선승에 닿으면 무승부, 시간 · 대기는 최소 1 프레임입니다. */
            static RoundSeriesSettings makeSeriesSettings( const FightingSettings& settings )
            {
                RoundSeriesSettings series;
                series._winScore          = settings._roundsToWin;
                series._roundTicks        = MathUtil::max( 1, settings._roundFrames );
                series._intermissionTicks = MathUtil::max( 1, settings._roundOverFrames );
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
        if ( _pDef == nullptr || _moveIndex < 0 || _moveIndex >= static_cast<int32>( _pDef->_listMove.size() ) )
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

    void FightingMatch::initialize( const FighterDef& fighter0, const FighterDef& fighter1, const FightingSettings& settings )
    {
        _settings = settings;
        _eventBuffer.clear();
        _frame                                 = 0;
        _lastRoundWinner                       = -1;
        const FighterDef* arrDef[kPlayerCount] = { &fighter0, &fighter1 };
        for ( int32 player = 0; player < kPlayerCount; ++player )
        {
            _arrFighter[player]       = FighterRuntime{};
            _arrFighter[player]._pDef = arrDef[player];
            _arrFighter[player]._side = player == 0 ? 1 : -1;
            _arrFighter[player]._inputBuffer.clear();
        }
        _series.initialize( FightingMatchInternal::makeSeriesSettings( settings ) );
        (void)_series.start( kPlayerCount ); // 참가자 둘 — 실패하지 않는다
        startRound();
    }

    void FightingMatch::startRound()
    {
        for ( int32 player = 0; player < kPlayerCount; ++player )
            resetFighter( player );
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
            updateFighter( player, arrPressed[player] );
        for ( int32 player = 0; player < kPlayerCount; ++player )
            updateControl( player, arrInput[player] );
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
                const bool bAdvanced = fighter._timeline.advanceFrame();
                if ( fighter._bAirborne == SW_TRUE && fighter._timeline.isInHitstop() == false )
                    updateAirMotion( player );
                if ( fighter._timeline.isPlaying() == false )
                {
                    finishMove( player );
                }
                else
                {
                    const FighterMove* pMove = fighter.findCurrentMove();
                    if ( bAdvanced && pMove != nullptr && pMove->_bTracking == SW_TRUE )
                        aimAtOpponent( player );
                }
                break;
            }
            case FighterState::Sidestep:
            {
                // 상대를 축으로 돈다 — 상대의 직선 기술은 시작 방향에 묶여 있어 옆으로 벌어진 각만큼 빗나간다.
                const FighterRuntime& other = _arrFighter[opponent];
                const float32         step  = MathUtil::toRadian( fighter._pDef->_sidestepAngle ) / static_cast<float32>( fighter._pDef->_sidestepFrames );
                const float32         angle = step * static_cast<float32>( fighter._sidestepSign );
                const float32         relX  = fighter._x - other._x;
                const float32         relZ  = fighter._z - other._z;
                const float32         cosA  = MathUtil::cos( angle );
                const float32         sinA  = MathUtil::sin( angle );
                fighter._x                  = other._x + relX * cosA - relZ * sinA;
                fighter._z                  = other._z + relX * sinA + relZ * cosA;
                --fighter._stateFrames;
                if ( fighter._stateFrames <= 0 )
                    enterState( player, FighterState::Neutral, 0 );
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
                // 경직은 닿은 다음 프레임부터 센다 — 0 이 된 다음 프레임에 움직인다(이득 = 경직 − 남은 프레임).
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
                updateAirMotion( player );
                const float32 limit   = _settings._stageHalfSize - fighter._pDef->_pushRadius;
                const bool    bAtWall = MathUtil::abs( fighter._x ) >= limit || MathUtil::abs( fighter._z ) >= limit;
                if ( fighter._bAirborne == SW_FALSE )
                {
                    enterState( player, FighterState::Down, _settings._downFrames );
                }
                else if ( bAtWall && fighter._bWallSplatUsed == SW_FALSE )
                {
                    // 저글로 벽까지 날라 왔다 — 벽꽝.
                    fighter._bWallSplatUsed = SW_TRUE;
                    fighter._bAirborne      = SW_FALSE;
                    fighter._y              = 0.0f;
                    fighter._velocityY      = 0.0f;
                    fighter._carryX         = 0.0f;
                    fighter._carryZ         = 0.0f;
                    enterState( player, FighterState::WallSplat, _settings._wallSplatFrames );
                    pushEvent( FightingEvent::Kind::WallSplat, opponent, 0 );
                }
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
                const uint16 pressed = static_cast<uint16>( pressedButtons & FightingMatchInternal::kButtonMask );
                if ( pressed != 0 && fighter._bThrowAttempted == SW_FALSE )
                {
                    fighter._bThrowAttempted = SW_TRUE; // 첫 시도만 — 틀린 버튼이면 더 못 푼다
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
                        break;
                    }
                }
                --fighter._stateFrames;
                if ( fighter._stateFrames <= 0 )
                    landThrow( player );
                break;
            }
            case FighterState::Knockout:
            {
                break;
            }
        }
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
        const hashed_string& moveId = fighter._pDef->_listMove[static_cast<size_t>( moveIndex )]._frame._id;
        for ( const MoveCancelWindow& window : fighter._timeline.getMove()._listCancel )
        {
            if ( window._toFrame < fighter._timeline.getFrame() )
                continue;
            for ( const hashed_string& candidate : window._listMoveId )
            {
                if ( candidate == moveId )
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
            // 스트링 · 캔슬 — 지금 기술의 캔슬 창 안에서만.
            if ( fighter._timeline.canCancelInto( move._frame._id ) == false )
                return false;
        }
        else
        {
            const bool bFree = bAsIfFree || fighter._state == FighterState::Neutral || fighter._state == FighterState::Sidestep ||
                               fighter._state == FighterState::Jump;
            if ( bFree == false || ( move._bStringOnly == SW_TRUE && isUpcomingCancelTarget( player, moveIndex ) == false ) )
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
        if ( move._bGroundHit == SW_TRUE && other._state != FighterState::Down )
            return false;
        if ( move._bNearWall == SW_TRUE && isNearWall( 1 - player ) == false )
            return false;
        if ( move._bRageArt == SW_TRUE && fighter._bRage == SW_FALSE )
            return false;
        if ( move._bHeatBurst == SW_TRUE && ( _settings._bHeatEnabled == SW_FALSE || fighter._bHeatUsed == SW_TRUE ) )
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
            // 기반 `InputCommandBuffer::findCompleted` 와 같은 규칙 — 우선도, 같으면 단계가 많은 것.
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
        aimAtOpponent( player ); // 공격 방향은 여기서 고정(추적 기술만 다시 겨눈다)
        pushEvent( FightingEvent::Kind::MoveStarted, player, moveIndex, move._frame._id );
        if ( move._bRageArt == SW_TRUE )
        {
            fighter._bRage     = SW_FALSE;
            fighter._bRageUsed = SW_TRUE;
            pushEvent( FightingEvent::Kind::RageArt, player, 0, move._frame._id );
        }
        if ( move._bHeatBurst == SW_TRUE )
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

        // 1) 지금 낼 수 있는 기술 — 없으면 못 움직이는 동안의 커맨드를 선입력에 담는다.
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

        // 2) 횡이동 · 점프.
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

        // 3) 자세 · 가드 · 걷기.
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
            const FighterState state = _arrFighter[player]._state;
            if ( state != FighterState::Attacking && state != FighterState::Juggle && state != FighterState::Throwing )
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
        const FighterRuntime& fighter = _arrFighter[attacker];
        const FighterRuntime& other   = _arrFighter[1 - attacker];
        if ( fighter._state != FighterState::Attacking || fighter._timeline.isInHitstop() || fighter._timeline.hasContact() ||
             fighter._timeline.getPhase() != MovePhase::Active )
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
                if ( pMove->_bGroundHit == SW_FALSE )
                    return false;
                break;
            }
            default:
            {
                break;
            }
        }
        if ( pMove->_frame._height == AttackHeight::Throw && ( other._bAirborne == SW_TRUE || other._state == FighterState::Juggle ) )
            return false; // 공중은 잡지 못한다

        // 허트 캡슐 — 바닥 원(반지름) × 높이 구간. 높이는 자세 · 상태로.
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

        // 공격 방향 기준 앞 거리 · 옆 거리(옆으로 벌어진 각이 크면 옆 거리가 반지름을 넘는다).
        const float32 relX    = other._x - fighter._x;
        const float32 relZ    = other._z - fighter._z;
        const float32 forward = relX * fighter._dirX + relZ * fighter._dirZ;
        const float32 lateral = MathUtil::abs( relX * fighter._dirZ - relZ * fighter._dirX );

        (void)fighter._timeline.collectActiveHitboxes( _listHitboxScratch );
        for ( const MoveHitbox& hitbox : _listHitboxScratch )
        {
            const float32 halfWidth   = hitbox._width * 0.5f;
            const float32 halfHeight  = hitbox._height * 0.5f;
            const float32 thickness   = hitbox._shape == HitboxShape::Capsule ? MathUtil::min( hitbox._width, hitbox._height ) * 0.5f : 0.0f;
            const float32 boxBottom   = fighter._y + hitbox._y - halfHeight;
            const float32 boxTop      = fighter._y + hitbox._y + halfHeight;
            const bool    bReach      = hitbox._x - halfWidth <= forward + radius && forward - radius <= hitbox._x + halfWidth;
            const bool    bSide       = lateral <= radius + thickness;
            const bool    bHeightBand = boxBottom <= bodyTop && bodyBottom <= boxTop;
            if ( bReach && bSide && bHeightBand )
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
        // 두 쪽을 먼저 다 보고 함께 적용한다 — 같은 프레임에 닿으면 상쇄(둘 다 맞는다).
        bool arrContact[kPlayerCount] = { false, false };
        for ( int32 player = 0; player < kPlayerCount; ++player )
            arrContact[player] = testHit( player );
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
            return; // 높이로 헛쳤다(앉아서 상단 · 잡기) — 지속 프레임 동안 다시 본다

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
            // 잡았다 — 풀기 창을 연다. 피해는 창이 닫힐 때.
            enterState( defender, FighterState::ThrowBreak, _settings._throwBreakFrames );
            other._breakButtons    = move._breakButtons;
            other._bThrowAttempted = SW_FALSE;
            other._bufferedMove    = -1;
            fighter._timeline.registerContact( false );
            fighter._state       = FighterState::Throwing;
            fighter._stateFrames = MathUtil::max( 1, frameData._recovery );
            fighter._guard       = GuardStance::None;
            pushEvent( FightingEvent::Kind::ThrowGrab, attacker, 0, frameData._id );
            return;
        }

        const int32 damage = computeDamage( attacker, frameData._damage, bInCombo );
        applyDamage( defender, damage );
        fighter._timeline.registerContact( false );
        other._hitstop      = frameData._hitstop;
        other._bufferedMove = -1;
        ++other._comboHits;

        if ( other._state == FighterState::Juggle )
        {
            if ( move._bScrew == SW_TRUE && other._bScrewUsed == SW_FALSE )
            {
                // 스크류 — 다시 띄워 멀리 나른다. 한 콤보에 한 번.
                other._bScrewUsed = SW_TRUE;
                other._velocityY  = _settings._screwVelocity;
                other._carryX     = fighter._dirX * _settings._screwCarrySpeed;
                other._carryZ     = fighter._dirZ * _settings._screwCarrySpeed;
                pushEvent( FightingEvent::Kind::Screw, attacker, 0, frameData._id );
            }
            else if ( move._bBound == SW_TRUE && other._bBoundUsed == SW_FALSE )
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
        else if ( other._state == FighterState::WallSplat || other._state == FighterState::Down )
        {
            ++other._airHits; // 벽꽝 · 다운 추가타도 콤보 감쇠를 이어 간다
        }
        else if ( frameData._bLauncher == SW_TRUE || other._bAirborne == SW_TRUE )
        {
            // 띄우기 — 또는 점프 중에 맞아 떨어진다.
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
        else if ( frameData._bWallSplat == SW_TRUE && isNearWall( defender ) && other._bWallSplatUsed == SW_FALSE )
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

    void FightingMatch::landThrow( int32 defender )
    {
        const int32     attacker = 1 - defender;
        FighterRuntime& fighter  = _arrFighter[attacker];
        // 잡은 쪽은 `Throwing` 에서 기술 자리를 버렸으므로 피해는 잡힐 때 기억한 기술에서 — 잡은 쪽 타임라인의 기술입니다.
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
            FighterRuntime& fighter = _arrFighter[player];
            const float32   ratio   = static_cast<float32>( fighter._health ) / static_cast<float32>( MathUtil::max( 1, fighter._pDef->_health ) );
            if ( fighter._bRage == SW_FALSE && fighter._bRageUsed == SW_FALSE && ratio <= _settings._rageThreshold )
            {
                fighter._bRage = SW_TRUE;
                pushEvent( FightingEvent::Kind::RageEntered, player, fighter._health );
            }
        }

        if ( _series.advanceTick() != RoundSeriesTick::TimeUp )
            return;
        // 시간 초과 — 체력 비율(정수 교차 곱으로 정확히)이 높은 쪽.
        const int64 left  = static_cast<int64>( _arrFighter[0]._health ) * MathUtil::max( 1, _arrFighter[1]._pDef->_health );
        const int64 right = static_cast<int64>( _arrFighter[1]._health ) * MathUtil::max( 1, _arrFighter[0]._pDef->_health );
        endRound( left > right ? 0 : ( left < right ? 1 : kDraw ), FightingEvent::Kind::TimeOut );
    }

    void FightingMatch::endRound( int32 winner, FightingEvent::Kind reason )
    {
        pushEvent( reason, winner, 0 );
        _lastRoundWinner = winner;
        // 무승부 라운드는 둘 다 1 위 — 둘 다 1 승이다. 라운드 중에만 불리므로 거절되지 않는다.
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

    void FightingMatch::pushEvent( FightingEvent::Kind kind, int32 player, int32 value, const hashed_string& moveId )
    {
        FightingEvent event;
        event._kind   = kind;
        event._player = player;
        event._value  = value;
        event._moveId = moveId;
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
        // 롤백은 프레임마다 · 되감아 다시 돌 때마다 저장한다 — 링 슬롯의 버퍼를 이어받아 쓰고 돌려준다(새로 잡지 않는다).
        BitWriter writer{ std::move( outBuffer ) };
        writer.writeUint32( FightingMatchInternal::kStateMagic );
        writer.writeVarInt( FightingMatchInternal::kStateVersion );
        FightingMatchInternal::writeCounter( writer, _frame );
        FightingMatchInternal::writeCounter( writer, _lastRoundWinner );
        for ( const FighterRuntime& fighter : _arrFighter )
        {
            writer.writeVarUint( fighter._pDef != nullptr ? fighter._pDef->_listMove.size() : 0 ); // 같은 캐릭터인지 대 보는 값
            writer.writeFloat( fighter._x );
            writer.writeFloat( fighter._z );
            writer.writeFloat( fighter._y );
            writer.writeFloat( fighter._dirX );
            writer.writeFloat( fighter._dirZ );
            writer.writeFloat( fighter._velocityY );
            writer.writeFloat( fighter._gravity );
            writer.writeFloat( fighter._carryX );
            writer.writeFloat( fighter._carryZ );
            const int32 arrCounter[] = { fighter._health, fighter._stateFrames, fighter._hitstop, fighter._moveIndex, fighter._bufferedMove,
                                         fighter._bufferedAge, fighter._stanceIndex, fighter._stanceFrames, fighter._comboHits, fighter._airHits,
                                         fighter._heatFrames, fighter._sidestepSign, fighter._side, fighter._breakButtons };
            for ( const int32 counter : arrCounter )
                FightingMatchInternal::writeCounter( writer, counter );
            writer.writeVarUint( static_cast<uint64>( fighter._state ) );
            writer.writeVarUint( static_cast<uint64>( fighter._posture ) );
            writer.writeVarUint( static_cast<uint64>( fighter._guard ) );
            const uint8 arrFlag[] = { fighter._bAirborne, fighter._bScrewUsed, fighter._bBoundUsed, fighter._bWallSplatUsed,
                                      fighter._bRage, fighter._bRageUsed, fighter._bHeatUsed, fighter._bThrowAttempted };
            for ( const uint8 flag : arrFlag )
                writer.writeBool( flag == SW_TRUE );
            FightingMatchInternal::writeTimeline( writer, fighter._timeline );
            // 입력 버퍼 — 오래된 것부터(되살릴 때 그 순서로 다시 쌓는다).
            FightingMatchInternal::writeCounter( writer, fighter._inputBuffer.getFrameCount() );
            for ( int32 framesAgo = fighter._inputBuffer.getFrameCount() - 1; framesAgo >= 0; --framesAgo )
                writer.writeBits( encodeInput( fighter._inputBuffer.getFrame( framesAgo ) ), 8 );
        }
        // 라운드 묶음은 맨 뒤 — 되살릴 때 마지막에 읽어, 맞을 때만 바뀌는 묶음 덕에 loadState 가 통째로 원자적이다.
        _series.writeState( writer );
        outBuffer = writer.releaseBytes();
    }

    bool FightingMatch::loadState( const vector<uint8>& buffer )
    {
        BitReader reader( buffer.data(), static_cast<int32>( buffer.size() ) );
        if ( reader.readUint32() != FightingMatchInternal::kStateMagic || reader.readVarInt() != FightingMatchInternal::kStateVersion )
            return false;
        const int32 frame           = FightingMatchInternal::readCounter( reader );
        const int32 lastRoundWinner = FightingMatchInternal::readCounter( reader );

        FighterRuntime arrLoaded[kPlayerCount] = { _arrFighter[0], _arrFighter[1] };
        for ( FighterRuntime& fighter : arrLoaded )
        {
            if ( fighter._pDef == nullptr || reader.readVarUint() != fighter._pDef->_listMove.size() )
                return false;
            fighter._x          = reader.readFloat();
            fighter._z          = reader.readFloat();
            fighter._y          = reader.readFloat();
            fighter._dirX       = reader.readFloat();
            fighter._dirZ       = reader.readFloat();
            fighter._velocityY  = reader.readFloat();
            fighter._gravity    = reader.readFloat();
            fighter._carryX     = reader.readFloat();
            fighter._carryZ     = reader.readFloat();
            int32* arrCounter[] = { &fighter._health, &fighter._stateFrames, &fighter._hitstop, &fighter._moveIndex, &fighter._bufferedMove,
                                    &fighter._bufferedAge, &fighter._stanceIndex, &fighter._stanceFrames, &fighter._comboHits, &fighter._airHits,
                                    &fighter._heatFrames, &fighter._sidestepSign, &fighter._side };
            for ( int32* pCounter : arrCounter )
                *pCounter = FightingMatchInternal::readCounter( reader );
            fighter._breakButtons    = static_cast<uint16>( FightingMatchInternal::readCounter( reader ) & 0xFFFF );
            const uint64 state       = reader.readVarUint();
            const uint64 posture     = reader.readVarUint();
            const uint64 guard       = reader.readVarUint();
            const int32  moveCount   = static_cast<int32>( fighter._pDef->_listMove.size() );
            const int32  stanceCount = static_cast<int32>( fighter._pDef->_listStance.size() );
            const bool   bBadEnum    = state >= static_cast<uint64>( FightingMatchInternal::kStateCount ) ||
                                  posture > static_cast<uint64>( FighterPosture::Stance ) || guard > static_cast<uint64>( GuardStance::Crouching );
            const bool bBadIndex = fighter._moveIndex < -1 || fighter._moveIndex >= moveCount || fighter._bufferedMove < -1 ||
                                   fighter._bufferedMove >= moveCount || fighter._stanceIndex < -1 || fighter._stanceIndex >= stanceCount;
            if ( bBadEnum || bBadIndex )
                return false;
            fighter._state   = static_cast<FighterState>( state );
            fighter._posture = static_cast<FighterPosture>( posture );
            fighter._guard   = static_cast<GuardStance>( guard );
            uint8* arrFlag[] = { &fighter._bAirborne, &fighter._bScrewUsed, &fighter._bBoundUsed, &fighter._bWallSplatUsed,
                                 &fighter._bRage, &fighter._bRageUsed, &fighter._bHeatUsed, &fighter._bThrowAttempted };
            for ( uint8* pFlag : arrFlag )
                *pFlag = reader.readBool() ? SW_TRUE : SW_FALSE;
            if ( FightingMatchInternal::readTimeline( reader, fighter.findCurrentMove(), fighter._timeline ) == false )
                return false;
            const int32 inputCount = FightingMatchInternal::readCounter( reader );
            if ( inputCount < 0 || inputCount > fighter._inputBuffer.getCapacity() )
                return false;
            fighter._inputBuffer.clear();
            for ( int32 index = 0; index < inputCount; ++index )
                fighter._inputBuffer.push( decodeInput( static_cast<uint8>( reader.readBits( 8 ) ) ) );
        }
        if ( reader.hasOverflowed() )
            return false;
        // 라운드 묶음은 맨 뒤 — 맞을 때만 바뀌므로 이 뒤에는 실패할 일이 없다.
        if ( _series.readState( reader ) == false )
            return false;

        _arrFighter[0]   = arrLoaded[0];
        _arrFighter[1]   = arrLoaded[1];
        _frame           = frame;
        _lastRoundWinner = lastRoundWinner;
        _eventBuffer.clear();
        return true;
    }
} // namespace sw
