#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/MatchServerAgent.h"

#include "Core/Serialization/BitStream.h"

#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/MatchQueueService.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Shared/MatchmakingProtocol.h"

namespace sw
{
    MatchServerAgent::MatchServerAgent()
        : _mutex{}
        , _mapAccountToExpected{}
        , _newMatchBuffer{}
        , _descriptor{}
    {
    }

    void MatchServerAgent::initialize( const ServerDescriptor& descriptor )
    {
        _descriptor = descriptor;
    }

    string MatchServerAgent::getAssignTopic() const
    {
        return MatchQueueService::makeAssignTopic( _descriptor._serverID );
    }

    bool MatchServerAgent::handleAssign( const ServerBusMessage& message, UnixMilliseconds nowMs )
    {
        MatchFormed match;
        if ( MatchQueueBus::kAssignTopic.read( message, match ) != ServerBusReadResult::Read )
            return false; // 읽기가 (타입, 결과, 버전)마다 한 번 오류 로그와 카운터를 남겼다
        ScopedLock<mutex> lock{ _mutex };
        for ( size_t team = 0; team < match._listTeam.size(); ++team )
        {
            for ( const MatchMember& member : match._listTeam[team]._listMember )
            {
                _mapAccountToExpected[member._accountID] = Expected{ match._matchID, nowMs.count() + kExpectTTLMs, static_cast<int32>( team ) };
            }
        }
        _newMatchBuffer.push( std::move( match ) );
        return true;
    }

    bool MatchServerAgent::requestBackfill( ServerBus& bus, const MatchBackfillRequest& request ) const
    {
        MatchBackfillRequest sent = request;
        sent._serverID            = _descriptor._serverID;
        sent._address             = _descriptor._address;
        sent._port                = _descriptor._port;
        sent._portCount           = _descriptor._portCount;
        return MatchQueueBus::kBackfillTopic.publish( bus, sent._modeID, sent );
    }

    void MatchServerAgent::update( UnixMilliseconds nowMs )
    {
        ScopedLock<mutex> lock{ _mutex };
        for ( auto expectedIt = _mapAccountToExpected.begin(); expectedIt != _mapAccountToExpected.end(); )
        {
            if ( nowMs.count() >= expectedIt->second._expiresMs )
                expectedIt = _mapAccountToExpected.erase( expectedIt );
            else
                ++expectedIt;
        }
    }

    bool MatchServerAgent::findExpected( AccountID accountID, uint64& outMatchID, int32& outTeam ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        const auto        expectedIt = _mapAccountToExpected.find( accountID );
        if ( expectedIt == _mapAccountToExpected.end() )
            return false;
        outMatchID = expectedIt->second._matchID;
        outTeam    = expectedIt->second._team;
        return true;
    }

    bool MatchServerAgent::isAdmitted( AccountID accountID ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _mapAccountToExpected.find( accountID ) != _mapAccountToExpected.end();
    }

    int32 MatchServerAgent::getExpectedCount() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return static_cast<int32>( _mapAccountToExpected.size() );
    }
} // namespace sw
