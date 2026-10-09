#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Server/Matchmaking/Service/MatchServerAgent.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Kits/Feature/Online/Matchmaking/MatchmakingProtocol.h"
#include "GameFramework/Kits/Feature/Online/Server/Matchmaking/Service/MatchQueueService.h"

namespace sw
{
    MatchServerAgent::MatchServerAgent()
        : _mapAccountToExpected{}
        , _newMatchBuffer{}
        , _serverId{ 0 }
    {
    }

    void MatchServerAgent::initialize( uint64 serverId ) { _serverId = serverId; }

    string MatchServerAgent::getAssignTopic() const { return MatchQueueService::makeAssignTopic( _serverId ); }

    bool MatchServerAgent::handleAssign( const vector<uint8>& bytes, int64 nowMs )
    {
        BitReader   reader( bytes.data(), static_cast<int32>( bytes.size() ) );
        MatchFormed match;
        if ( MatchmakingProtocol::readFormed( reader, match ) == false )
        {
            SW_LOG_WARNING( "MatchServerAgent: dropped a malformed assignment (%# bytes)", bytes.size() );
            return false;
        }
        for ( size_t team = 0; team < match._listTeam.size(); ++team )
        {
            for ( const MatchMember& member : match._listTeam[team] )
            {
                _mapAccountToExpected[member._accountId] = Expected{ match._matchId, nowMs + kExpectTtlMs, static_cast<int32>( team ) };
            }
        }
        _newMatchBuffer.push( std::move( match ) );
        return true;
    }

    void MatchServerAgent::tick( int64 nowMs )
    {
        for ( auto expectedIt = _mapAccountToExpected.begin(); expectedIt != _mapAccountToExpected.end(); )
        {
            if ( nowMs >= expectedIt->second._expiresMs )
                expectedIt = _mapAccountToExpected.erase( expectedIt );
            else
                ++expectedIt;
        }
    }

    bool MatchServerAgent::findExpected( AccountId accountId, uint64& outMatchId, int32& outTeam ) const
    {
        const auto expectedIt = _mapAccountToExpected.find( accountId );
        if ( expectedIt == _mapAccountToExpected.end() )
            return false;
        outMatchId = expectedIt->second._matchId;
        outTeam    = expectedIt->second._team;
        return true;
    }
} // namespace sw
