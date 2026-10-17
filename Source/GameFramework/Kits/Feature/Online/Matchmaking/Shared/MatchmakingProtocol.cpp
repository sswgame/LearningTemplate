#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Matchmaking/Shared/MatchmakingProtocol.h"

#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"
#include "GameFramework/Base/Online/Store/ServiceRecordUtil.h"

namespace sw
{
    namespace
    {
        constexpr uint32 kMatchmakingRecordVersion = 1; ///< 캐시 기록(A)의 판이다. 필드를 빼거나 뜻을 바꿀 때 올린다. 옛 기록은 읽지 못해 버려지고 다시 만들어진다
    } // namespace
} // namespace sw

namespace sw
{
    bool MatchmakingProtocol::encodeParty( const PartySnapshot& party, vector<uint8>& outBytes )
    {
        return ServiceRecordUtil::encode( kMatchmakingRecordVersion, party, outBytes );
    }

    bool MatchmakingProtocol::decodeParty( const vector<uint8>& bytes, PartySnapshot& outParty )
    {
        return ServiceRecordUtil::readCache( bytes, kMatchmakingRecordVersion, outParty );
    }

    bool MatchmakingProtocol::encodeLobby( const LobbySnapshot& lobby, vector<uint8>& outBytes )
    {
        return ServiceRecordUtil::encode( kMatchmakingRecordVersion, lobby, outBytes );
    }

    bool MatchmakingProtocol::decodeLobby( const vector<uint8>& bytes, LobbySnapshot& outLobby )
    {
        return ServiceRecordUtil::readCache( bytes, kMatchmakingRecordVersion, outLobby );
    }

    vector<uint8> MatchmakingProtocol::encodeID( uint64 id )
    {
        const string text = ServiceKeyUtil::makeHex64( id );
        return vector<uint8>( reinterpret_cast<const uint8*>( text.data() ), reinterpret_cast<const uint8*>( text.data() ) + text.size() );
    }

    bool MatchmakingProtocol::decodeID( const vector<uint8>& bytes, uint64& outID )
    {
        const string_view text( reinterpret_cast<const utf8*>( bytes.data() ), bytes.size() );
        return ServiceKeyUtil::parseHex64( text, outID );
    }

    MatchmakingResult MatchmakingProtocol::fromErrorCode( uint16 errorCode )
    {
        static constexpr OnlineErrorResult<MatchmakingResult> kArrErrorResult[] = {
            {            OnlineError::kOk,       MatchmakingResult::Ok},
            {OnlineError::kInvalidRequest,  MatchmakingResult::Invalid},
            {      OnlineError::kConflict, MatchmakingResult::Conflict},
        };
        return OnlineError::toResult( errorCode, kArrErrorResult, MatchmakingResult::Unavailable );
    }
} // namespace sw

namespace sw
{
    const OnlineProtocolEntry MatchmakingProtocol::kOnlineEntry{ "Matchmaking", MatchmakingMethod{}, MatchmakingProtocol::MessageTypes{} };
} // namespace sw
