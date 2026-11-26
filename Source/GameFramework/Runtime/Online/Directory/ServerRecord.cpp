#include "pch.h"

#include "GameFramework/Runtime/Online/Directory/ServerRecord.h"

#include "Core/Serialization/BitStream.h"

#include "GameFramework/Runtime/Online/Store/ServiceKeyUtil.h"

namespace sw
{
    string ServerRecord::makeSeatKey( uint64 serverID )
    {
        string key{ "sd/seat/" };
        ServiceKeyUtil::appendHex64( key, serverID );
        return key;
    }

    string ServerRecord::makeRecordKey( uint64 serverID )
    {
        string key{ "sd/srv/" };
        ServiceKeyUtil::appendHex64( key, serverID );
        return key;
    }

    string ServerRecord::makeIndexKey( string_view kind )
    {
        string key{ "sd/idx/" };
        key.append( kind.data(), kind.size() );
        return key;
    }

    string ServerRecord::makeMember( uint64 serverID )
    {
        return ServiceKeyUtil::makeHex64( serverID );
    }

    bool ServerRecord::parseMember( string_view member, uint64& outServerID )
    {
        return ServiceKeyUtil::parseHex64( member, outServerID );
    }

    bool ServerRecord::isValidName( string_view name )
    {
        return ServiceKeyUtil::isTokenText( name, kMaxNameSize, "_-" );
    }
} // namespace sw
