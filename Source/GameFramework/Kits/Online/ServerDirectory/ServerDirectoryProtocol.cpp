#include "pch.h"

#include "GameFramework/Kits/Online/ServerDirectory/ServerDirectoryProtocol.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"

namespace sw
{
    void ServerDirectoryProtocol::writeMaintenance( BitWriter& outWriter, const MaintenanceWindow& window, bool bIncludeAllowList )
    {
        ServiceKeyUtil::writeString( outWriter, window._scope );
        ServiceKeyUtil::writeString( outWriter, window._messageKey );
        outWriter.writeVarInt( window._startMs );
        outWriter.writeVarInt( window._endMs );
        if ( bIncludeAllowList == false )
            return;
        outWriter.writeVarUint( window._listAllowedAccount.size() );
        for ( const AccountId accountId : window._listAllowedAccount )
            outWriter.writeVarUint( accountId );
    }

    bool ServerDirectoryProtocol::readMaintenance( BitReader& reader, bool bIncludeAllowList, MaintenanceWindow& outWindow )
    {
        const bool bTextOk = ServiceKeyUtil::readString( reader, ServerRecord::kMaxNameSize, outWindow._scope ) &&
                             ServiceKeyUtil::readString( reader, ServerDirectoryLimit::kMaxMessageKeySize, outWindow._messageKey );
        if ( bTextOk == false )
            return false;
        outWindow._startMs = reader.readVarInt();
        outWindow._endMs   = reader.readVarInt();
        outWindow._listAllowedAccount.clear();
        if ( bIncludeAllowList )
        {
            const uint64 count = reader.readVarUint();
            if ( count > static_cast<uint64>( ServerDirectoryLimit::kMaxAllowedAccountCount ) )
                return false;
            for ( uint64 index = 0; index < count; ++index )
                outWindow._listAllowedAccount.push_back( reader.readVarUint() );
        }
        return reader.hasOverflowed() == false;
    }

    void ServerDirectoryProtocol::writeNotice( BitWriter& outWriter, const ServiceNotice& notice )
    {
        outWriter.writeVarUint( notice._noticeId );
        ServiceKeyUtil::writeString( outWriter, notice._text );
        outWriter.writeVarInt( notice._startMs );
        outWriter.writeVarInt( notice._endMs );
        outWriter.writeVarInt( notice._priority );
        outWriter.writeBool( notice._bLiteralText == SW_TRUE );
    }

    bool ServerDirectoryProtocol::readNotice( BitReader& reader, ServiceNotice& outNotice )
    {
        outNotice._noticeId = reader.readVarUint();
        if ( ServiceKeyUtil::readString( reader, ServerDirectoryLimit::kMaxNoticeTextSize, outNotice._text ) == false )
            return false;
        outNotice._startMs      = reader.readVarInt();
        outNotice._endMs        = reader.readVarInt();
        outNotice._priority     = static_cast<int32>( reader.readVarInt() );
        outNotice._bLiteralText = reader.readBool() ? SW_TRUE : SW_FALSE;
        return reader.hasOverflowed() == false;
    }

    void ServerDirectoryProtocol::writeStatus( BitWriter& outWriter, const ServerDirectoryStatus& status )
    {
        outWriter.writeVarUint( status._listMaintenance.size() );
        for ( const MaintenanceWindow& window : status._listMaintenance )
            writeMaintenance( outWriter, window, false );
        outWriter.writeVarUint( status._listNotice.size() );
        for ( const ServiceNotice& notice : status._listNotice )
            writeNotice( outWriter, notice );
    }

    bool ServerDirectoryProtocol::readStatus( BitReader& reader, ServerDirectoryStatus& outStatus )
    {
        const uint64 windowCount = reader.readVarUint();
        if ( windowCount > static_cast<uint64>( ServerDirectoryLimit::kMaxWindowCount ) )
            return false;
        outStatus._listMaintenance.resize( static_cast<size_t>( windowCount ) );
        for ( MaintenanceWindow& window : outStatus._listMaintenance )
        {
            if ( readMaintenance( reader, false, window ) == false )
                return false;
        }
        const uint64 noticeCount = reader.readVarUint();
        if ( noticeCount > static_cast<uint64>( ServerDirectoryLimit::kMaxNoticeCount ) )
            return false;
        outStatus._listNotice.resize( static_cast<size_t>( noticeCount ) );
        for ( ServiceNotice& notice : outStatus._listNotice )
        {
            if ( readNotice( reader, notice ) == false )
                return false;
        }
        return reader.hasOverflowed() == false;
    }

    void ServerDirectoryProtocol::writeAssignmentRequest( BitWriter& outWriter, const ServerAssignmentRequest& request )
    {
        ServiceKeyUtil::writeString( outWriter, request._kind );
        ServiceKeyUtil::writeString( outWriter, request._region );
        outWriter.writeVarUint( request._buildVersion );
        outWriter.writeVarInt( request._seatCount );
    }

    bool ServerDirectoryProtocol::readAssignmentRequest( BitReader& reader, ServerAssignmentRequest& outRequest )
    {
        const bool bTextOk = ServiceKeyUtil::readString( reader, ServerRecord::kMaxNameSize, outRequest._kind ) &&
                             ServiceKeyUtil::readString( reader, ServerRecord::kMaxNameSize, outRequest._region );
        if ( bTextOk == false )
            return false;
        outRequest._buildVersion = static_cast<uint32>( reader.readVarUint() );
        outRequest._seatCount    = static_cast<int32>( reader.readVarInt() );
        return reader.hasOverflowed() == false && 1 <= outRequest._seatCount && outRequest._seatCount <= ServerDirectoryLimit::kMaxSeatCount;
    }

    void ServerDirectoryProtocol::writeAssignment( BitWriter& outWriter, const ServerAssignment& assignment )
    {
        outWriter.writeBits( static_cast<uint32>( assignment._result ), 8 );
        outWriter.writeVarUint( assignment._serverId );
        ServiceKeyUtil::writeString( outWriter, assignment._address );
        outWriter.writeBits( assignment._port, 16 );
        outWriter.writeVarInt( assignment._maintenanceEndMs );
        ServiceKeyUtil::writeString( outWriter, assignment._messageKey );
    }

    bool ServerDirectoryProtocol::readAssignment( BitReader& reader, ServerAssignment& outAssignment )
    {
        const uint32 result     = reader.readBits( 8 );
        outAssignment._serverId = reader.readVarUint();
        if ( result >= static_cast<uint32>( ServerDirectoryResult::Count ) ||
             ServiceKeyUtil::readString( reader, ServerRecord::kMaxAddressSize, outAssignment._address ) == false )
            return false;
        outAssignment._result           = static_cast<ServerDirectoryResult>( result );
        outAssignment._port             = static_cast<uint16>( reader.readBits( 16 ) );
        outAssignment._maintenanceEndMs = reader.readVarInt();
        return ServiceKeyUtil::readString( reader, ServerDirectoryLimit::kMaxMessageKeySize, outAssignment._messageKey ) && reader.hasOverflowed() == false;
    }

    void ServerDirectoryProtocol::writeServerList( BitWriter& outWriter, const vector<ServerListEntry>& listEntry )
    {
        outWriter.writeVarUint( listEntry.size() );
        for ( const ServerListEntry& entry : listEntry )
        {
            outWriter.writeVarUint( entry._serverId );
            ServiceKeyUtil::writeString( outWriter, entry._region );
            ServiceKeyUtil::writeString( outWriter, entry._address );
            outWriter.writeBits( entry._port, 16 );
            outWriter.writeBits( entry._fillPercent, 8 );
            outWriter.writeBits( static_cast<uint32>( entry._state ), 8 );
        }
    }

    bool ServerDirectoryProtocol::readServerList( BitReader& reader, vector<ServerListEntry>& outListEntry )
    {
        const uint64 count = reader.readVarUint();
        if ( count > static_cast<uint64>( ServerDirectoryLimit::kMaxServerListCount ) )
            return false;
        outListEntry.resize( static_cast<size_t>( count ) );
        for ( ServerListEntry& entry : outListEntry )
        {
            entry._serverId    = reader.readVarUint();
            const bool bTextOk = ServiceKeyUtil::readString( reader, ServerRecord::kMaxNameSize, entry._region ) &&
                                 ServiceKeyUtil::readString( reader, ServerRecord::kMaxAddressSize, entry._address );
            if ( bTextOk == false )
                return false;
            entry._port        = static_cast<uint16>( reader.readBits( 16 ) );
            entry._fillPercent = static_cast<uint8>( reader.readBits( 8 ) );
            const uint32 state = reader.readBits( 8 );
            if ( state >= static_cast<uint32>( ServerState::Count ) )
                return false;
            entry._state = static_cast<ServerState>( state );
        }
        return reader.hasOverflowed() == false;
    }
} // namespace sw
