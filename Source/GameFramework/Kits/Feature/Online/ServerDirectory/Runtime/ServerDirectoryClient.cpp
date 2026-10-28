#include "pch.h"

#include "GameFramework/Kits/Feature/Online/ServerDirectory/Runtime/ServerDirectoryClient.h"

#include "Core/Serialization/BitStream.h"

#include "GameFramework/Kits/Feature/Online/ServerDirectory/Runtime/ServerDirectoryProtocol.h"

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

    void ServerDirectoryClient::initialize( OnlineServiceClient* pClient )
    {
        _pClient = pClient;
    }

    const OnlineProtocolEntry& ServerDirectoryClient::getProtocol() const
    {
        return ServerDirectoryProtocol::kOnlineEntry;
    }

    uint64 ServerDirectoryClient::requestStatus( const StatusDelegate& onStatus )
    {
        return _statusCallTable.send( *_pClient, ServerDirectoryMethod::kGetStatus, BitWriter{}, NetRequestOptions{},
                                      OnlineResponseDelegate::create<&ServerDirectoryClient::onStatusResponse>( this ), onStatus );
    }

    uint64 ServerDirectoryClient::requestAssignment( const ServerAssignmentRequest& request, const AssignmentDelegate& onAssignment )
    {
        return _assignmentCallTable.sendMessage<ServerDirectoryProtocol::MessageTypes>( *_pClient, ServerDirectoryMethod::kAssignServer, request, NetRequestOptions{},
                                                                                        OnlineResponseDelegate::create<&ServerDirectoryClient::onAssignmentResponse>( this ), onAssignment );
    }

    uint64 ServerDirectoryClient::requestServerList( string_view kind, const ServerListDelegate& onServerList )
    {
        ServerListRequest request;
        request._kind = string( kind );
        return _serverListCallTable.sendMessage<ServerDirectoryProtocol::MessageTypes>( *_pClient, ServerDirectoryMethod::kListServers, request, NetRequestOptions{},
                                                                                        OnlineResponseDelegate::create<&ServerDirectoryClient::onServerListResponse>( this ), onServerList );
    }

    void ServerDirectoryClient::onServicePush( uint16 kind, BitReader& body )
    {
        if ( kind != ServerDirectoryMethod::kPushStatus )
            return;
        ServerDirectoryStatus status;
        if ( ServerDirectoryProtocol::MessageTypes::read( body, status ) != FixedReadResult::Ok )
            return;
        _lastStatus = std::move( status );
        ++_statusRevision;
    }

    void ServerDirectoryClient::onStatusResponse( const OnlineResponse& response )
    {
        StatusDelegate onStatus;
        if ( _statusCallTable.take( response._requestID, onStatus ) == false )
            return;
        uint16                errorCode = response._errorCode;
        ServerDirectoryStatus status;
        if ( errorCode == OnlineError::kOk )
        {
            BitReader reader( response._pBody, response._bodySize );
            if ( ServerDirectoryProtocol::MessageTypes::read( reader, status ) == FixedReadResult::Ok )
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
        if ( _assignmentCallTable.take( response._requestID, onAssignment ) == false )
            return;
        uint16           errorCode = response._errorCode;
        ServerAssignment assignment;
        if ( errorCode == OnlineError::kOk )
        {
            BitReader reader( response._pBody, response._bodySize );
            if ( ServerDirectoryProtocol::MessageTypes::read( reader, assignment ) != FixedReadResult::Ok )
                errorCode = OnlineError::kInvalidRequest;
        }
        if ( onAssignment.isBound() )
            onAssignment( errorCode, assignment );
    }

    void ServerDirectoryClient::onServerListResponse( const OnlineResponse& response )
    {
        ServerListDelegate onServerList;
        if ( _serverListCallTable.take( response._requestID, onServerList ) == false )
            return;
        uint16          errorCode = response._errorCode;
        ServerListReply listReply;
        if ( errorCode == OnlineError::kOk )
        {
            BitReader reader( response._pBody, response._bodySize );
            if ( ServerDirectoryProtocol::MessageTypes::read( reader, listReply ) != FixedReadResult::Ok )
                errorCode = OnlineError::kInvalidRequest;
        }
        if ( onServerList.isBound() )
            onServerList( errorCode, listReply._listEntry );
    }
} // namespace sw
