#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Rule/MatchMaker.h"

#include <algorithm>
#include <cstdlib>

namespace sw
{
    namespace
    {
        struct MatchMakerInternal
        {
            /** @brief 닻의 후보 하나 — 표 번호와 닻까지의 실력 거리입니다. */
            struct Candidate
            {
                int32 _ticketIndex{ 0 };
                int32 _ratingDistance{ 0 };
            };

            /** @brief 닻에 가까운 실력 순, 같으면 오래 기다린 순, 같으면 표 id 순 — 결정적이다. */
            struct CandidateOrder
            {
                const vector<MatchTicket>* _pListTicket{ nullptr };

                bool operator()( const Candidate& left, const Candidate& right ) const
                {
                    if ( left._ratingDistance != right._ratingDistance )
                        return left._ratingDistance < right._ratingDistance;
                    const MatchTicket& leftTicket  = ( *_pListTicket )[static_cast<size_t>( left._ticketIndex )];
                    const MatchTicket& rightTicket = ( *_pListTicket )[static_cast<size_t>( right._ticketIndex )];
                    if ( leftTicket._enqueuedMs != rightTicket._enqueuedMs )
                        return leftTicket._enqueuedMs < rightTicket._enqueuedMs;
                    return leftTicket._ticketID < rightTicket._ticketID;
                }
            };

            /** @brief 오래 기다린 순, 같으면 표 id 순입니다. */
            struct AgeOrder
            {
                bool operator()( const MatchTicket& left, const MatchTicket& right ) const
                {
                    if ( left._enqueuedMs != right._enqueuedMs )
                        return left._enqueuedMs < right._enqueuedMs;
                    return left._ticketID < right._ticketID;
                }
            };

            /** @brief 인원이 많은 표부터입니다(stable_sort 와 함께 — 같은 인원은 후보 순서 그대로). */
            struct TicketSizeOrder
            {
                const vector<MatchTicket>* _pListTicket{ nullptr };

                bool operator()( int32 left, int32 right ) const
                {
                    return ( *_pListTicket )[static_cast<size_t>( left )]._listMember.size() > ( *_pListTicket )[static_cast<size_t>( right )]._listMember.size();
                }
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    MatchMaker::MatchMaker()
        : _listTicket{}
        , _definition{}
        , _nextMatchID{ 1 }
    {
    }

    void MatchMaker::initialize( const MatchModeDefinition& definition, uint64 matchIDSeed )
    {
        _definition            = definition;
        _definition._teamCount = std::clamp( definition._teamCount, 1, MatchmakingLimit::kMaxTeamCount );
        _definition._teamSize  = std::clamp( definition._teamSize, 1, MatchmakingLimit::kMaxTeamSize );
        _nextMatchID           = ( matchIDSeed << 32 ) | 1u;
        _listTicket.clear();
    }

    int32 MatchMaker::computeRatingWindow( const MatchTicket& ticket, int64 nowMs ) const
    {
        const int64 waitedSeconds = std::max<int64>( 0, nowMs - ticket._enqueuedMs ) / 1000;
        const int64 window        = static_cast<int64>( _definition._baseRatingWindow ) + waitedSeconds * _definition._ratingWindowPerSecond;
        return static_cast<int32>( std::min<int64>( window, _definition._maxRatingWindow ) );
    }

    MatchmakingResult MatchMaker::addTicket( const MatchTicket& ticket )
    {
        if ( ticket._listMember.empty() || static_cast<int32>( ticket._listMember.size() ) > _definition._teamSize )
            return MatchmakingResult::PartyTooLarge;
        if ( hasTicket( ticket._ticketID ) )
            return MatchmakingResult::AlreadyQueued;
        _listTicket.push_back( ticket );
        return MatchmakingResult::Ok;
    }

    bool MatchMaker::removeTicket( uint64 ticketID )
    {
        for ( size_t index = 0; index < _listTicket.size(); ++index )
        {
            if ( _listTicket[index]._ticketID == ticketID )
            {
                _listTicket.erase( _listTicket.begin() + static_cast<ptrdiff_t>( index ) );
                return true;
            }
        }
        return false;
    }

    bool MatchMaker::hasTicket( uint64 ticketID ) const
    {
        for ( const MatchTicket& ticket : _listTicket )
        {
            if ( ticket._ticketID == ticketID )
                return true;
        }
        return false;
    }

    void MatchMaker::process( int64 nowMs, vector<MatchFormed>& outListMatch, vector<MatchTicket>& outListTimedOut )
    {
        // 1) 시한 — 넣은 순서를 지켜 돌려준다.
        vector<MatchTicket> listWaiting;
        listWaiting.reserve( _listTicket.size() );
        for ( MatchTicket& ticket : _listTicket )
        {
            if ( nowMs - ticket._enqueuedMs > _definition._maxWaitMs )
                outListTimedOut.push_back( std::move( ticket ) );
            else
                listWaiting.push_back( std::move( ticket ) );
        }
        _listTicket.swap( listWaiting );

        // 2) 오래 기다린 순으로 닻
        std::sort( _listTicket.begin(), _listTicket.end(), MatchMakerInternal::AgeOrder{} );
        vector<uint8> listUsed( _listTicket.size(), 0 );
        for ( int32 anchorIndex = 0; anchorIndex < static_cast<int32>( _listTicket.size() ); ++anchorIndex )
        {
            if ( listUsed[static_cast<size_t>( anchorIndex )] != 0 )
                continue;
            MatchFormed match;
            if ( tryFormMatch( anchorIndex, nowMs, listUsed, match ) == false )
                continue;
            match._matchID = _nextMatchID++;
            outListMatch.push_back( std::move( match ) );
        }

        // 3) 쓴 표를 뺀다
        vector<MatchTicket> listRemaining;
        listRemaining.reserve( _listTicket.size() );
        for ( size_t index = 0; index < _listTicket.size(); ++index )
        {
            if ( listUsed[index] == 0 )
                listRemaining.push_back( std::move( _listTicket[index] ) );
        }
        _listTicket.swap( listRemaining );
    }

    bool MatchMaker::tryFormMatch( int32 anchorIndex, int64 nowMs, vector<uint8>& inoutListUsed, MatchFormed& outMatch ) const
    {
        const MatchTicket& anchor       = _listTicket[static_cast<size_t>( anchorIndex )];
        const int32        anchorRating = anchor.computeAverageRating();
        const int32        anchorWindow = computeRatingWindow( anchor, nowMs );
        const bool         bAnchorRelax = nowMs - anchor._enqueuedMs >= _definition._regionRelaxMs;
        const int32        totalSlots   = _definition._teamCount * _definition._teamSize;

        vector<MatchMakerInternal::Candidate> listCandidate;
        for ( int32 index = 0; index < static_cast<int32>( _listTicket.size() ); ++index )
        {
            if ( index == anchorIndex || inoutListUsed[static_cast<size_t>( index )] != 0 )
                continue;
            const MatchTicket& ticket   = _listTicket[static_cast<size_t>( index )];
            const int32        distance = std::abs( ticket.computeAverageRating() - anchorRating );
            if ( distance > std::max( anchorWindow, computeRatingWindow( ticket, nowMs ) ) )
                continue;
            const bool bRelax = bAnchorRelax || nowMs - ticket._enqueuedMs >= _definition._regionRelaxMs;
            if ( bRelax == false && ticket._region != anchor._region )
                continue;
            listCandidate.push_back( MatchMakerInternal::Candidate{ index, distance } );
        }
        std::sort( listCandidate.begin(), listCandidate.end(), MatchMakerInternal::CandidateOrder{ &_listTicket } );

        vector<int32> listChosen{ anchorIndex };
        int32         filledSlots = static_cast<int32>( anchor._listMember.size() );
        for ( const MatchMakerInternal::Candidate& candidate : listCandidate )
        {
            if ( filledSlots == totalSlots )
                break;
            const int32 size = static_cast<int32>( _listTicket[static_cast<size_t>( candidate._ticketIndex )]._listMember.size() );
            if ( filledSlots + size > totalSlots )
                continue;
            listChosen.push_back( candidate._ticketIndex );
            filledSlots += size;
        }
        if ( filledSlots != totalSlots || partitionIntoTeams( listChosen, outMatch ) == false )
            return false;

        for ( const int32 index : listChosen )
        {
            inoutListUsed[static_cast<size_t>( index )] = 1;
            outMatch._listTicket.push_back( _listTicket[static_cast<size_t>( index )] );
        }
        outMatch._modeID = _definition._modeID;
        outMatch._region = anchor._region;
        int64 ratingSum  = 0;
        for ( const vector<MatchMember>& team : outMatch._listTeam )
        {
            for ( const MatchMember& member : team )
            {
                ratingSum += member._rating;
            }
        }
        outMatch._averageRating = static_cast<int32>( ratingSum / totalSlots );
        return true;
    }

    bool MatchMaker::partitionIntoTeams( const vector<int32>& listTicketIndex, MatchFormed& outMatch ) const
    {
        // 큰 표부터 — 작은 표가 먼저 자리를 나눠 가지면 파티가 들어갈 팀이 남지 않는다.
        vector<int32> listOrder = listTicketIndex;
        std::stable_sort( listOrder.begin(), listOrder.end(), MatchMakerInternal::TicketSizeOrder{ &_listTicket } );
        outMatch._listTeam.assign( static_cast<size_t>( _definition._teamCount ), vector<MatchMember>{} );
        vector<int64> listTeamRating( static_cast<size_t>( _definition._teamCount ), 0 );
        for ( const int32 ticketIndex : listOrder )
        {
            const MatchTicket& ticket   = _listTicket[static_cast<size_t>( ticketIndex )];
            const int32        size     = static_cast<int32>( ticket._listMember.size() );
            int32              bestTeam = -1;
            for ( int32 team = 0; team < _definition._teamCount; ++team )
            {
                const bool bRoom  = static_cast<int32>( outMatch._listTeam[static_cast<size_t>( team )].size() ) + size <= _definition._teamSize;
                const bool bLower = bestTeam < 0 || listTeamRating[static_cast<size_t>( team )] < listTeamRating[static_cast<size_t>( bestTeam )];
                if ( bRoom && bLower )
                    bestTeam = team;
            }
            if ( bestTeam < 0 )
                return false; // 파티를 쪼개지 않고는 나눌 수 없다
            for ( const MatchMember& member : ticket._listMember )
            {
                outMatch._listTeam[static_cast<size_t>( bestTeam )].push_back( member );
                listTeamRating[static_cast<size_t>( bestTeam )] += member._rating;
            }
        }
        return true;
    }
} // namespace sw
