#include "pch.h"

#include "GameFramework/Kits/Feature/Online/LiveOps/Runtime/LiveOpsProtocol.h"

#include "Engine/Serialization/Format/JSONSerializer.h"

#include "GameFramework/Runtime/Online/Store/ServiceRecordUtil.h"

namespace sw
{
    bool LiveOpsProtocol::encodeEvent( const LiveEventDef& definition, vector<uint8>& outBytes )
    {
        return ServiceRecordUtil::encode( definition, outBytes );
    }

    bool LiveOpsProtocol::decodeEvent( const string_view key, const vector<uint8>& bytes, LiveEventDef& outDefinition )
    {
        return ServiceRecordUtil::readDurable( key, bytes, outDefinition );
    }

    bool LiveOpsProtocol::readEventJSON( string_view definitionJSON, LiveEventDef& outDefinition )
    {
        LiveEventDef definition;
        if ( JSONSerializer::deserialize( &definition, *LiveEventDef::StaticType(), definitionJSON ) == false )
            return false;
        outDefinition = std::move( definition );
        return true;
    }

    expected<string> LiveOpsProtocol::writeEventJSON( const LiveEventDef& definition )
    {
        return JSONSerializer::serialize( &definition, *LiveEventDef::StaticType() );
    }

    bool LiveOpsProtocol::encodeDevice( const PushDeviceRegistration& registration, vector<uint8>& outBytes )
    {
        return ServiceRecordUtil::encode( registration, outBytes );
    }

    bool LiveOpsProtocol::decodeDevice( const string_view key, const vector<uint8>& bytes, PushDeviceRegistration& outRegistration )
    {
        return ServiceRecordUtil::readDurable( key, bytes, outRegistration );
    }

} // namespace sw

namespace sw
{
    const OnlineProtocolEntry LiveOpsProtocol::kOnlineEntry{ "LiveOps", LiveOpsProtocol::Table{} };
} // namespace sw
