#include "pch.h"

#include "GameFramework/Base/Gameplay/Match/RoundSeries.h"

#include "Engine/Reflection/ReflectionValidation.h"

#include "GameFramework/Base/Foundation/Utility/ConfigRangeUtil.h"

namespace sw
{
    RoundSeries::RoundSeries()
        : _settings{}
        , _listTotal{}
        , _roundIndex{ 0 }
        , _roundTicksRemaining{ 0 }
        , _intermissionTicksRemaining{ 0 }
        , _winner{ kNoWinner }
        , _phase{ RoundSeriesPhase::Waiting }
    {
    }

    bool RoundSeries::initialize( const RoundSeriesSettings& settings )
    {
        const string rangeError = ConfigRangeUtil::describeRangeErrors( settings );
        if ( rangeError.empty() == false )
        {
            SW_LOG_ERROR( "RoundSeries: refused settings - %#", rangeError.c_str() );
            return false;
        }
        _settings = settings;
        _listTotal.clear();
        _roundIndex                 = 0;
        _roundTicksRemaining        = 0;
        _intermissionTicksRemaining = 0;
        _winner                     = kNoWinner;
        _phase                      = RoundSeriesPhase::Waiting;
        return true;
    }

    bool RoundSeries::start( int32 participantCount )
    {
        if ( participantCount < 1 )
            return false;
        _listTotal.assign( static_cast<size_t>( participantCount ), 0 );
        _winner = kNoWinner;
        openRound( 0 );
        return true;
    }

    void RoundSeries::openRound( int32 roundIndex )
    {
        _roundIndex                 = roundIndex;
        _roundTicksRemaining        = _settings._roundTicks;
        _intermissionTicksRemaining = 0;
        _phase                      = RoundSeriesPhase::RoundActive;
    }

    RoundSeriesTick RoundSeries::advanceTick()
    {
        if ( _phase == RoundSeriesPhase::Intermission )
        {
            --_intermissionTicksRemaining;
            if ( _intermissionTicksRemaining > 0 )
                return RoundSeriesTick::None;
            openRound( _roundIndex + 1 );
            return RoundSeriesTick::RoundStarted;
        }
        if ( _phase != RoundSeriesPhase::RoundActive || _settings._roundTicks <= 0 )
            return RoundSeriesTick::None;
        if ( _roundTicksRemaining > 0 )
            --_roundTicksRemaining;
        return _roundTicksRemaining > 0 ? RoundSeriesTick::None : RoundSeriesTick::TimeUp;
    }

    RoundSeriesOutcome RoundSeries::reportRound( const vector<int32>& listRoundScore )
    {
        if ( _phase != RoundSeriesPhase::RoundActive || listRoundScore.size() != _listTotal.size() )
            return RoundSeriesOutcome::Rejected;
        const int32 participantCount = getParticipantCount();
        for ( int32 participant = 0; participant < participantCount; ++participant )
        {
            _listTotal[static_cast<size_t>( participant )] += getPlacementPoint( computeRank( listRoundScore, participant ) );
        }
        return finishRound();
    }

    RoundSeriesOutcome RoundSeries::reportRoundWinner( int32 winner )
    {
        const bool bKnownWinner = winner == kNoWinner || isValidParticipant( winner );
        if ( _phase != RoundSeriesPhase::RoundActive || bKnownWinner == false )
            return RoundSeriesOutcome::Rejected;
        // 무승부 라운드는 모두 1 위이고, 아니면 이긴 쪽이 1 위, 나머지가 2 위이다.
        const int32 participantCount = getParticipantCount();
        for ( int32 participant = 0; participant < participantCount; ++participant )
        {
            const int32 rank = winner == kNoWinner || winner == participant ? 1 : 2;
            _listTotal[static_cast<size_t>( participant )] += getPlacementPoint( rank );
        }
        return finishRound();
    }

    RoundSeriesOutcome RoundSeries::finishRound()
    {
        // 목표에 닿은 참가자 중 총점이 가장 높은 한 명을 고른다. 같으면 동점 규칙을 따른다.
        int32       best             = kNoWinner;
        int32       bestTotal        = 0;
        bool        bTied            = false;
        const int32 participantCount = getParticipantCount();
        for ( int32 participant = 0; participant < participantCount; ++participant )
        {
            const int32 total = _listTotal[static_cast<size_t>( participant )];
            if ( total < _settings._winScore )
                continue;
            if ( best == kNoWinner || total > bestTotal )
            {
                best      = participant;
                bestTotal = total;
                bTied     = false;
            }
            else if ( total == bestTotal )
            {
                bTied = true;
            }
        }
        const bool bDecided = best != kNoWinner && ( bTied == false || _settings._tieRule == RoundSeriesTieRule::Draw );
        if ( bDecided )
        {
            _winner = bTied ? kNoWinner : best;
            _phase  = RoundSeriesPhase::Finished;
            return RoundSeriesOutcome::Finished;
        }
        if ( _settings._intermissionTicks > 0 )
        {
            _phase                      = RoundSeriesPhase::Intermission;
            _intermissionTicksRemaining = _settings._intermissionTicks;
            return RoundSeriesOutcome::Intermission;
        }
        openRound( _roundIndex + 1 );
        return RoundSeriesOutcome::NextRound;
    }

    void RoundSeries::validateState( ValidationContext& context ) const
    {
        if ( _winner >= getParticipantCount() )
            context.addError( "round series winner is not a participant" );
    }

    int32 RoundSeries::computeRank( const vector<int32>& listRoundScore, int32 participant )
    {
        if ( participant < 0 || participant >= static_cast<int32>( listRoundScore.size() ) )
            return 0;
        const int32 score = listRoundScore[static_cast<size_t>( participant )];
        int32       rank  = 1;
        for ( const int32 other : listRoundScore )
        {
            rank += other > score ? 1 : 0;
        }
        return rank;
    }

    int32 RoundSeries::getPlacementPoint( int32 rank ) const
    {
        if ( rank < 1 )
            return 0;
        const vector<int32>& listPoint = _settings._listPlacementPoint;
        if ( listPoint.empty() )
            return rank == 1 ? 1 : 0;
        const size_t placement = static_cast<size_t>( rank - 1 );
        return placement < listPoint.size() ? listPoint[placement] : 0;
    }

    int32 RoundSeries::getTotal( int32 participant ) const
    {
        return isValidParticipant( participant ) ? _listTotal[static_cast<size_t>( participant )] : 0;
    }
} // namespace sw
