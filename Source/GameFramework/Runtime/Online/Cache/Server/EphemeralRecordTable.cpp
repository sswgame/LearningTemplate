#include "pch.h"

#include "GameFramework/Runtime/Online/Cache/Server/EphemeralRecordTable.h"

#include "Core/Serialization/BitStream.h"
#include "Core/String/StringUtil.h"

namespace sw
{
    EphemeralValue EphemeralValueUtil::makeID( uint64 id )
    {
        SW_ASSERT( id != 0 && "a cached id is never 0 - 0 reads as absent" );
        BitWriter writer;
        writer.writeVarUint( id );
        return EphemeralValue{ writer.releaseBytes() };
    }

    bool EphemeralValueUtil::readID( const EphemeralReply& reply, uint64& outID )
    {
        if ( reply._result != EphemeralResult::Ok )
            return false;
        BitReader    reader( reply._value.data(), static_cast<int32>( reply._value.size() ) );
        const uint64 id = reader.readVarUint();
        if ( reader.hasOverflowed() || id == 0 )
            return false;
        outID = id;
        return true;
    }

    EphemeralValue EphemeralValueUtil::makeMarker()
    {
        return EphemeralValue{ vector<uint8>{ 1 } };
    }

    bool EphemeralValueUtil::readCounter( const EphemeralReply& reply, int64& outValue )
    {
        if ( reply._result != EphemeralResult::Ok )
            return false;
        return StringUtil::parseInt64( string_view( reinterpret_cast<const utf8*>( reply._value.data() ), reply._value.size() ), outValue );
    }
} // namespace sw
