#include "pch.h"

#include "GameFramework/Match/RoundSeries.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"

namespace sw
{
    namespace
    {
        struct RoundSeriesInternal
        {
            static constexpr int64 kMaxStateValue = 0x3FFFFFFF; ///< 상태 바이트 정수의 상한 — 깨진 바이트가 넘치는 값을 넣지 않게

            static bool isStateValue( int64 value, int64 minValue ) { return minValue <= value && value <= kMaxStateValue; }
        };
    } // namespace
} // namespace sw

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

    void RoundSeries::initialize( const RoundSeriesSettings& settings )
    {
        _settings                    = settings;
        _settings._winScore          = MathUtil::max( 1, settings._winScore );
        _settings._roundTicks        = MathUtil::max( 0, settings._roundTicks );
        _settings._intermissionTicks = MathUtil::max( 0, settings._intermissionTicks );
        _listTotal.clear();
        _roundIndex                 = 0;
        _roundTicksRemaining        = 0;
        _intermissionTicksRemaining = 0;
        _winner                     = kNoWinner;
        _phase                      = RoundSeriesPhase::Waiting;
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
            _listTotal[static_cast<size_t>( participant )] += getPlacementPoint( computeRank( listRoundScore, participant ) );
        return finishRound();
    }

    RoundSeriesOutcome RoundSeries::reportRoundWinner( int32 winner )
    {
        const bool bKnownWinner = winner == kNoWinner || isValidParticipant( winner );
        if ( _phase != RoundSeriesPhase::RoundActive || bKnownWinner == false )
            return RoundSeriesOutcome::Rejected;
        // 무승부 라운드는 모두 1 위, 아니면 이긴 쪽 1 위 · 나머지 2 위.
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
        // 목표에 닿은 참가자 중 총점이 가장 높은 한 명 — 같으면 동점 규칙.
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

    void RoundSeries::writeState( BitWriter& outWriter ) const
    {
        outWriter.writeVarUint( static_cast<uint64>( _phase ) );
        outWriter.writeVarInt( _roundIndex );
        outWriter.writeVarInt( _roundTicksRemaining );
        outWriter.writeVarInt( _intermissionTicksRemaining );
        outWriter.writeVarInt( _winner );
        outWriter.writeVarUint( static_cast<uint64>( _listTotal.size() ) );
        for ( const int32 total : _listTotal )
            outWriter.writeVarInt( total );
    }

    bool RoundSeries::readState( BitReader& reader )
    {
        const uint64 phase             = reader.readVarUint();
        const int64  roundIndex        = reader.readVarInt();
        const int64  roundTicks        = reader.readVarInt();
        const int64  intermissionTicks = reader.readVarInt();
        const int64  winner            = reader.readVarInt();
        const uint64 participantCount  = reader.readVarUint();
        const bool   bBadPhase         = phase > static_cast<uint64>( RoundSeriesPhase::Finished );
        const bool   bBadCounter       = RoundSeriesInternal::isStateValue( roundIndex, 0 ) == false || RoundSeriesInternal::isStateValue( roundTicks, 0 ) == false ||
                                 RoundSeriesInternal::isStateValue( intermissionTicks, 0 ) == false;
        const bool bBadWinner = RoundSeriesInternal::isStateValue( winner, kNoWinner ) == false || winner >= static_cast<int64>( _listTotal.size() );
        // 같은 참가자 수로 시작한 묶음에만 되돌린다.
        const bool bOtherSeries = participantCount != static_cast<uint64>( _listTotal.size() );
        if ( bBadPhase || bBadCounter || bBadWinner || bOtherSeries || reader.hasOverflowed() )
            return false;
        vector<int32> listTotal( _listTotal.size(), 0 );
        for ( int32& total : listTotal )
        {
            const int64 value = reader.readVarInt();
            if ( RoundSeriesInternal::isStateValue( value, -RoundSeriesInternal::kMaxStateValue ) == false )
                return false;
            total = static_cast<int32>( value );
        }
        if ( reader.hasOverflowed() )
            return false;

        _listTotal.swap( listTotal );
        _roundIndex                 = static_cast<int32>( roundIndex );
        _roundTicksRemaining        = static_cast<int32>( roundTicks );
        _intermissionTicksRemaining = static_cast<int32>( intermissionTicks );
        _winner                     = static_cast<int32>( winner );
        _phase                      = static_cast<RoundSeriesPhase>( phase );
        return true;
    }

    int32 RoundSeries::computeRank( const vector<int32>& listRoundScore, int32 participant )
    {
        if ( participant < 0 || participant >= static_cast<int32>( listRoundScore.size() ) )
            return 0;
        const int32 score = listRoundScore[static_cast<size_t>( participant )];
        int32       rank  = 1;
        for ( const int32 other : listRoundScore )
            rank += other > score ? 1 : 0;
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
