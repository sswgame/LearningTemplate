#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/MatchServerAgent.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/MatchQueueService.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Shared/MatchmakingProtocol.h"

namespace sw
{
    MatchServerAgent::MatchServerAgent()
        : _mapAccountToExpected{}
        , _newMatchBuffer{}
        , _serverID{ 0 }
    {
    }

    void MatchServerAgent::initialize( uint64 serverID ) { _serverID = serverID; }

    string MatchServerAgent::getAssignTopic() const { return MatchQueueService::makeAssignTopic( _serverID ); }

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
                _mapAccountToExpected[member._accountID] = Expected{ match._matchID, nowMs + kExpectTtlMs, static_cast<int32>( team ) };
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

    bool MatchServerAgent::findExpected( AccountID accountID, uint64& outMatchID, int32& outTeam ) const
    {
        const auto expectedIt = _mapAccountToExpected.find( accountID );
        if ( expectedIt == _mapAccountToExpected.end() )
            return false;
        outMatchID = expectedIt->second._matchID;
        outTeam    = expectedIt->second._team;
        return true;
    }
} // namespace sw
