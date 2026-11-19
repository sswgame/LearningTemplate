#include "pch.h"

#include "Games/Empty/KitDemo/FightingDemoComponent.h"

#include "Core/Encoding/FourCCUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Action/Fighting/FightingMatch.h"

namespace sw
{
    namespace
    {
        struct FightingDemoComponentInternal
        {
            static constexpr uint32 kStateTag     = FourCCUtil::make( "KDFG" );
            static constexpr uint32 kStateVersion = 1;

            // 버튼 "3"(비트 2)이 하단이다(카탈로그 buttons="1,2,3,4").
            static constexpr const utf8* kFighterID   = "basic";
            static constexpr uint16      kLowButton   = 1 << 2; ///< 버튼 "3"
            static constexpr int32       kRoundsToWin = 1;      ///< 한 라운드로 대전이 끝난다
            static constexpr int32       kHitFrames   = 2;      ///< 누른 뒤 첫 지속 프레임(발생 3)까지 더 돌리는 프레임
            static constexpr int32       kAttacker    = 0;
            static constexpr int32       kDefender    = 1;
            static constexpr int32       kNoWinner    = -1;
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( kitDemoFightingState, "KitDemo.Fighting.State", "Fighting demo state (0 not started, 1 match run finished)", FightingDemoComponent,
                                &FightingDemoComponent::getDemoState );
    SW_AUTOMATION_GETTER_WATCH( kitDemoFightingWinner, "KitDemo.Fighting.MatchWinner", "Match winner (0, 1, 2 draw, -1 not over)", FightingDemoComponent,
                                &FightingDemoComponent::getMatchWinner );
    SW_AUTOMATION_GETTER_WATCH( kitDemoFightingLoserHealth, "KitDemo.Fighting.LoserHealth", "Health of the knocked out player", FightingDemoComponent,
                                &FightingDemoComponent::getLoserHealth );
    SW_AUTOMATION_GETTER_WATCH( kitDemoFightingRounds, "KitDemo.Fighting.WinnerRounds", "Rounds won by player 0", FightingDemoComponent,
                                &FightingDemoComponent::getWinnerRounds );

    FightingDemoComponent::FightingDemoComponent()
        : _catalogPath{}
        , _pCatalog{}
        , _matchWinner{ FightingDemoComponentInternal::kNoWinner }
        , _loserHealth{ 0 }
        , _winnerRounds{ 0 }
    {
    }

    FightingDemoComponent::~FightingDemoComponent() = default;

    bool FightingDemoComponent::runMatch()
    {
        const FighterDef* pFighter = _pCatalog->find( hashed_string( FightingDemoComponentInternal::kFighterID ) );
        if ( pFighter == nullptr )
        {
            SW_LOG_ERROR_ID( "KitDemo.FightingUnknownFighter", "Fighting demo: fighter '%#' is not in the catalog - the match does not open", FightingDemoComponentInternal::kFighterID );
            return false;
        }
        FightingSettings settings;
        settings._roundsToWin = FightingDemoComponentInternal::kRoundsToWin;
        FightingMatch match;
        if ( match.initialize( *pFighter, *pFighter, settings ) == false )
            return false;
        // 중립 한 프레임 뒤에 눌러야 "새로 누름" 으로 읽힌다. 하단은 서 있는 상대가 막지 못한다.
        const InputFrame neutral{};
        match.advanceFrame( neutral, neutral );
        InputFrame press{};
        press._buttons = FightingDemoComponentInternal::kLowButton;
        match.advanceFrame( press, neutral );
        for ( int32 frame = 0; frame < FightingDemoComponentInternal::kHitFrames; ++frame )
        {
            match.advanceFrame( neutral, neutral );
        }
        _matchWinner  = match.getMatchWinner();
        _loserHealth  = match.getFighter( FightingDemoComponentInternal::kDefender )._health;
        _winnerRounds = match.getRoundWins( FightingDemoComponentInternal::kAttacker );
        markRunFinished();
        return true;
    }

    bool FightingDemoComponent::startGame()
    {
        if ( acquireRequired( _catalogPath, _pCatalog ) == false )
            return false;
        return runMatch();
    }

    void FightingDemoComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "KitDemo.FightingDone", "[KitDemo] fighting: player %# won the match, loser health %#, %# rounds", _matchWinner, _loserHealth, _winnerRounds );
    }

    void FightingDemoComponent::serializeGameState( StateIO& io )
    {
        serializeResults( io, FightingDemoComponentInternal::kStateTag, FightingDemoComponentInternal::kStateVersion );
    }
} // namespace sw
