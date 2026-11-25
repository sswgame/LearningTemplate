#include "pch.h"

#include "GameFramework/Kits/Genre/Action/Fighting/Component/FightingGameModeComponent.h"

#include "Core/Foundation/Constants.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Serialization/Format/FormatDef.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Foundation/Minigame/MinigameStack.h"

namespace sw
{
    namespace
    {
        struct FightingGameModeComponentInternal
        {
            static constexpr FormatDef kStateFormat{ "FGHT" };

            // 넘패드 방향 표기다(격투 게임 공용).
            static constexpr uint8 kNeutral = 5;
            static constexpr uint8 kLeft    = 4;
            static constexpr uint8 kRight   = 6;
            static constexpr uint8 kFirst   = 1;
            static constexpr uint8 kLast    = 9;

            /** @brief 화면 쪽 @p side(+1 이면 오른쪽을 본다)에서 상대 쪽(앞)과 반대쪽(뒤)입니다. */
            static uint8 findForward( int32 side ) { return side > 0 ? kRight : kLeft; }
            static uint8 findBackward( int32 side ) { return side > 0 ? kLeft : kRight; }

            // 사운드 이벤트 이름 — 클립 · 범위 · 상한은 게임의 사운드 이벤트 데이터(audioevents.xml)가 정한다.
            static constexpr const utf8* kEventWallSplat   = "FightingWallSplat";
            static constexpr const utf8* kEventThrowBroken = "FightingThrowBroken";
            static constexpr const utf8* kEventSpecial     = "FightingSpecial";
            static constexpr const utf8* kEventRoundEnd    = "FightingRoundEnd";
            static constexpr const utf8* kEventWon         = "FightingWon";
            static constexpr const utf8* kEventLost        = "FightingLost";
            static constexpr const utf8* kEventLaunch      = "FightingLaunch";
            static constexpr const utf8* kEventThrowGrab   = "FightingThrowGrab";
            static constexpr const utf8* kEventSidestep    = "FightingSidestep";

            /** @brief 대전의 알림 @p kind 가 기술과 상관없이 내는 소리입니다. 없으면 nullptr 입니다(기술의 소리는 기술 표, 대전 끝은 이긴 쪽이 정한다). */
            static const utf8* findSound( FightingEvent::Kind kind )
            {
                switch ( kind )
                {
                    case FightingEvent::Kind::WallSplat:
                    {
                        return kEventWallSplat;
                    }
                    case FightingEvent::Kind::ThrowBroken:
                    {
                        return kEventThrowBroken;
                    }
                    case FightingEvent::Kind::RageEntered:
                    case FightingEvent::Kind::RageArt:
                    case FightingEvent::Kind::HeatActivated:
                    {
                        return kEventSpecial;
                    }
                    case FightingEvent::Kind::Knockout:
                    case FightingEvent::Kind::TimeOut:
                    {
                        return kEventRoundEnd;
                    }
                    case FightingEvent::Kind::Screw:
                    case FightingEvent::Kind::Bound:
                    {
                        return kEventLaunch;
                    }
                    case FightingEvent::Kind::ThrowGrab:
                    {
                        return kEventThrowGrab;
                    }
                    case FightingEvent::Kind::Sidestep:
                    {
                        return kEventSidestep;
                    }
                    case FightingEvent::Kind::MoveStarted: // 기술 표의 소리를 `findMoveSound` 가 고른다
                    case FightingEvent::Kind::Hit:
                    case FightingEvent::Kind::Blocked:
                    case FightingEvent::Kind::ThrowLanded:
                    case FightingEvent::Kind::RoundEnd: // 승부가 정한다(`settle`)
                    case FightingEvent::Kind::MatchEnd:
                    {
                        break;
                    }
                }
                return nullptr;
            }

            /** @brief 알림 @p kind 에 기술 @p move 가 내는 소리입니다(기술 표). 없으면 빈 이름입니다. */
            static hashed_string findMoveSound( FightingEvent::Kind kind, const MoveFrameData& move )
            {
                switch ( kind )
                {
                    case FightingEvent::Kind::MoveStarted:
                    {
                        return move._startSound;
                    }
                    case FightingEvent::Kind::Hit:
                    case FightingEvent::Kind::ThrowLanded:
                    {
                        return move._hitSound;
                    }
                    case FightingEvent::Kind::Blocked:
                    {
                        return move._blockSound;
                    }
                    default:
                    {
                        return hashed_string{};
                    }
                }
            }
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( fightingMatchWinner, "Fighting.MatchWinner", "Fighting match winner (0 player, 1 opponent, 2 draw, -1 not over)", FightingGameModeComponent,
                                &FightingGameModeComponent::getMatchWinner );
    SW_AUTOMATION_GETTER_WATCH( fightingRound, "Fighting.Round", "Fighting round number (1 = first)", FightingGameModeComponent, &FightingGameModeComponent::getRound );
    SW_AUTOMATION_GETTER_WATCH( fightingOpponentHealth, "Fighting.OpponentHealth", "Health of the opponent fighter", FightingGameModeComponent, &FightingGameModeComponent::getOpponentHealth );
    SW_AUTOMATION_GETTER_WATCH( fightingPlayerState, "Fighting.PlayerState", "State of the player fighter (FighterState: 0 neutral, 1 attacking, 2 sidestep, 3 jump ...)", FightingGameModeComponent,
                                &FightingGameModeComponent::getPlayerState );

    FightingGameModeComponent::FightingGameModeComponent()
        : _playerFighter{}
        , _opponentFighter{}
        , _rules{}
        , _cpuThinkFrames{ 18 }
        , _cpuAttackRange{ 1.1f }
        , _cpuAttackChance{ 0.6f }
        , _seed{ 7 }
        , _victoryMoney{ 30 }
        , _pCatalog{}
        , _match{}
        , _timer{}
        , _random{}
        , _arrCPUHand{}
        , _listEvent{}
        , _queuedButtons{ 0 }
        , _commandDirection{ FightingGameModeComponentInternal::kNeutral }
    {
    }

    FightingGameModeComponent::~FightingGameModeComponent() = default;

    bool FightingGameModeComponent::openMatch( FightingMatch& outMatch ) const
    {
        const FighterDef* const pPlayer   = _pCatalog->find( _playerFighter );
        const FighterDef* const pOpponent = _pCatalog->find( _opponentFighter );
        if ( pPlayer == nullptr || pOpponent == nullptr )
        {
            SW_LOG_ERROR_ID( "Fighting.UnknownFighter", "Fighting game mode: fighter '%#' or '%#' is not in the catalog - the match does not open", _playerFighter.c_str(), _opponentFighter.c_str() );
            return false;
        }
        return outMatch.initialize( *pPlayer, *pOpponent, _rules ); // 규칙이 범위 밖이면 대전이 오류를 남긴다
    }

    bool FightingGameModeComponent::startGame()
    {
        if ( acquireBoundCatalog( _pCatalog ) == false || openMatch( _match ) == false )
            return false;
        _timer.reset();
        _random = GameRandom( _seed );
        for ( CPUHand& hand : _arrCPUHand )
        {
            hand = CPUHand{};
        }
        _queuedButtons    = 0;
        _commandDirection = FightingGameModeComponentInternal::kNeutral;
        return true;
    }

    void FightingGameModeComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "Fighting.Started", "[Fighting] '%#' against '%#', first to %# rounds - the player input arrives as commands (direction, buttons)", _playerFighter.c_str(),
                        _opponentFighter.c_str(), _rules._roundsToWin );
    }

    bool FightingGameModeComponent::isAcceptingInput() const
    {
        return isStarted() && _match.getSeries().isRunning();
    }

    bool FightingGameModeComponent::submitPlayerInput( uint8 direction, uint16 pressedButtons )
    {
        const bool bDirectionValid = FightingGameModeComponentInternal::kFirst <= direction && direction <= FightingGameModeComponentInternal::kLast;
        if ( isAcceptingInput() == false || bDirectionValid == false )
            return false;
        _commandDirection = direction;
        _queuedButtons    = static_cast<uint16>( _queuedButtons | pressedButtons );
        return true;
    }

    AbsolutePosition FightingGameModeComponent::getFighterPosition( int32 player ) const
    {
        const FighterRuntime& fighter = _match.getFighter( player );
        return AbsolutePosition{ float3( fighter._x, fighter._y, fighter._z ) };
    }

    float32 FightingGameModeComponent::computeHealthRatio( int32 player ) const
    {
        const FighterRuntime& fighter = _match.getFighter( player );
        return fighter._pDef != nullptr ? static_cast<float32>( fighter._health ) / static_cast<float32>( fighter._pDef->_health ) : 0.0f;
    }

    float32 FightingGameModeComponent::computeSecondsRemaining() const
    {
        return static_cast<float32>( _match.getRoundFramesRemaining() ) * _timer.getStep();
    }

    InputFrame FightingGameModeComponent::consumePlayerInput()
    {
        InputFrame frame;
        frame._direction = _commandDirection;
        frame._buttons   = _queuedButtons;
        _queuedButtons   = 0;
        return frame;
    }

    InputFrame FightingGameModeComponent::makeCPUInput( int32 player )
    {
        using Internal                = FightingGameModeComponentInternal;
        CPUHand&              hand    = _arrCPUHand[player];
        const FighterRuntime& fighter = _match.getFighter( player );
        InputFrame            frame;
        frame._direction = hand._direction == 0 ? Internal::kNeutral : hand._direction;
        if ( hand._thinkFramesLeft > 0 )
        {
            --hand._thinkFramesLeft;
            return frame;
        }
        hand._thinkFramesLeft = _cpuThinkFrames - 1;
        if ( _match.computeDistance() > _cpuAttackRange )
        {
            hand._direction = Internal::findForward( fighter._side );
        }
        else if ( _random.nextChance( _cpuAttackChance ) && fighter._pDef->_listMove.empty() == false )
        {
            // 첫 단계의 버튼만 누른다 — 방향이 드는 커맨드는 컴퓨터가 쓰지 않는다.
            const int32         moveIndex = _random.nextInt( 0, static_cast<int32>( fighter._pDef->_listMove.size() ) - 1 );
            const InputCommand& command   = fighter._pDef->_listMove[static_cast<size_t>( moveIndex )]._command;
            frame._buttons                = command._listStep.empty() ? uint16{ 0 } : command._listStep.front()._buttons;
            hand._direction               = Internal::kNeutral;
        }
        else
        {
            hand._direction = Internal::findBackward( fighter._side );
        }
        frame._direction = hand._direction;
        return frame;
    }

    void FightingGameModeComponent::queueEventSound( const FightingEvent& event )
    {
        using Internal = FightingGameModeComponentInternal;
        if ( const utf8* const pSound = Internal::findSound( event._kind ) )
            getSoundQueue().queueEvent( pSound );
        // 기술 id 는 알림의 사람(공격한 쪽)이 쓴 것이라 그 캐릭터의 기술이다.
        const bool              bFromFighter = event._moveID.empty() == false && event._player.has_value() && 0 <= *event._player && *event._player < FightingMatch::kPlayerCount;
        const FighterDef* const pDef         = bFromFighter ? _match.getFighter( *event._player )._pDef : nullptr;
        const optional<int32>   moveIndex    = pDef != nullptr ? pDef->findMoveIndex( event._moveID ) : nullopt;
        if ( moveIndex.has_value() == false )
            return;
        const hashed_string sound = Internal::findMoveSound( event._kind, pDef->_listMove[static_cast<size_t>( *moveIndex )]._frame );
        if ( sound.empty() == false )
            getSoundQueue().queueEvent( sound.c_str() ); // 이름 표의 글은 사라지지 않는다
    }

    void FightingGameModeComponent::settle()
    {
        _listEvent.clear();
        _match.pollEvents( _listEvent );
        for ( const FightingEvent& event : _listEvent )
        {
            queueEventSound( event );
            if ( event._kind == FightingEvent::Kind::RoundEnd )
                SW_LOG_INFO_ID( "Fighting.RoundEnd", "[Fighting] round %# ends - winner %# (2 = draw)", _match.getRound(), event._value );
            if ( event._kind != FightingEvent::Kind::MatchEnd )
                continue;
            const utf8* pResult = kDrawResult;
            if ( event._value == kPlayer )
                pResult = kWinResult;
            else if ( event._value == kOpponent )
                pResult = kLoseResult;
            endMatch( hashed_string( pResult ) );
            getSoundQueue().queueEvent( event._value == kPlayer ? FightingGameModeComponentInternal::kEventWon : FightingGameModeComponentInternal::kEventLost );
            SW_LOG_INFO_ID( "Fighting.MatchEnd", "[Fighting] the match ends - winner %# (2 = draw), rounds %# to %#", event._value, getPlayerRounds(), getOpponentRounds() );
        }
    }

    void FightingGameModeComponent::updateMatch( float32 deltaTime )
    {
        const bool  bAutoPlay = isAutoPlayOn();
        const int32 stepCount = _timer.consume( deltaTime );
        for ( int32 step = 0; step < stepCount; ++step )
        {
            const InputFrame playerInput = bAutoPlay ? makeCPUInput( kPlayer ) : consumePlayerInput();
            _match.advanceFrame( playerInput, makeCPUInput( kOpponent ) );
        }
        settle();
    }

    void FightingGameModeComponent::collectMinigameResult( vector<MinigameResultValue>& outListValue ) const
    {
        if ( getMatchWinner() == kPlayer )
            addMoneyResult( outListValue, _victoryMoney );
    }

    void FightingGameModeComponent::serializeGameState( StateIO& io )
    {
        io.header( FightingGameModeComponentInternal::kStateFormat );
        if ( io.isOk() == false )
            return;
        // 대전은 같은 캐릭터로 연 뒤에 읽는다(키트 계약). 입력 명령은 싣지 않는다.
        if ( io.valueOpened( _match, [this]( FightingMatch& match )
        { return _pCatalog != nullptr && openMatch( match ); } ) == false )
            return;
        io.value( _timer );
        io.value( _random );
        for ( CPUHand& hand : _arrCPUHand )
        {
            io.value( hand._thinkFramesLeft );
            io.value( hand._direction );
            io.check( hand._thinkFramesLeft >= 0 && hand._direction <= FightingGameModeComponentInternal::kLast );
        }
        io.end();
    }
} // namespace sw
