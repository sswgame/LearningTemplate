#include "pch.h"

#include "GameFramework/Base/Online/Directory/ServerRecord.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"

namespace sw
{
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

    string ServerRecord::makeMember( uint64 serverID ) { return ServiceKeyUtil::makeHex64( serverID ); }

    bool ServerRecord::parseMember( string_view member, uint64& outServerID ) { return ServiceKeyUtil::parseHex64( member, outServerID ); }

    bool ServerRecord::isValidName( string_view name )
    {
        if ( name.empty() || name.size() > static_cast<size_t>( kMaxNameSize ) )
            return false;
        for ( const utf8 ch : name )
        {
            const bool bAllowed = ( 'a' <= ch && ch <= 'z' ) || ( '0' <= ch && ch <= '9' ) || ch == '_' || ch == '-';
            if ( bAllowed == false )
                return false;
        }
        return true;
    }

    void ServerRecord::writeStatus( BitWriter& outWriter, const ServerStatus& status )
    {
        const ServerDescriptor& descriptor = status._descriptor;
        outWriter.writeVarUint( descriptor._serverID );
        ServiceKeyUtil::writeString( outWriter, descriptor._kind );
        ServiceKeyUtil::writeString( outWriter, descriptor._region );
        ServiceKeyUtil::writeString( outWriter, descriptor._address );
        outWriter.writeBits( descriptor._port, 16 );
        outWriter.writeVarUint( descriptor._buildVersion );
        outWriter.writeVarInt( descriptor._capacity );
        outWriter.writeVarInt( status._heartbeatMs );
        outWriter.writeVarInt( status._load );
        outWriter.writeBits( static_cast<uint32>( status._state ), 8 );
    }

    bool ServerRecord::readStatus( BitReader& reader, ServerStatus& outStatus )
    {
        ServerDescriptor& descriptor = outStatus._descriptor;
        descriptor._serverID         = reader.readVarUint();
        const bool bTextOk           = ServiceKeyUtil::readString( reader, kMaxNameSize, descriptor._kind ) &&
                             ServiceKeyUtil::readString( reader, kMaxNameSize, descriptor._region ) &&
                             ServiceKeyUtil::readString( reader, kMaxAddressSize, descriptor._address );
        if ( bTextOk == false )
            return false;
        descriptor._port         = static_cast<uint16>( reader.readBits( 16 ) );
        descriptor._buildVersion = static_cast<uint32>( reader.readVarUint() );
        descriptor._capacity     = static_cast<int32>( reader.readVarInt() );
        outStatus._heartbeatMs   = reader.readVarInt();
        outStatus._load          = static_cast<int32>( reader.readVarInt() );
        const uint32 state       = reader.readBits( 8 );
        if ( reader.hasOverflowed() || state >= static_cast<uint32>( ServerState::Count ) )
            return false;
        outStatus._state = static_cast<ServerState>( state );
        return true;
    }

    vector<uint8> ServerRecord::encode( const ServerStatus& status )
    {
        BitWriter writer;
        writer.writeBits( kFormatVersion, 8 );
        writeStatus( writer, status );
        return writer.releaseBytes();
    }

    bool ServerRecord::decode( const vector<uint8>& bytes, ServerStatus& outStatus )
    {
        BitReader reader( bytes.data(), static_cast<int32>( bytes.size() ) );
        if ( reader.readBits( 8 ) != kFormatVersion )
            return false;
        return readStatus( reader, outStatus );
    }
} // namespace sw
