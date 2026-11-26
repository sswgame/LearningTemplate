#include "pch.h"

#include "GameFramework/Kits/Feature/Online/LiveOps/Runtime/LiveOpsProtocol.h"

#include "Engine/Serialization/Format/JSONSerializer.h"

namespace sw
{
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

} // namespace sw

namespace sw
{
    const OnlineProtocolEntry LiveOpsProtocol::kOnlineEntry{ "LiveOps", LiveOpsProtocol::Table{} };
} // namespace sw
