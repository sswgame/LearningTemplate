#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Matchmaking/Runtime/MatchmakingProtocol.h"

#include "Core/String/StringUtil.h"

#include "GameFramework/Runtime/Online/Store/ServiceKeyUtil.h"
#include "GameFramework/Runtime/Online/Store/ServiceRecordUtil.h"

namespace sw
{
    bool MatchmakingProtocol::encodeParty( const PartySnapshot& party, vector<uint8>& outBytes )
    {
        return ServiceRecordUtil::encode( party, outBytes );
    }

    bool MatchmakingProtocol::decodeParty( const vector<uint8>& bytes, PartySnapshot& outParty )
    {
        return ServiceRecordUtil::readCache( bytes, outParty );
    }

    bool MatchmakingProtocol::encodeLobby( const LobbySnapshot& lobby, vector<uint8>& outBytes )
    {
        return ServiceRecordUtil::encode( lobby, outBytes );
    }

    bool MatchmakingProtocol::decodeLobby( const vector<uint8>& bytes, LobbySnapshot& outLobby )
    {
        return ServiceRecordUtil::readCache( bytes, outLobby );
    }

    vector<uint8> MatchmakingProtocol::encodeID( uint64 id )
    {
        return StringUtil::toBytes( ServiceKeyUtil::makeHex64( id ) );
    }

    bool MatchmakingProtocol::decodeID( const vector<uint8>& bytes, uint64& outID )
    {
        const string_view text( reinterpret_cast<const utf8*>( bytes.data() ), bytes.size() );
        return ServiceKeyUtil::parseHex64( text, outID );
    }

} // namespace sw

namespace sw
{
    const OnlineProtocolEntry MatchmakingProtocol::kOnlineEntry{ "Matchmaking", MatchmakingProtocol::Table{} };
} // namespace sw
