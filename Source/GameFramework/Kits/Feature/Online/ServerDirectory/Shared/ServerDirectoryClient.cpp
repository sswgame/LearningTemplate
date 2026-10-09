#include "pch.h"

#include "GameFramework/Kits/Feature/Online/ServerDirectory/Shared/ServerDirectoryClient.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"
#include "GameFramework/Kits/Feature/Online/ServerDirectory/Shared/ServerDirectoryProtocol.h"

namespace sw
{
    ServerDirectoryClient::ServerDirectoryClient()
        : _statusCallTable{}
        , _assignmentCallTable{}
        , _serverListCallTable{}
        , _lastStatus{}
        , _pClient{ nullptr }
        , _statusRevision{ 0 }
    {
    }

    void ServerDirectoryClient::initialize( OnlineServiceClient* pClient ) { _pClient = pClient; }

    uint32 ServerDirectoryClient::getProtocolVersion() const { return ServerDirectoryMethod::kWireVersion; }

    uint64 ServerDirectoryClient::requestStatus( const StatusDelegate& onStatus )
    {
        return _statusCallTable.send( *_pClient, ServerDirectoryMethod::kGetStatus, BitWriter{}, NetRequestOptions{},
                                      OnlineResponseDelegate::create<&ServerDirectoryClient::onStatusResponse>( this ), onStatus );
    }

    uint64 ServerDirectoryClient::requestAssignment( const ServerAssignmentRequest& request, const AssignmentDelegate& onAssignment )
    {
        BitWriter body;
        ServerDirectoryProtocol::writeAssignmentRequest( body, request );
        return _assignmentCallTable.send( *_pClient, ServerDirectoryMethod::kAssignServer, body, NetRequestOptions{},
                                          OnlineResponseDelegate::create<&ServerDirectoryClient::onAssignmentResponse>( this ), onAssignment );
    }

    uint64 ServerDirectoryClient::requestServerList( string_view kind, const ServerListDelegate& onServerList )
    {
        BitWriter body;
        ServiceKeyUtil::writeString( body, kind );
        return _serverListCallTable.send( *_pClient, ServerDirectoryMethod::kListServers, body, NetRequestOptions{},
                                          OnlineResponseDelegate::create<&ServerDirectoryClient::onServerListResponse>( this ), onServerList );
    }

    void ServerDirectoryClient::onServicePush( uint16 kind, BitReader& body )
    {
        if ( kind != ServerDirectoryMethod::kPushStatus )
            return;
        ServerDirectoryStatus status;
        if ( ServerDirectoryProtocol::readStatus( body, status ) == false )
            return;
        _lastStatus = std::move( status );
        ++_statusRevision;
    }

    void ServerDirectoryClient::onStatusResponse( const OnlineResponse& response )
    {
        StatusDelegate onStatus;
        if ( _statusCallTable.take( response._requestId, onStatus ) == false )
            return;
        uint16                errorCode = response._errorCode;
        ServerDirectoryStatus status;
        if ( errorCode == OnlineError::kOk )
        {
            BitReader reader( response._pBody, response._bodySize );
            if ( ServerDirectoryProtocol::readStatus( reader, status ) )
            {
                _lastStatus = status;
                ++_statusRevision;
            }
            else
            {
                errorCode = OnlineError::kInvalidRequest;
            }
        }
        if ( onStatus.isBound() )
            onStatus( errorCode, status );
    }

    void ServerDirectoryClient::onAssignmentResponse( const OnlineResponse& response )
    {
        AssignmentDelegate onAssignment;
        if ( _assignmentCallTable.take( response._requestId, onAssignment ) == false )
            return;
        uint16           errorCode = response._errorCode;
        ServerAssignment assignment;
        if ( errorCode == OnlineError::kOk )
        {
            BitReader reader( response._pBody, response._bodySize );
            if ( ServerDirectoryProtocol::readAssignment( reader, assignment ) == false )
                errorCode = OnlineError::kInvalidRequest;
        }
        if ( onAssignment.isBound() )
            onAssignment( errorCode, assignment );
    }

    void ServerDirectoryClient::onServerListResponse( const OnlineResponse& response )
    {
        ServerListDelegate onServerList;
        if ( _serverListCallTable.take( response._requestId, onServerList ) == false )
            return;
        uint16                  errorCode = response._errorCode;
        vector<ServerListEntry> listEntry;
        if ( errorCode == OnlineError::kOk )
        {
            BitReader reader( response._pBody, response._bodySize );
            if ( ServerDirectoryProtocol::readServerList( reader, listEntry ) == false )
                errorCode = OnlineError::kInvalidRequest;
        }
        if ( onServerList.isBound() )
            onServerList( errorCode, listEntry );
    }
} // namespace sw
