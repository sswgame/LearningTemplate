#include "pch.h"

#include "Games/Empty/KitDemo/PartyArenaDemoComponent.h"

#include "Core/Encoding/FourCCUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Serialization/Format/StateIO.h"

namespace sw
{
    namespace
    {
        struct PartyArenaDemoComponentInternal
        {
            static constexpr uint32 kStateTag     = FourCCUtil::make( "KDPA" );
            static constexpr uint32 kStateVersion = 1;

            static constexpr int32 kPlayerCount = 3;
            // 라운드마다 넘기는 사람 순서의 라운드 점수 — 0 번이 늘 1 위, 2 번이 늘 3 위다.
            static constexpr int32 kFirstPlayerRoundScore  = 10;
            static constexpr int32 kSecondPlayerRoundScore = 5;
            static constexpr int32 kThirdPlayerRoundScore  = 1;
            static constexpr int32 kMaxRounds              = 20; ///< 규칙이 틀려 우승자가 나오지 않을 때 멈추는 상한
            static constexpr int32 kNoWinner               = -1;
            static constexpr int32 kNoPoints               = 0;
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( kitDemoPartyArenaState, "KitDemo.PartyArena.State", "Party arena demo state (0 not started, 1 series run finished)", PartyArenaDemoComponent,
                                &PartyArenaDemoComponent::getDemoState );
    SW_AUTOMATION_GETTER_WATCH( kitDemoPartyArenaWinner, "KitDemo.PartyArena.Winner", "Player index that won the round series (-1 none)", PartyArenaDemoComponent,
                                &PartyArenaDemoComponent::getWinner );
    SW_AUTOMATION_GETTER_WATCH( kitDemoPartyArenaWinnerTotal, "KitDemo.PartyArena.WinnerTotal", "Series total of the winner", PartyArenaDemoComponent,
                                &PartyArenaDemoComponent::getWinnerTotal );
    SW_AUTOMATION_GETTER_WATCH( kitDemoPartyArenaRounds, "KitDemo.PartyArena.Rounds", "Rounds reported before the series ended", PartyArenaDemoComponent,
                                &PartyArenaDemoComponent::getRoundsPlayed );

    PartyArenaDemoComponent::PartyArenaDemoComponent()
        : _catalogPath{}
        , _pCatalog{}
        , _winner{ PartyArenaDemoComponentInternal::kNoWinner }
        , _winnerTotal{ 0 }
        , _roundsPlayed{ 0 }
    {
    }

    PartyArenaDemoComponent::~PartyArenaDemoComponent() = default;

    bool PartyArenaDemoComponent::runSeries()
    {
        // 공유 카탈로그는 읽기 전용이라 진행 상태를 가진 시리즈를 카탈로그 값으로 새로 세운다.
        PartyRoundSeries series;
        for ( const PartyRoundDef& round : _pCatalog->getRounds() )
        {
            series.addRound( round );
        }
        series.setPlacementPoints( _pCatalog->getPlacementPoints() );
        if ( series.setWinScore( _pCatalog->getWinScore() ) == false || series.start( PartyArenaDemoComponentInternal::kPlayerCount ) == false )
        {
            SW_LOG_ERROR_ID( "KitDemo.PartyArenaOpenFailed", "Party arena demo: the series catalog has no round or no usable win score - the series does not open" );
            return false;
        }
        const vector<int32> listRoundScore{ PartyArenaDemoComponentInternal::kFirstPlayerRoundScore, PartyArenaDemoComponentInternal::kSecondPlayerRoundScore,
                                            PartyArenaDemoComponentInternal::kThirdPlayerRoundScore };
        for ( int32 round = 0; round < PartyArenaDemoComponentInternal::kMaxRounds && series.isFinished() == false; ++round )
        {
            if ( series.reportRound( listRoundScore ) == false )
                return false;
            ++_roundsPlayed;
        }
        _winner      = series.getWinner();
        _winnerTotal = _winner != PartyArenaDemoComponentInternal::kNoWinner ? series.getTotal( _winner ) : PartyArenaDemoComponentInternal::kNoPoints;
        markRunFinished();
        return true;
    }

    bool PartyArenaDemoComponent::startGame()
    {
        if ( acquireRequired( _catalogPath, _pCatalog ) == false )
            return false;
        return runSeries();
    }

    void PartyArenaDemoComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "KitDemo.PartyArenaDone", "[KitDemo] party arena: player %# won with %# points after %# rounds", _winner, _winnerTotal, _roundsPlayed );
    }

    void PartyArenaDemoComponent::serializeGameState( StateIO& io )
    {
        serializeResults( io, PartyArenaDemoComponentInternal::kStateTag, PartyArenaDemoComponentInternal::kStateVersion );
    }
} // namespace sw
