#include "pch.h"

#include "GameFramework/Runtime/Online/Store/Server/ServiceRecordTable.h"

#include "Core/Log/Logger.h"
#include "Core/Serialization/BitStream.h"

namespace sw
{
    bool ServiceLinkTable::decode( string_view key, const ServiceRecord& record, uint64& outID ) const
    {
        BitReader    reader{ record._bytes.data(), static_cast<int32>( record._bytes.size() ) };
        const uint64 id = reader.readVarUint();
        if ( reader.hasOverflowed() || id == 0 )
        {
            SW_LOG_ERROR_ID( "ServiceLinkTable.LinkRecordUnreadable", "The link record '%#' of table %# holds no id (%# bytes) - requests that follow it fail until the record is fixed", key,
                             _name.c_str(), static_cast<uint64>( record._bytes.size() ) );
            return false;
        }
        outID = id;
        return true;
    }

    ServiceStoreResult ServiceLinkTable::read( IServiceStoreConnection& connection, string_view key, uint64& outID, uint64* pOutVersion ) const
    {
        ServiceRecord            record;
        const ServiceStoreResult result = connection.readRecord( _name, key, record );
        if ( pOutVersion != nullptr )
            *pOutVersion = record._version;
        if ( result != ServiceStoreResult::Ok )
            return result;
        return decode( key, record, outID ) ? ServiceStoreResult::Ok : ServiceStoreResult::Corrupt;
    }

    bool ServiceLinkTable::encode( string_view key, uint64 id, vector<uint8>& outBytes ) const
    {
        outBytes.clear();
        if ( id == 0 )
        {
            SW_LOG_ERROR_ID( "ServiceLinkTable.LinkToNoID", "The link record '%#' of table %# was given id 0 - the transaction is refused", key, _name.c_str() );
            return false;
        }
        BitWriter writer;
        writer.writeVarUint( id );
        outBytes = writer.releaseBytes();
        return true;
    }
} // namespace sw
