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

    void MatchServerAgent::initialize( uint64 serverID )
    {
        _serverID = serverID;
    }

    string MatchServerAgent::getAssignTopic() const
    {
        return MatchQueueService::makeAssignTopic( _serverID );
    }

    bool MatchServerAgent::handleAssign( const ServerBusMessage& message, int64 nowMs )
    {
        MatchFormed match;
        if ( MatchQueueBus::kAssignTopic.read( message, match ) != ServerBusReadResult::Read )
            return false; // 읽기가 (타입, 결과, 버전)마다 한 번 오류 로그와 카운터를 남겼다
        for ( size_t team = 0; team < match._listTeam.size(); ++team )
        {
            for ( const MatchMember& member : match._listTeam[team]._listMember )
            {
                _mapAccountToExpected[member._accountID] = Expected{ match._matchID, nowMs + kExpectTtlMs, static_cast<int32>( team ) };
            }
        }
        _newMatchBuffer.push( std::move( match ) );
        return true;
    }

    void MatchServerAgent::update( int64 nowMs )
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
